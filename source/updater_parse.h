#ifndef UPDATER_PARSE_H
#define UPDATER_PARSE_H

/* Pure logic of the self-updater: version comparison and release-list
 * parsing. No 3DS headers, so it builds and is unit-tested on the host
 * (tools/updater-test). */

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char tag[32];       /* "v0.6.5" */
    char cia_url[384];  /* browser_download_url of the .cia asset */
    bool prerelease;
} UpdaterRelease;

/* Compares "vX.Y.Z[-dev...]" strings. Returns true when `remote` is a newer
 * build than `current`. A "-dev" build sits on the way to its own X.Y.Z, so
 * the plain X.Y.Z release counts as newer than it. Unparseable input -> false. */
bool Updater_IsNewer(const char* current, const char* remote);

/* Scans a GitHub `GET /repos/{owner}/{repo}/releases` JSON body (newest
 * first) and picks the first release that has a .cia asset and, unless
 * `allowBeta`, is not a prerelease. Tolerates a truncated body. */
bool Updater_PickRelease(const char* json, bool allowBeta, UpdaterRelease* out);

/* Like Updater_IsNewer, for a build whose channel is known. A beta build
 * ("v0.3.2 BETA", numbered like the stable it leads to) also counts the
 * stable release of its own version as newer, so a beta player is moved to
 * the stable build once it exists. A beta never sees its own pre-release page
 * as newer, or it would offer to install itself forever. `-dev` builds are
 * never betas: they follow Updater_IsNewer. */
bool Updater_IsNewerBuild(const char* current, bool currentIsBeta,
                          const char* remote, bool remotePrerelease);

/* Writes the "what's new" text of every release in a releases JSON body that
 * is on an allowed channel (`allowBeta`: pre-releases too) and newer than
 * `current` (as Updater_IsNewerBuild). Per release: a "== vX.Y.Z ==" line,
 * then the lines of its notes block (the text between the <!-- sm-notes -->
 * markers of the release body, markdown flattened to plain "- " lines), or
 * "- (no notes for this version)". Lines end with '\n'. When `out` fills it is
 * cut at a line boundary and "..." is appended. Always NUL-terminated.
 * Returns the number of releases listed. Tolerates a truncated body and
 * `"body": null`. */
int Updater_CollectNotes(const char* json, bool allowBeta, const char* current,
                         bool currentIsBeta, char* out, size_t outSize);

#endif
