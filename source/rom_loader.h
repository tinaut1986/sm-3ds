// Locates the user's Super Metroid ROM on the SD card and verifies it.
//
// The ROM is never bundled with the app: it lives in DATA_DIR, which is also
// the working directory of the game (saves/, save states, debug dumps).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROM_DATA_DIR "sdmc:/3ds/Super Metroid 3DS"

typedef enum {
  ROM_OK = 0,
  ROM_NO_DIR,      // folder did not exist (it has been created)
  ROM_NO_FILES,    // no .smc/.sfc in the folder
  ROM_BAD_HASH,    // candidates found, none has the expected sha1
} RomStatus;

typedef struct {
  RomStatus status;
  char path[512];     // full path of the accepted ROM (ROM_OK)
  char name[256];     // file name of the accepted ROM, or of the first rejected one
  bool had_header;    // a 512-byte copier header was found (the emulator skips it)
  int rejected;       // number of candidates skipped for a wrong hash
  char sha1[41];      // sha1 (headerless) of the accepted/first rejected ROM
} RomInfo;

extern const char kRomExpectedSha1[41];

// Creates ROM_DATA_DIR if needed, scans it, fills `info`. Returns info->status.
RomStatus RomLoader_Find(RomInfo *info);

// Human-readable explanation of a failed status (for the error screen).
const char *RomLoader_StatusText(RomStatus status);

// Plain SHA-1, exposed for the host-side test.
void Sha1Hex(const uint8_t *data, size_t len, char out[41]);
