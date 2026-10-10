// Languages and their strings from files (docs/PLAN.md P4.14, docs/translations.md).
//
// A language is a UTF-8 text file `<code>.txt` in a lang folder: `[section]` lines, then
// `English = translation` lines; `#` starts a comment line. The languages offered are English and
// Japanese (built in: English is the code's own text, Japanese the game's Japanese text with
// English everywhere else) plus one per file found. The folders are read in order and a file
// replaces one of the same name found before: the port's own in romfs, then the SD card's.
#pragma once

#include <stdbool.h>

enum { kLangMaxCount = 24 };

typedef struct {
  char code[12];       // the file's name without .txt ("es"); "en" and "ja" for the built-in ones
  char name[40];       // the language in itself, [language] name (UTF-8)
  char path[192];      // the file, "" for the built-in ones
  bool japanese;       // the game's Japanese text (japanese_text_flag)
} LangInfo;

// The folders to look in, before the first call of anything else (default: romfs and the SD
// card's data folder on the console; SM_LANG_DIR or ./romfs/lang on the PC). Rescans.
void Lang_SetDirs(const char *const *dirs, int count);

int Lang_Count(void);
const LangInfo *Lang_Info(int i);      // English for an index out of range
int Lang_Find(const char *code);       // -1: none

// Reads language i's strings; the built-in ones have none. False if its file could not be read.
bool Lang_Load(int i);

// The loaded language's text for `key` (the English) in `section`, NULL when it has none.
const char *Lang_Get(const char *section, const char *key);

// Every entry of the loaded language, for the checks of tools/lang-check: false past the end.
bool Lang_Entry(int i, const char **section, const char **key, const char **value, int *line);
