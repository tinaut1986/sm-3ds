#pragma once
// The Homebrew Launcher build (.3dsx): where it runs from, and how a new one replaces it. The
// updater (updater.c) and the FTP server's SITE INSTALL (ftp_server.c) use it instead of the
// system installer, which only knows CIAs. Nothing here touches installed titles.
#include <stdbool.h>

// True when this process was started by the Homebrew Launcher, not installed as a CIA.
bool HbInstall_IsHomebrew(void);

// The running .3dsx ("sdmc:/3ds/....3dsx"): the path the launcher passed in argv[0], or
// sdmc:/3ds/SuperMetroid3DSPort.3dsx when argv[0] is not a .3dsx on the SD card. After an install
// that renamed the file (below), the new one.
const char *HbInstall_Path(void);

// Moves the .3dsx at `path` (a newlib path on the SD card) over the running one. A running file
// named as the old releases' download ("sm-3ds-v<version>.3dsx") is replaced by "sm-3ds.3dsx" in
// the same folder and removed (#61); any other name is kept. The old file is
// kept as "<name>.bak" until the new one is in place, and put back if that fails. Refuses a file
// that is not a 3dsx. The running copy is in memory, so the new one is used from the next start.
// On failure `error` says why. Called from a worker thread (updater, FTP server): it first has the
// main thread close the romfs (HbInstall_MainTick), which keeps the running file open.
bool HbInstall_File(const char *path, char *error, unsigned errorCap);

// Main thread, once a frame: closes the romfs when an install asks for it. The romfs is the running
// .3dsx itself: while it is open the file cannot be renamed, and once replaced its offsets point
// into the new file. Afterwards romfs:/ reads fail and the callers fall back to the SD card.
void HbInstall_MainTick(void);

// The romfs was closed for an install (so the exit must not close it again).
bool HbInstall_RomfsClosed(void);

// Has the 3dsx start again when the process exits: tells Luma's loader (Rosalina's hb:ldr, what
// the Homebrew Launcher itself uses to open an app) to load HbInstall_Path() and sets the
// chainloader to the launcher's host title. False when the loader is not there (another
// launcher): quitting then goes back to it.
bool HbInstall_SetRelaunch(void);

// Whether HbInstall_SetRelaunch can work (hb:ldr answers), to word the prompts.
bool HbInstall_CanRelaunch(void);
