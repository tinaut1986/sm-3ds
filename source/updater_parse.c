#include "updater_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool ParseVersion(const char* s, long out[3], bool* isDev) {
    char* end;
    int i;

    if (*s == 'v' || *s == 'V') s++;
    for (i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') return false;
        out[i] = strtol(s, &end, 10);
        s = end;
        if (i < 2) {
            if (*s != '.') return false;
            s++;
        }
    }
    *isDev = (strncmp(s, "-dev", 4) == 0);
    return true;
}

bool Updater_IsNewer(const char* current, const char* remote) {
    long cur[3], rem[3];
    bool curDev, remDev;
    int i;

    if (!ParseVersion(current, cur, &curDev) || !ParseVersion(remote, rem, &remDev)) {
        return false;
    }
    for (i = 0; i < 3; i++) {
        if (rem[i] != cur[i]) return rem[i] > cur[i];
    }
    return curDev && !remDev;
}

bool Updater_IsNewerBuild(const char* current, bool currentIsBeta,
                          const char* remote, bool remotePrerelease) {
    long cur[3], rem[3];
    bool curDev, remDev;

    if (Updater_IsNewer(current, remote)) return true;
    if (!currentIsBeta || remotePrerelease) return false;
    if (!ParseVersion(current, cur, &curDev) || !ParseVersion(remote, rem, &remDev)) return false;
    if (curDev || remDev) return false;
    return cur[0] == rem[0] && cur[1] == rem[1] && cur[2] == rem[2];
}

/* Copies the JSON string value that follows `"key":` inside [seg, segEnd). */
static bool FindString(const char* seg, const char* segEnd, const char* key,
                       char* out, size_t outSize) {
    char pat[40];
    const char* p;
    size_t n = 0;

    strcpy(pat, "\"");
    strncat(pat, key, sizeof(pat) - 4);
    strcat(pat, "\"");
    p = strstr(seg, pat);
    if (!p || p >= segEnd) return false;
    p += strlen(pat);
    while (p < segEnd && (*p == ' ' || *p == ':')) p++;
    if (p >= segEnd || *p != '"') return false;
    p++;
    while (p < segEnd && *p != '"' && n + 1 < outSize) {
        out[n++] = *p++;
    }
    if (p >= segEnd || *p != '"') return false; /* truncated or too long */
    out[n] = '\0';
    return true;
}

static bool FindCiaUrl(const char* seg, const char* segEnd, char* out, size_t outSize) {
    static const char kKey[] = "\"browser_download_url\"";
    const char* p = seg;

    while ((p = strstr(p, kKey)) != NULL && p < segEnd) {
        char url[384];
        size_t len;

        if (FindString(p, segEnd, "browser_download_url", url, sizeof(url))) {
            len = strlen(url);
            if (len > 4 && strcmp(url + len - 4, ".cia") == 0) {
                strncpy(out, url, outSize - 1);
                out[outSize - 1] = '\0';
                return true;
            }
        }
        p += sizeof(kKey) - 1;
    }
    return false;
}

bool Updater_PickRelease(const char* json, bool allowBeta, UpdaterRelease* out) {
    static const char kTag[] = "\"tag_name\"";
    const char* p = json;

    while ((p = strstr(p, kTag)) != NULL) {
        const char* next = strstr(p + sizeof(kTag) - 1, kTag);
        const char* segEnd = next ? next : p + strlen(p);
        UpdaterRelease rel;
        char flag[8];

        memset(&rel, 0, sizeof(rel));
        p += sizeof(kTag) - 1;
        if (FindString(p - (sizeof(kTag) - 1), segEnd, "tag_name", rel.tag, sizeof(rel.tag))) {
            const char* pre = strstr(p, "\"prerelease\"");
            rel.prerelease = false;
            if (pre && pre < segEnd) {
                pre += sizeof("\"prerelease\"") - 1;
                while (pre < segEnd && (*pre == ' ' || *pre == ':')) pre++;
                strncpy(flag, pre, 4);
                flag[4] = '\0';
                rel.prerelease = (strcmp(flag, "true") == 0);
            }
            if ((allowBeta || !rel.prerelease) &&
                FindCiaUrl(p, segEnd, rel.cia_url, sizeof(rel.cia_url))) {
                *out = rel;
                return true;
            }
        }
        p = segEnd;
        if (!next) break;
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* Release notes                                                             */
/* ------------------------------------------------------------------------- */

#define NOTES_RAW_MAX 3072
#define NOTES_LINE_MAX 200
#define NOTES_RESERVE 5 /* "...\n" and the NUL always fit */

static const char kNotesOpen[] = "<!-- sm-notes -->";
static const char kNotesClose[] = "<!-- /sm-notes -->";

/* Unescapes the JSON string text in [p, end) into out (NUL-terminated).
 * \uXXXX becomes UTF-8 below 0x800 and '?' above; \r is dropped, \t is a space. */
static size_t Unescape(const char* p, const char* end, char* out, size_t outSize) {
    size_t n = 0;

    while (p < end && n + 3 < outSize) {
        char c = *p++;

        if (c != '\\') {
            out[n++] = c;
            continue;
        }
        if (p >= end) break;
        c = *p++;
        switch (c) {
            case 'n': out[n++] = '\n'; break;
            case 't': out[n++] = ' '; break;
            case 'r': break;
            case 'u': {
                unsigned long cp = 0;
                int i;

                if (end - p < 4) {
                    p = end;
                    break;
                }
                for (i = 0; i < 4; i++) {
                    char h = p[i];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned long)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned long)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned long)(h - 'A' + 10);
                }
                p += 4;
                if (cp < 0x80) {
                    out[n++] = (char)cp;
                } else if (cp < 0x800) {
                    out[n++] = (char)(0xC0 | (cp >> 6));
                    out[n++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    out[n++] = '?';
                }
                break;
            }
            default: out[n++] = c; break; /* \" \\ \/ */
        }
    }
    out[n] = '\0';
    return n;
}

