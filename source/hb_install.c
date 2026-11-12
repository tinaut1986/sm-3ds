#include "hb_install.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define HB_DEFAULT_PATH "sdmc:/3ds/SuperMetroid3DSPort.3dsx"

extern int __system_argc;   // libctru: the arguments the launcher passed
extern char **__system_argv;

// The romfs: 0 open, 1 an install asks to close it, 2 closed (HbInstall_MainTick).
static volatile int sRomfs = 0;

bool HbInstall_IsHomebrew(void) { return envIsHomebrew(); }

void HbInstall_MainTick(void) {
  if (sRomfs == 1) {
    romfsExit();
    sRomfs = 2;
  }
}

bool HbInstall_RomfsClosed(void) { return sRomfs == 2; }

// Waits up to 2 s for the main thread; when it does not answer the rename below fails and the
// copy is used instead.
static void CloseRomfs(void) {
  if (sRomfs == 2) return;
  sRomfs = 1;
  for (int i = 0; i < 200 && sRomfs != 2; i++) svcSleepThread(10 * 1000 * 1000LL);
}

// Rosalina's hb:ldr, as ../new-hbmenu's loader (source/loaders/rosalina.c) talks to it.
static bool HbLdrConnect(Handle *h) { return R_SUCCEEDED(svcConnectToPort(h, "hb:ldr")); }

static Result HbLdrCall(Handle h, u32 cmd, const void *buf, u32 size) {
  u32 *cmdbuf = getThreadCommandBuffer();
  cmdbuf[0] = IPC_MakeHeader(cmd, 0, 2);
  cmdbuf[1] = IPC_Desc_StaticBuffer(size, 0);
  cmdbuf[2] = (u32)buf;
  Result rc = svcSendSyncRequest(h);
  return R_SUCCEEDED(rc) ? (Result)cmdbuf[1] : rc;
}

bool HbInstall_CanRelaunch(void) {
  Handle h = 0;
  if (!HbInstall_IsHomebrew() || !HbLdrConnect(&h)) return false;
  svcCloseHandle(h);
  return true;
}

bool HbInstall_SetRelaunch(void) {
  // The argument block the loader hands the next 3dsx (argc, then the strings): argv[0] is the
  // file again, so HbInstall_Path() finds it after the restart.
  static u32 argbuf[0x400 / sizeof(u32)];
  const char *path = HbInstall_Path();
  Handle h = 0;
  if (!HbInstall_IsHomebrew() || !HbLdrConnect(&h)) return false;
  memset(argbuf, 0, sizeof(argbuf));
  argbuf[0] = 1;
  snprintf((char *)&argbuf[1], sizeof(argbuf) - sizeof(u32), "%s", path);
  const char *target = !strncmp(path, "sdmc:/", 6) ? path + 5 : path;   // the loader wants "/3ds/..."
  const bool ok = R_SUCCEEDED(HbLdrCall(h, 2, target, strlen(target) + 1)) &&   // SetTarget
                  R_SUCCEEDED(HbLdrCall(h, 3, argbuf, sizeof(argbuf)));         // SetArgv
  svcCloseHandle(h);
  if (ok) aptSetChainloaderToSelf();
  return ok;
}

static bool CopyFile(const char *from, const char *to) {
  static char buf[64 * 1024];   // one install at a time (the updater and SITE INSTALL never overlap)
  FILE *in = fopen(from, "rb");
  if (!in) return false;
  FILE *out = fopen(to, "wb");
  bool ok = out != NULL;
  size_t n;
  while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0) ok = fwrite(buf, 1, n, out) == n;
  ok = ok && !ferror(in);
  fclose(in);
  if (out && fclose(out) != 0) ok = false;
  return ok;
}

static bool EndsWith(const char *s, const char *end) {
  const size_t n = strlen(s), m = strlen(end);
  return n >= m && strcmp(s + n - m, end) == 0;
}

// Where an install put the new 3dsx when that is not the running file (InstallTarget), so the
// relaunch and the FTP server's reply name the new one.
static char sInstalledPath[288];

const char *HbInstall_Path(void) {
  if (sInstalledPath[0]) return sInstalledPath;
  // The launcher passes "sdmc:/3ds/<file>.3dsx"; 3dslink passes "3dslink:/...", which is no file.
  if (__system_argc > 0 && __system_argv && __system_argv[0] && !strncmp(__system_argv[0], "sdmc:/", 6) &&
      EndsWith(__system_argv[0], ".3dsx"))
    return __system_argv[0];
  return HB_DEFAULT_PATH;
}

// The file an install writes. Releases up to v1.0.3 published the 3dsx as "sm-3ds-<tag>.3dsx" and
// the update kept that name, so the file carried an old version forever and a new download sat
// beside it (#61). A running file with that name is replaced by "sm-3ds.3dsx" in the same folder,
// the name every release has used since; any other name is kept.
static const char *InstallTarget(char *buf, size_t cap) {
  const char *running = HbInstall_Path();
  const char *slash = strrchr(running, '/');
  const char *base = slash ? slash + 1 : running;
  if (strncmp(base, "sm-3ds-v", 8) != 0 || !EndsWith(base, ".3dsx")) return running;
  snprintf(buf, cap, "%.*ssm-3ds.3dsx", (int)(base - running), running);
  return buf;
}

static bool Is3dsx(const char *path) {
  char magic[4];
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  const bool ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "3DSX", 4);
  fclose(f);
  return ok;
}

// A new 3dsx is in place at `target`. When that is not the running file, the old one goes (it is
// a versioned copy of the same game) and the relaunch opens the new one. If the old file cannot
// be removed (the romfs did not close) it stays: the new one is still the one that starts.
static void Installed(const char *target, const char *running) {
  if (target == running) return;
  snprintf(sInstalledPath, sizeof(sInstalledPath), "%s", target);
  remove(running);
}

bool HbInstall_File(const char *path, char *error, unsigned errorCap) {
  const char *running = HbInstall_Path();
  char targetBuf[288];
  const char *target = InstallTarget(targetBuf, sizeof(targetBuf));
  char bak[300];
  struct stat st;

  if (!Is3dsx(path)) {
    snprintf(error, errorCap, "not a 3dsx");
    return false;
  }
  CloseRomfs();
  snprintf(bak, sizeof(bak), "%s.bak", target);
  remove(bak);
  const bool had = stat(target, &st) == 0;
  if (!had || rename(target, bak) == 0) {
    if (rename(path, target) == 0) {
      remove(bak);
      Installed(target, running);
      return true;
    }
    if (had) rename(bak, target);
  }
  // The file is still held open (the romfs did not close): its contents can be written over.
  if (had && !CopyFile(target, bak)) {
    snprintf(error, errorCap, "cannot back up the old 3dsx");
    return false;
  }
  if (!CopyFile(path, target)) {
    if (had) CopyFile(bak, target);
    remove(bak);
    snprintf(error, errorCap, "cannot replace the 3dsx");
    return false;
  }
  remove(path);
  remove(bak);
  Installed(target, running);
  return true;
}
