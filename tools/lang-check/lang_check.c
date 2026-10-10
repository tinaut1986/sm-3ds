// The language files' tool (docs/translations.md, docs/PLAN.md P4.14).
//
//   lang_check template OUT.txt   every key the port looks up, with empty values and their notes:
//                                 the starting point of a new language
//   lang_check FILE.txt...        checks language files against those keys
//
// A check prints `error:` (the file would show something wrong: a missing [language] name, a %d or
// %s that differs from the English, a value over its length) or `warning:` (a key the port never
// looks up, most likely a typo; a character the fonts may not draw), then a summary line per
// file: `FILE: N translated, M left in English, E errors, W warnings`. Exit status 1 if any error.
// The room on screen is not checked here: see the file in tools/ui-preview and the game-text
// captures (docs/translations.md).
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_text.h"
#include "lang_file.h"
#include "ui_font.h"
#include "ui_lang.h"

typedef struct {
  char section[24];
  char key[256];
  char note[96];
  bool seen;
} Key;

enum { kMaxKeys = 1024 };
static Key g_keys[kMaxKeys];
static int g_key_count;

static void AddKey(const char *section, const char *key, const char *note) {
  for (int i = 0; i < g_key_count; i++)
    if (!strcmp(g_keys[i].section, section) && !strcmp(g_keys[i].key, key)) return;
  if (g_key_count >= kMaxKeys) return;
  Key *k = &g_keys[g_key_count++];
  snprintf(k->section, sizeof(k->section), "%s", section);
  snprintf(k->key, sizeof(k->key), "%s", key);
  snprintf(k->note, sizeof(k->note), "%s", note ? note : "");
}

static Key *FindKey(const char *section, const char *key) {
  for (int i = 0; i < g_key_count; i++)
    if (!strcmp(g_keys[i].section, section) && !strcmp(g_keys[i].key, key)) return &g_keys[i];
  return NULL;
}

static void CollectKeys(void) {
  UiLang_ForEachKey(AddKey);
  GameText_ForEachKey(AddKey);
  GameTextScreens_ForEachKey(AddKey);
}

// ---- The template ------------------------------------------------------------------------------

static int WriteTemplate(const char *path) {
  FILE *f = fopen(path, "w");
  if (!f) {
    fprintf(stderr, "cannot write %s\n", path);
    return 1;
  }
  fprintf(f, "# Super Metroid 3DS: a new language. Written by tools/lang-check (do not edit: regenerate it).\n"
             "# Copy it to <code>.txt (the language's code: de.txt, it.txt...), fill in the values and put it in\n"
             "# the SD card's /3ds/Super Metroid 3DS/lang/. A value left empty keeps the English. How to write and\n"
             "# check one: docs/translations.md.\n");
  const char *section = "";
  for (int i = 0; i < g_key_count; i++) {
    const Key *k = &g_keys[i];
    if (strcmp(k->section, section)) {
      section = k->section;
      fprintf(f, "\n[%s]\n", section);
    }
    if (k->note[0]) fprintf(f, "# %s\n", k->note);
    fprintf(f, "%s =\n", k->key);
  }
  fclose(f);
  return 0;
}

// ---- Checks ------------------------------------------------------------------------------------

static int g_errors, g_warnings;
static const char *g_file;

static void Report(bool error, int line, const char *fmt, const char *a, const char *b) {
  printf("%s:%d: %s: ", g_file, line, error ? "error" : "warning");
  printf(fmt, a, b);
  printf("\n");
  if (error) g_errors++;
  else g_warnings++;
}

static unsigned NextCode(const char **s) {
  const unsigned char *p = (const unsigned char *)*s;
  if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
    *s += 2;
    return (p[0] & 0x1F) << 6 | (p[1] & 0x3F);
  }
  if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
    *s += 3;
    return (p[0] & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F);
  }
  *s += 1;
  return p[0];
}

static int Utf8Len(const char *s) {
  int n = 0;
  while (*s) NextCode(&s), n++;
  return n;
}

// The %-conversions of a format string, in order ("%d%s").
static void Conversions(const char *s, char *out, size_t size) {
  size_t n = 0;
  for (; *s && n + 2 < size; s++)
    if (*s == '%' && s[1]) {
      out[n++] = '%';
      out[n++] = *++s;
    }
  out[n] = 0;
}

