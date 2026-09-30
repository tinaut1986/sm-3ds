#include "rom_loader.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// Super Metroid (Japan, USA), headerless.
const char kRomExpectedSha1[41] = "da957f0d63d14cb441d215462904c4fa8519c613";

#define ROM_SIZE 0x300000u    // 3 MiB
#define COPIER_HEADER 0x200u

// ---- SHA-1 ---------------------------------------------------------------

typedef struct {
  uint32_t h[5];
  uint64_t bytes;
  uint8_t buf[64];
  size_t fill;
} Sha1Ctx;

static uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void Sha1Block(Sha1Ctx *c, const uint8_t *p) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
           (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
  for (int i = 16; i < 80; i++)
    w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20)      { f = (b & cc) | (~b & d);           k = 0x5A827999; }
    else if (i < 40) { f = b ^ cc ^ d;                    k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
    else             { f = b ^ cc ^ d;                    k = 0xCA62C1D6; }
    uint32_t t = Rol(a, 5) + f + e + k + w[i];
    e = d; d = cc; cc = Rol(b, 30); b = a; a = t;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void Sha1Update(Sha1Ctx *c, const uint8_t *data, size_t len) {
  c->bytes += len;
  while (len) {
    size_t n = 64 - c->fill;
    if (n > len) n = len;
    memcpy(c->buf + c->fill, data, n);
    c->fill += n; data += n; len -= n;
    if (c->fill == 64) { Sha1Block(c, c->buf); c->fill = 0; }
  }
}

void Sha1Hex(const uint8_t *data, size_t len, char out[41]) {
  Sha1Ctx c = { { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 }, 0, { 0 }, 0 };
  Sha1Update(&c, data, len);
  uint64_t bits = c.bytes * 8;
  uint8_t pad = 0x80;
  Sha1Update(&c, &pad, 1);
  pad = 0;
  while (c.fill != 56) Sha1Update(&c, &pad, 1);
  uint8_t lenbe[8];
  for (int i = 0; i < 8; i++) lenbe[i] = (uint8_t)(bits >> (56 - 8 * i));
  Sha1Update(&c, lenbe, 8);
  for (int i = 0; i < 5; i++) snprintf(out + i * 8, 9, "%08x", (unsigned)c.h[i]);
}

// ---- Scanning ------------------------------------------------------------

static bool HasRomExtension(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot && (!strcasecmp(dot, ".smc") || !strcasecmp(dot, ".sfc"));
}

// Hashes the file with any copier header removed. Returns false if it is not
// even the right size (cheap reject before reading 3 MiB).
static bool HashRomFile(const char *path, char sha1[41], bool *had_header) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  bool ok = false;
  if (size == (long)ROM_SIZE || size == (long)(ROM_SIZE + COPIER_HEADER)) {
    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (buf && fread(buf, 1, (size_t)size, f) == (size_t)size) {
      size_t skip = (size_t)size - ROM_SIZE;
      *had_header = skip != 0;
      Sha1Hex(buf + skip, ROM_SIZE, sha1);
      ok = true;
    }
    free(buf);
  }
  fclose(f);
  return ok;
}

RomStatus RomLoader_Find(RomInfo *info) {
  memset(info, 0, sizeof(*info));

  DIR *dir = opendir(ROM_DATA_DIR);
  if (!dir) {
    // Create the folder so the user sees where to put the ROM.
    mkdir("sdmc:/3ds", 0777);
    mkdir(ROM_DATA_DIR, 0777);
    return info->status = ROM_NO_DIR;
  }

  int candidates = 0;
  struct dirent *e;
  while ((e = readdir(dir)) != NULL) {
    if (!HasRomExtension(e->d_name)) continue;
    char path[512];
    snprintf(path, sizeof(path), ROM_DATA_DIR "/%s", e->d_name);
    char sha1[41] = "";
    bool header = false;
    candidates++;
    if (HashRomFile(path, sha1, &header) && !strcmp(sha1, kRomExpectedSha1)) {
      snprintf(info->path, sizeof(info->path), "%s", path);
      snprintf(info->name, sizeof(info->name), "%s", e->d_name);
      snprintf(info->sha1, sizeof(info->sha1), "%s", sha1);
      info->had_header = header;
      closedir(dir);
      return info->status = ROM_OK;
    }
    if (info->rejected++ == 0) {
      snprintf(info->name, sizeof(info->name), "%s", e->d_name);
      snprintf(info->sha1, sizeof(info->sha1), "%s", sha1[0] ? sha1 : "(wrong size)");
    }
  }
  closedir(dir);
  return info->status = candidates ? ROM_BAD_HASH : ROM_NO_FILES;
}

const char *RomLoader_StatusText(RomStatus status) {
  switch (status) {
  case ROM_OK:       return "OK";
  case ROM_NO_DIR:   return "Folder created. Copy your ROM into it.";
  case ROM_NO_FILES: return "No .smc/.sfc file found in the folder.";
  case ROM_BAD_HASH: return "ROM found, but it is not Super Metroid (Japan, USA).";
  }
  return "?";
}