/* Finds the end of the JSON string that starts at p (just after its opening
 * quote): the first unescaped quote, or segEnd when the text is truncated. */
static const char* StringEnd(const char* p, const char* segEnd) {
    while (p < segEnd && *p != '"') {
        if (*p == '\\' && p + 1 < segEnd) p++;
        p++;
    }
    return p;
}

/* Appends `line` and a newline. When it does not fit, appends "..." instead
 * and returns false: the caller stops. */
static bool AddLine(char* out, size_t cap, size_t* len, const char* line) {
    size_t n = strlen(line);

    if (*len + n + 1 + NOTES_RESERVE > cap) {
        memcpy(out + *len, "...\n", 5);
        *len += 4;
        return false;
    }
    memcpy(out + *len, line, n);
    out[*len + n] = '\n';
    out[*len + n + 1] = '\0';
    *len += n + 1;
    return true;
}

/* Flattens one markdown line to plain text in `line`. Returns false for a
 * line to skip (empty, or a heading). */
static bool FlattenLine(const char* s, const char* e, char* line, size_t lineSize) {
    size_t n = 0;

    while (s < e && *s == ' ') s++;
    if (s >= e || *s == '#') return false;
    if (e - s >= 2 && (*s == '*' || *s == '-') && s[1] == ' ') {
        line[n++] = '-';
        line[n++] = ' ';
        s += 2;
    }
    while (s < e && n + 1 < lineSize) {
        if (*s == '`') {
            s++;
        } else if (*s == '*' && s + 1 < e && s[1] == '*') {
            s += 2;
        } else {
            line[n++] = *s++;
        }
    }
    while (n > 0 && line[n - 1] == ' ') n--;
    line[n] = '\0';
    return n > 0;
}

/* Adds the notes block of the release body in [seg, segEnd). */
static bool AddNotes(const char* seg, const char* segEnd, char* out, size_t cap, size_t* len) {
    static char raw[NOTES_RAW_MAX];
    const char* b = strstr(seg, "\"body\"");
    const char *s, *e, *open, *close;
    size_t rawLen, i, lineStart = 0;
    bool any = false;
    char line[NOTES_LINE_MAX];

    if (b && b < segEnd) {
        b += 6;
        while (b < segEnd && (*b == ' ' || *b == ':')) b++;
        if (b < segEnd && *b == '"') {
            s = b + 1;
            e = StringEnd(s, segEnd);
            open = strstr(s, kNotesOpen);
            if (open && open < e) {
                open += sizeof(kNotesOpen) - 1;
                close = strstr(open, kNotesClose);
                if (!close || close > e) close = e; /* truncated before the end marker */
                rawLen = Unescape(open, close, raw, sizeof(raw));
                for (i = 0; i <= rawLen; i++) {
                    if (raw[i] != '\n' && raw[i] != '\0') continue;
                    if (FlattenLine(raw + lineStart, raw + i, line, sizeof(line))) {
                        any = true;
                        if (!AddLine(out, cap, len, line)) return false;
                    }
                    lineStart = i + 1;
                }
            }
        }
    }
    if (!any) return AddLine(out, cap, len, "- (no notes for this version)");
    return true;
}

int Updater_CollectNotes(const char* json, bool allowBeta, const char* current,
                         bool currentIsBeta, char* out, size_t outSize) {
    static const char kTag[] = "\"tag_name\"";
    const char* p = json;
    size_t len = 0;
    int count = 0;

    if (outSize == 0) return 0;
    out[0] = '\0';
    if (outSize < NOTES_RESERVE + 8) return 0;

    while ((p = strstr(p, kTag)) != NULL) {
        const char* next = strstr(p + sizeof(kTag) - 1, kTag);
        const char* segEnd = next ? next : p + strlen(p);
        char tag[32];
        const char* pre;
        bool prerelease = false;

        if (FindString(p, segEnd, "tag_name", tag, sizeof(tag))) {
            pre = strstr(p, "\"prerelease\"");
            if (pre && pre < segEnd) {
                pre += sizeof("\"prerelease\"") - 1;
                while (pre < segEnd && (*pre == ' ' || *pre == ':')) pre++;
                prerelease = (strncmp(pre, "true", 4) == 0);
            }
            if ((allowBeta || !prerelease) &&
                Updater_IsNewerBuild(current, currentIsBeta, tag, prerelease)) {
                char head[48];

                snprintf(head, sizeof(head), "== %s ==", tag);
                if (!AddLine(out, outSize, &len, head)) return count;
                count++;
                if (!AddNotes(p, segEnd, out, outSize, &len)) return count;
            }
        }
        p = segEnd;
        if (!next) break;
    }
    return count;
}