// Letters the game's capitals can show (game_text_screens.c, game_text.c): A-Z in either case,
// digits, the accented vowels, Ñ and Ç (as marks), and common punctuation.
static bool GameCapital(unsigned c) {
  if (c == ' ' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
  if (strchr(".,!?'-():+#/&", (int)c) && c) return true;
  if (c == 0xBF || c == 0xA1) return true;   // ¿ ¡
  if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 0x20;
  static const unsigned kMarked[] = { 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD,
                                      0xCE, 0xCF, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD9, 0xDA, 0xDB, 0xDC };
  for (size_t i = 0; i < sizeof(kMarked) / sizeof(kMarked[0]); i++)
    if (kMarked[i] == c) return true;
  return false;
}

static bool UiChar(unsigned c) { return c == ' ' || (c <= 0xFF && UiFont_Glyph((unsigned char)c)); }

static bool UiSection(const char *s) {
  return !strcmp(s, "ui") || !strcmp(s, "items") || !strcmp(s, "beams") || !strcmp(s, "ammo") ||
         !strcmp(s, "areas") || !strcmp(s, "areas short") || !strcmp(s, "language");
}

static void CheckValue(const Key *k, const char *value, int line) {
  // A length limit in the note ("11 characters at most", "3 letters at most").
  int max = 0;
  const char *at = strstr(k->note, " at most");
  if (at) {
    const char *p = k->note;
    while (*p && (*p < '0' || *p > '9')) p++;
    max = atoi(p);
  }
  if (max > 0 && Utf8Len(value) > max) {
    char m[16];
    snprintf(m, sizeof(m), "%d", max);
    Report(true, line, "\"%s\" is longer than %s characters", value, m);
  }
  if (!strcmp(k->section, "ui")) {
    char a[32], b[32];
    Conversions(k->key, a, sizeof(a));
    Conversions(value, b, sizeof(b));
    if (strcmp(a, b)) Report(true, line, "\"%s\": the English has the conversions \"%s\"", value, a);
  }
  if (!strcmp(k->section, "message boxes")) {
    int bars = 0;
    for (const char *p = value; *p; p++) bars += *p == '|';
    if (bars > 2) Report(true, line, "\"%s\": a box has three rows at most%s", value, "");
  }
  const bool ui = UiSection(k->section);
  const bool lowercase_line = strstr(k->key, "line)") != NULL;   // "(line)", "(modern line)"
  for (const char *p = value; *p;) {
    const char *start = p;
    const unsigned c = NextCode(&p);
    bool ok = ui ? UiChar(c) : lowercase_line ? (GameCapital(c) || c == '{' || c == '}') : GameCapital(c) || c == '|';
    if (!ok) {
      char ch[8];
      snprintf(ch, sizeof(ch), "%.*s", (int)(p - start), start);
      Report(false, line, "\"%s\": the fonts may not draw \"%s\"", value, ch);
    }
  }
}

static int CheckFile(const char *path) {
  g_file = path;
  g_errors = g_warnings = 0;
  for (int i = 0; i < g_key_count; i++) g_keys[i].seen = false;
  // Read it as the console does: its folder, the language with its name.
  char dir[256], code[32];
  const char *slash = strrchr(path, '/');
  snprintf(dir, sizeof(dir), "%.*s", slash ? (int)(slash - path) : 1, slash ? path : ".");
  snprintf(code, sizeof(code), "%s", slash ? slash + 1 : path);
  char *dot = strrchr(code, '.');
  if (!dot || strcmp(dot, ".txt")) {
    printf("%s: error: not a .txt file\n", path);
    return 1;
  }
  *dot = 0;
  const char *dirs[1] = { dir };
  Lang_SetDirs(dirs, 1);
  const int lang = Lang_Find(code);
  if (lang < 0 || !Lang_Load(lang)) {
    printf("%s: error: cannot be read as a language (its name is its code: de.txt; en and ja are taken)\n", path);
    return 1;
  }
  int translated = 0;
  bool named = false;
  const char *section, *key, *value;
  int line;
  for (int i = 0; Lang_Entry(i, &section, &key, &value, &line); i++) {
    Key *k = FindKey(section, key);
    if (!k) {
      Report(false, line, "[%s] \"%s\" is not a key the port looks up (a typo?)", section, key);
      continue;
    }
    if (!*value) continue;
    if (!strcmp(section, "language")) named = true;
    else if (!k->seen) translated++;
    k->seen = true;
    CheckValue(k, value, line);
  }
  if (!named) Report(true, 0, "no [language] name%s%s", "", "");
  int left = 0;
  for (int i = 0; i < g_key_count; i++) left += !g_keys[i].seen && strcmp(g_keys[i].section, "language");
  printf("%s: %d translated, %d left in English, %d errors, %d warnings\n", path, translated, left, g_errors, g_warnings);
  return g_errors ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: lang_check template OUT.txt | lang_check FILE.txt...\n");
    return 2;
  }
  CollectKeys();
  if (!strcmp(argv[1], "template")) return argc == 3 ? WriteTemplate(argv[2]) : 2;
  int status = 0;
  for (int i = 1; i < argc; i++) status |= CheckFile(argv[i]);
  return status;
}
