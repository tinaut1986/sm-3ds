#include "lang_file.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __3DS__
static const char *const kDefaultDirs[] = { "romfs:/lang", "sdmc:/3ds/Super Metroid 3DS/lang" };
#elif defined(LANG_DIR)   // the host tools: the repository's romfs/lang, given by their build
static const char *const kDefaultDirs[] = { LANG_DIR };
#else
static const char *const kDefaultDirs[] = { "romfs/lang" };
#endif

enum { kMaxDirs = 4, kMaxFileBytes = 256 * 1024 };

static char g_dirs[kMaxDirs][160];
static int g_dir_count = -1;   // -1: the defaults, not set yet
static LangInfo g_langs[kLangMaxCount];
static int g_lang_count;
static bool g_scanned;

// The port's own files come first, in this order (the old config.ini indices); the rest by code.
static const char *const kOwnOrder[] = { "es", "ca", "fr", "pt" };

// ---- Parsing -------------------------------------------------------------------------------

typedef struct {
  const char *section, *key, *value;
  int line;
} Entry;

static char *g_text;            // the loaded file, cut into the entries' strings
static Entry *g_entries;
static int g_entry_count;

static char *Trim(char *s) {
  while (*s == ' ' || *s == '\t') s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
  return s;
}

static char *ReadFile(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = n >= 0 && n <= kMaxFileBytes ? malloc((size_t)n + 1) : NULL;
  if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  if (buf) buf[n] = 0;
  return buf;
}

// At most one entry per line.
static int MaxEntries(const char *text) {
  int n = 1;
  for (; *text; text++) n += *text == '\n';
  return n;
}

// Cuts `text` into entries in `out` (MaxEntries long; the strings point into `text`): how many.
static int Parse(char *text, Entry *out) {
  char *s = text;
  if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
  const char *section = "";
  int n = 0, line_no = 0;
  while (*s) {
    char *line = s;
    char *nl = strchr(s, '\n');
    if (nl) {
      *nl = 0;
      s = nl + 1;
    } else {
      s += strlen(s);
    }
    line_no++;
    line = Trim(line);
    if (!*line || *line == '#') continue;
    if (*line == '[') {
      char *end = strchr(line, ']');
      if (end) {
        *end = 0;
        section = Trim(line + 1);
      }
      continue;
    }
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    const char *key = Trim(line), *value = Trim(eq + 1);
    if (!*key) continue;
    out[n++] = (Entry){ section, key, value, line_no };
  }
  return n;
}

static int CompareEntries(const void *a, const void *b) {
  const Entry *x = a, *y = b;
  const int c = strcmp(x->section, y->section);
  if (c) return c;
  const int k = strcmp(x->key, y->key);
  return k ? k : x->line - y->line;   // a key given twice: the later one wins (Lang_Get takes the last)
}

static void Unload(void) {
  free(g_text);
  free(g_entries);
  g_text = NULL;
  g_entries = NULL;
  g_entry_count = 0;
}

// ---- The languages offered -------------------------------------------------------------------

static int OwnRank(const char *code) {
  for (int i = 0; i < (int)(sizeof(kOwnOrder) / sizeof(kOwnOrder[0])); i++)
    if (!strcmp(code, kOwnOrder[i])) return i;
  return 99;
}

static int CompareLangs(const void *a, const void *b) {
  const LangInfo *x = a, *y = b;
  const int rx = OwnRank(x->code), ry = OwnRank(y->code);
  return rx != ry ? rx - ry : strcmp(x->code, y->code);
}

// Reads `path` into g_text and g_entries, sorted. False if it cannot be read.
static bool LoadFile(const char *path) {
  Unload();
  g_text = ReadFile(path);
  if (!g_text) return false;
  g_entries = malloc(sizeof(Entry) * (size_t)MaxEntries(g_text));
  if (!g_entries) {
    Unload();
    return false;
  }
  g_entry_count = Parse(g_text, g_entries);
  qsort(g_entries, (size_t)g_entry_count, sizeof(Entry), CompareEntries);
  return true;
}

// The [language] name of a file, else its code in capitals.
static void ReadName(LangInfo *l) {
  snprintf(l->name, sizeof(l->name), "%s", l->code);
  for (char *p = l->name; *p; p++) *p = (char)toupper((unsigned char)*p);
  if (!LoadFile(l->path)) return;
  const char *name = Lang_Get("language", "name");
  if (name) snprintf(l->name, sizeof(l->name), "%s", name);
  Unload();
}

