// Host test of the save-state store (source/states_store.c): the list, the ids, the info file,
// the screenshot and the removal of a state's files, in a scratch folder. No ROM needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "states_store.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static void Touch(const char *path) { FILE *f = fopen(path, "wb"); if (f) fclose(f); }
static bool Exists(const char *path) { struct stat st; return stat(path, &st) == 0; }

int main(int argc, char **argv) {
  if (argc < 2 || chdir(argv[1])) return 2;
  mkdir("saves", 0777);
  static StateInfo list[8];
  CHECK(States_Scan(list, 8) == 0);
  CHECK(States_NextId() == 0);

  // States 0, 3 and 12345 with dates out of id order; one without a .txt (an old state).
  const int ids[] = { 0, 3, 12345, 7 };
  for (int k = 0; k < 4; k++) {
    char p[48];
    snprintf(p, sizeof(p), "saves/save%d.sav", ids[k]);
    Touch(p);
    if (ids[k] == 7) continue;
    StateInfo si = { .id = ids[k], .has_info = true, .saved_at = 1000 + (ids[k] == 3 ? 500 : ids[k]), .area = 2, .room = 0x1A,
                     .health = 99, .max_health = 299, .reserve = 40, .max_reserve = 100, .missiles = 5, .max_missiles = 10, .hours = 1, .minutes = 2, .mark = ids[k] % 8 };
    snprintf(si.version, sizeof(si.version), "v0.2.3-dev.1+abc");
    CHECK(States_WriteInfo(&si));
  }
  CHECK(States_NextId() == 12346);
  CHECK(States_Scan(list, 8) == 4);
  CHECK(list[0].id == 12345 && list[1].id == 3 && list[2].id == 0 && list[3].id == 7);   // newest first, undated last
  CHECK(list[1].has_info && list[1].health == 99 && list[1].max_reserve == 100 && list[1].mark == 3 && !strcmp(list[1].version, "v0.2.3-dev.1+abc"));
  CHECK(!list[3].has_info);
  CHECK(States_Scan(list, 2) == 4);   // the total, with the list cut

  // The screenshot round trip and its size check.
  static uint16_t shot[kStateShotW * kStateShotH], back[kStateShotW * kStateShotH];
  for (int i = 0; i < kStateShotW * kStateShotH; i++) shot[i] = (uint16_t)(i * 7);
  CHECK(States_WriteShot(3, shot));
  CHECK(States_ReadShot(3, back) && !memcmp(shot, back, sizeof(shot)));
  CHECK(!States_ReadShot(0, back));
  States_ReadInfo(3, &list[0]);
  CHECK(list[0].has_shot);

  // Shrinking: a white screen stays white, and the corner pixel is the bottom-left of the framebuffer's layout.
  static uint32_t top[400 * 240];
  for (int i = 0; i < 400 * 240; i++) top[i] = 0xFFFFFFFFu;
  top[0] = 0xFF0000FFu;   // x 0, y 239 (the bottom-left): red
  States_MakeShot(top, back);
  CHECK(back[0] == 0xFFFF);
  CHECK(back[(kStateShotH - 1) * kStateShotW] != 0xFFFF);

  // Removing a state takes every file of it, and only its.
  Touch("saves/save3.rap");
  States_Delete(3);
  CHECK(!Exists("saves/save3.sav") && !Exists("saves/save3.txt") && !Exists("saves/save3.rap") && !Exists("saves/save3.img"));
  CHECK(Exists("saves/save0.sav"));
  CHECK(States_Scan(list, 8) == 3);

  if (!g_fail) printf("states_test: OK\n");
  return g_fail != 0;
}
