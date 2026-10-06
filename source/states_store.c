#include "states_store.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ID_MAX 999999

static void Path(char *out, size_t size, int id, const char *ext) { snprintf(out, size, "saves/save%d.%s", id, ext); }

bool States_ReadInfo(int id, StateInfo *si) {
  memset(si, 0, sizeof(*si));
  si->id = id;
  char path[48];
  Path(path, sizeof(path), id, "txt");
  FILE *f = fopen(path, "r");
  if (!f) return false;
  char line[80];
  while (fgets(line, sizeof(line), f)) {
    char key[24];
    long long v;
    if (sscanf(line, "version=%31s", si->version) == 1) continue;
    if (sscanf(line, "%23[^=]=%lld", key, &v) != 2) continue;
    if (!strcmp(key, "saved_at")) si->saved_at = v;
    else if (!strcmp(key, "area")) si->area = (unsigned)v;
    else if (!strcmp(key, "room")) si->room = (unsigned)v;
    else if (!strcmp(key, "health")) si->health = (unsigned)v;
    else if (!strcmp(key, "max_health")) si->max_health = (unsigned)v;
    else if (!strcmp(key, "reserve")) si->reserve = (unsigned)v;
    else if (!strcmp(key, "max_reserve")) si->max_reserve = (unsigned)v;
    else if (!strcmp(key, "missiles")) si->missiles = (unsigned)v;
    else if (!strcmp(key, "max_missiles")) si->max_missiles = (unsigned)v;
    else if (!strcmp(key, "supers")) si->supers = (unsigned)v;
    else if (!strcmp(key, "max_supers")) si->max_supers = (unsigned)v;
    else if (!strcmp(key, "pbs")) si->pbs = (unsigned)v;
    else if (!strcmp(key, "max_pbs")) si->max_pbs = (unsigned)v;
    else if (!strcmp(key, "hours")) si->hours = (unsigned)v;
    else if (!strcmp(key, "minutes")) si->minutes = (unsigned)v;
    else if (!strcmp(key, "mark") && v >= 0 && v < kStateMarks) si->mark = (int)v;
  }
  fclose(f);
  si->has_info = true;
  Path(path, sizeof(path), id, "img");
  struct stat st;
  si->has_shot = stat(path, &st) == 0;
  return true;
}

bool States_WriteInfo(const StateInfo *si) {
  char path[48];
  Path(path, sizeof(path), si->id, "txt");
  FILE *f = fopen(path, "w");
  if (!f) return false;
  fprintf(f, "saved_at=%lld\narea=%u\nroom=%u\nhealth=%u\nmax_health=%u\nreserve=%u\nmax_reserve=%u\nmissiles=%u\nmax_missiles=%u\n"
             "supers=%u\nmax_supers=%u\npbs=%u\nmax_pbs=%u\nhours=%u\nminutes=%u\nmark=%d\nversion=%s\n",
          si->saved_at, si->area, si->room, si->health, si->max_health, si->reserve, si->max_reserve, si->missiles, si->max_missiles,
          si->supers, si->max_supers, si->pbs, si->max_pbs, si->hours, si->minutes, si->mark,
          si->version[0] ? si->version : "-");
  fclose(f);
  return true;
}

static int CmpNewest(const void *a, const void *b) {
  const StateInfo *x = a, *y = b;
  if (x->saved_at != y->saved_at) return x->saved_at < y->saved_at ? 1 : -1;   // no date (an old state) sorts last
  return y->id - x->id;
}

int States_Scan(StateInfo *out, int max) {
  DIR *d = opendir("saves");
  if (!d) return 0;
  int n = 0, total = 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    int id;
    char tail[8];
    // saveN.sav only: the other files of a state are looked up by id.
    if (sscanf(e->d_name, "save%d.%7s", &id, tail) != 2 || strcmp(tail, "sav") || id < 0 || id > ID_MAX) continue;
    total++;
    if (n < max) States_ReadInfo(id, &out[n++]);
  }
  closedir(d);
  qsort(out, n, sizeof(out[0]), CmpNewest);
  return total;
}

int States_NextId(void) {
  DIR *d = opendir("saves");
  if (!d) return 0;
  int next = 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    int id;
    char tail[8];
    if (sscanf(e->d_name, "save%d.%7s", &id, tail) == 2 && id >= 0 && id <= ID_MAX && id >= next) next = id + 1;
  }
  closedir(d);
  return next > ID_MAX ? ID_MAX : next;
}

bool States_WriteShot(int id, const uint16_t *rgb565) {
  char path[48];
  Path(path, sizeof(path), id, "img");
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  const uint16_t head[3] = { 0x4D53 /* "SM" */, kStateShotW, kStateShotH };
  const bool ok = fwrite(head, sizeof(head), 1, f) == 1 &&
                  fwrite(rgb565, 2, kStateShotW * kStateShotH, f) == (size_t)(kStateShotW * kStateShotH);
  fclose(f);
  return ok;
}

bool States_ReadShot(int id, uint16_t *rgb565) {
  char path[48];
  Path(path, sizeof(path), id, "img");
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  uint16_t head[3];
  const bool ok = fread(head, sizeof(head), 1, f) == 1 && head[0] == 0x4D53 && head[1] == kStateShotW &&
                  head[2] == kStateShotH &&
                  fread(rgb565, 2, kStateShotW * kStateShotH, f) == (size_t)(kStateShotW * kStateShotH);
  fclose(f);
  return ok;
}

void States_Delete(int id) {
  static const char *const kExt[] = { "sav", "txt", "rap", "img" };
  char path[48];
  for (int i = 0; i < 4; i++) {
    Path(path, sizeof(path), id, kExt[i]);
    remove(path);
  }
}

void States_MakeShot(const uint32_t *top, uint16_t *out) {
  // The framebuffer is column-major with its origin at the bottom-left: pixel (x, y), y down
  // from the top, is top[x * 240 + (239 - y)].
  for (int y = 0; y < kStateShotH; y++) {
    for (int x = 0; x < kStateShotW; x++) {
      unsigned r = 0, g = 0, b = 0;
      for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
          const uint32_t c = top[(x * 2 + dx) * 240 + (239 - (y * 2 + dy))];
          r += c >> 24, g += c >> 16 & 0xFF, b += c >> 8 & 0xFF;
        }
      }
      out[y * kStateShotW + x] = (uint16_t)((r / 4 >> 3) << 11 | (g / 4 >> 2) << 5 | (b / 4 >> 3));
    }
  }
}