static void AddFile(const char *dir, const char *file) {
  const size_t len = strlen(file);
  if (len < 5 || len - 4 >= sizeof(g_langs[0].code) || strcmp(file + len - 4, ".txt")) return;
  if (strlen(dir) + 1 + len >= sizeof(g_langs[0].path)) return;
  char code[sizeof(g_langs[0].code)];
  for (size_t i = 0; i < len - 4; i++) code[i] = (char)tolower((unsigned char)file[i]);
  code[len - 4] = 0;
  if (!strcmp(code, "en") || !strcmp(code, "ja") || !strcmp(code, "readme")) return;   // built in, or the guide
  LangInfo *l = NULL;
  for (int i = 0; i < g_lang_count; i++)
    if (!strcmp(g_langs[i].code, code)) l = &g_langs[i];   // a later folder replaces the file
  if (!l) {
    if (g_lang_count >= kLangMaxCount - 1) return;          // one place kept for Japanese
    l = &g_langs[g_lang_count++];
  }
  memset(l, 0, sizeof(*l));
  snprintf(l->code, sizeof(l->code), "%s", code);
  snprintf(l->path, sizeof(l->path), "%.159s/%.15s", dir, file);   // fits: checked above
  ReadName(l);
}

static void Scan(void) {
  g_scanned = true;
  if (g_dir_count < 0) {
    const char *env = getenv("SM_LANG_DIR");
#ifndef __3DS__
    if (env && *env) {
      Lang_SetDirs(&env, 1);
      return;
    }
#endif
    (void)env;
    Lang_SetDirs(kDefaultDirs, (int)(sizeof(kDefaultDirs) / sizeof(kDefaultDirs[0])));
    return;
  }
  g_lang_count = 0;
  LangInfo *en = &g_langs[g_lang_count++];
  memset(en, 0, sizeof(*en));
  snprintf(en->code, sizeof(en->code), "en");
  snprintf(en->name, sizeof(en->name), "ENGLISH");
  for (int d = 0; d < g_dir_count; d++) {
    DIR *dir = opendir(g_dirs[d]);
    if (!dir) continue;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) AddFile(g_dirs[d], ent->d_name);
    closedir(dir);
  }
  qsort(g_langs + 1, (size_t)(g_lang_count - 1), sizeof(LangInfo), CompareLangs);
  LangInfo *ja = &g_langs[g_lang_count++];
  memset(ja, 0, sizeof(*ja));
  snprintf(ja->code, sizeof(ja->code), "ja");
  snprintf(ja->name, sizeof(ja->name), "JAPANESE");
  ja->japanese = true;
}

static void EnsureScanned(void) {
  if (!g_scanned) Scan();
}

void Lang_SetDirs(const char *const *dirs, int count) {
  g_dir_count = 0;
  for (int i = 0; i < count && g_dir_count < kMaxDirs; i++)
    snprintf(g_dirs[g_dir_count++], sizeof(g_dirs[0]), "%s", dirs[i]);
  Unload();
  Scan();
}

int Lang_Count(void) {
  EnsureScanned();
  return g_lang_count;
}

const LangInfo *Lang_Info(int i) {
  EnsureScanned();
  return &g_langs[i >= 0 && i < g_lang_count ? i : 0];
}

int Lang_Find(const char *code) {
  EnsureScanned();
  for (int i = 0; i < g_lang_count; i++)
    if (!strcmp(g_langs[i].code, code)) return i;
  return -1;
}

bool Lang_Load(int i) {
  EnsureScanned();
  Unload();
  const LangInfo *l = Lang_Info(i);
  return !l->path[0] || LoadFile(l->path);
}

const char *Lang_Get(const char *section, const char *key) {
  int lo = 0, hi = g_entry_count - 1, found = -1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    int c = strcmp(g_entries[mid].section, section);
    if (!c) c = strcmp(g_entries[mid].key, key);
    if (c <= 0) {
      if (!c) found = mid;   // keep going right: the last of equal keys
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return found >= 0 && *g_entries[found].value ? g_entries[found].value : NULL;
}

bool Lang_Entry(int i, const char **section, const char **key, const char **value, int *line) {
  if (i < 0 || i >= g_entry_count) return false;
  *section = g_entries[i].section;
  *key = g_entries[i].key;
  *value = g_entries[i].value;
  *line = g_entries[i].line;
  return true;
}
