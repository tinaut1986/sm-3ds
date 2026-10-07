#include <stdio.h>
#include <string.h>

#include "updater_parse.h"

static int sFailures;

#define CHECK(cond) do { if (!(cond)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); sFailures++; } } while (0)

/* Newest first, like the GitHub API: a beta, a stable, an older stable. */
static const char kJson[] =
    "[{\"tag_name\":\"v0.7.0\",\"prerelease\":true,\"assets\":["
    "{\"name\":\"a.zip\",\"browser_download_url\":\"http://h/a.zip\"},"
    "{\"name\":\"sm-3ds.cia\",\"browser_download_url\":\"http://h/beta.cia\"}]},"
    "{\"tag_name\":\"v0.6.5\",\"prerelease\":false,\"assets\":["
    "{\"browser_download_url\":\"http://h/stable.cia\"}]},"
    "{\"tag_name\":\"v0.6.4\",\"prerelease\": false,\"assets\":["
    "{\"browser_download_url\":\"http://h/old.cia\"}]}]";


/* ---- Updater_IsNewerBuild ---- */
static void TestNewerBuild(void) {
    /* a stable build behaves like IsNewer */
    CHECK(Updater_IsNewerBuild("v0.3.0", false, "v0.3.1", false));
    CHECK(!Updater_IsNewerBuild("v0.3.0", false, "v0.3.0", false));
    CHECK(!Updater_IsNewerBuild("v0.3.1", false, "v0.3.0", false));
    /* a beta is moved to the stable of its own version... */
    CHECK(Updater_IsNewerBuild("v0.3.1", true, "v0.3.1", false));
    /* ...but never to its own beta page */
    CHECK(!Updater_IsNewerBuild("v0.3.1", true, "v0.3.1", true));
    /* a newer beta page is newer for a beta (the channel filter is the caller's) */
    CHECK(Updater_IsNewerBuild("v0.3.1", true, "v0.3.2", true));
    CHECK(!Updater_IsNewerBuild("v0.3.2", true, "v0.3.1", false));
    /* dev builds follow IsNewer, beta or not */
    CHECK(!Updater_IsNewerBuild("v0.3.1-dev.2+ab", true, "v0.3.0", false));
    CHECK(Updater_IsNewerBuild("v0.3.1-dev.2+ab", true, "v0.3.1", false));
    CHECK(!Updater_IsNewerBuild("v0.3.1-dev.2+ab", true, "v0.3.1-dev.2+ab", false));
    CHECK(!Updater_IsNewerBuild("garbage", true, "v0.3.1", false));
    CHECK(!Updater_IsNewerBuild("v0.3.1", true, "", false));
}

/* ---- Updater_CollectNotes ---- */
static const char kNotesJson[] =
    "[{\"tag_name\":\"v0.4.0\",\"prerelease\":true,\"assets\":[],"
    "\"body\":\"> Beta\\r\\n\\r\\n<!-- sm-notes -->\\r\\n## What's new\\r\\n\\r\\n"
    "- Fixed **Ridley** door\\r\\n* `Spore Spawn` stalk\\r\\n- Caf\\u00e9 \\u2603 \\\"quoted\\\" a\\/b\\tc\\r\\n"
    "<!-- /sm-notes -->\\r\\n\\r\\n## Changelog\\r\\n- abc\"},"
    "{\"tag_name\":\"v0.3.1\",\"prerelease\":false,\"assets\":[],"
    "\"body\":\"<!-- sm-notes -->\\n- Stable one\\n- Stable two\\n<!-- /sm-notes -->\"},"
    "{\"tag_name\":\"v0.3.0\",\"prerelease\":false,\"assets\":[],"
    "\"body\":\"no block here\"},"
    "{\"tag_name\":\"v0.2.2\",\"prerelease\":false,\"assets\":[],\"body\":null}]";

static void TestCollectNotes(void) {
    char out[2048];
    int n;

    /* stable channel, running v0.3.0: only v0.3.1 */
    n = Updater_CollectNotes(kNotesJson, false, "v0.3.0", false, out, sizeof(out));
    CHECK(n == 1);
    CHECK(strcmp(out, "== v0.3.1 ==\n- Stable one\n- Stable two\n") == 0);

    /* beta channel: the beta first, with the escapes decoded and markdown flattened */
    n = Updater_CollectNotes(kNotesJson, true, "v0.3.0", false, out, sizeof(out));
    CHECK(n == 2);
    CHECK(strncmp(out, "== v0.4.0 ==\n- Fixed Ridley door\n- Spore Spawn stalk\n", 47) == 0);
    CHECK(strstr(out, "- Caf\xC3\xA9 ? \"quoted\" a/b c\n") != NULL);
    CHECK(strstr(out, "What's new") == NULL);
    CHECK(strstr(out, "Changelog") == NULL);
    CHECK(strstr(out, "== v0.3.1 ==\n- Stable one\n") != NULL);

    /* a very old install lists everything, the ones without a block say so */
    n = Updater_CollectNotes(kNotesJson, true, "v0.0.0", false, out, sizeof(out));
    CHECK(n == 4);
    CHECK(strstr(out, "== v0.3.0 ==\n- (no notes for this version)\n") != NULL);
    CHECK(strstr(out, "== v0.2.2 ==\n- (no notes for this version)\n") != NULL);

    /* up to date: nothing */
    n = Updater_CollectNotes(kNotesJson, true, "v0.4.0", true, out, sizeof(out));
    CHECK(n == 0 && out[0] == '\0');

    /* a beta v0.3.1 sees the stable v0.3.1 and not its own beta page */
    n = Updater_CollectNotes(
        "[{\"tag_name\":\"v0.3.1\",\"prerelease\":true,\"body\":\"x\"}]", true, "v0.3.1", true,
        out, sizeof(out));
    CHECK(n == 0);
    n = Updater_CollectNotes(kNotesJson, true, "v0.3.1", true, out, sizeof(out));
    CHECK(n == 2 && strncmp(out, "== v0.4.0 ==", 12) == 0);
    n = Updater_CollectNotes(kNotesJson, false, "v0.3.1", true, out, sizeof(out));
    CHECK(n == 1 && strncmp(out, "== v0.3.1 ==", 12) == 0);

    /* tiny buffers: cut at a line boundary, "..." at the end, always terminated */
    {
        char tiny[40];
        size_t len;

        n = Updater_CollectNotes(kNotesJson, true, "v0.0.0", false, tiny, sizeof(tiny));
        len = strlen(tiny);
        CHECK(len < sizeof(tiny));
        CHECK(len >= 4 && strcmp(tiny + len - 4, "...\n") == 0);
        CHECK(n >= 1);
        n = Updater_CollectNotes(kNotesJson, true, "v0.0.0", false, tiny, 4);
        CHECK(n == 0 && tiny[0] == '\0');
        n = Updater_CollectNotes(kNotesJson, true, "v0.0.0", false, tiny, 0);
        CHECK(n == 0);
    }

    /* truncated JSON: inside the block, inside an escape, before the body */
    n = Updater_CollectNotes(
        "[{\"tag_name\":\"v0.5.0\",\"body\":\"<!-- sm-notes -->\\n- Cut he", false, "v0.4.0", false,
        out, sizeof(out));
    CHECK(n == 1 && strcmp(out, "== v0.5.0 ==\n- Cut he\n") == 0);
    n = Updater_CollectNotes("[{\"tag_name\":\"v0.5.0\",\"body\":\"<!-- sm-notes -->\\n- A\\u00", false,
                             "v0.4.0", false, out, sizeof(out));
    CHECK(n == 1 && strncmp(out, "== v0.5.0 ==\n- A", 16) == 0);
    n = Updater_CollectNotes("[{\"tag_name\":\"v0.5.0\",\"prere", false, "v0.4.0", false, out,
                             sizeof(out));
    CHECK(n == 1 && strstr(out, "(no notes") != NULL);
    n = Updater_CollectNotes("[]", true, "v0.1.0", false, out, sizeof(out));
    CHECK(n == 0);
}

int main(void) {
    UpdaterRelease rel;

    CHECK(Updater_IsNewer("v0.6.4", "v0.6.5"));
    CHECK(!Updater_IsNewer("v0.6.5", "v0.6.5"));
    CHECK(!Updater_IsNewer("v0.6.5", "v0.6.4"));
    CHECK(Updater_IsNewer("v0.6.9", "v0.7.0"));
    CHECK(Updater_IsNewer("v0.6.4-dev.3+abc", "v0.6.4"));
    CHECK(!Updater_IsNewer("v0.6.4", "v0.6.4-dev.3+abc"));
    CHECK(Updater_IsNewer("v0.6.4-dev.3+abc", "v0.6.5"));
    CHECK(!Updater_IsNewer("garbage", "v0.6.5"));
    CHECK(!Updater_IsNewer("v0.6.4", ""));
    /* this port's own build versions (make print-version) */
    CHECK(Updater_IsNewer("v0.2.3-dev.9+aa50ff6", "v0.2.3"));
    CHECK(Updater_IsNewer("v0.2.2-dev.111+7ab9756", "v0.2.2"));
    CHECK(!Updater_IsNewer("v0.2.3", "v0.2.3-dev.9+aa50ff6"));

    CHECK(Updater_PickRelease(kJson, false, &rel));
    CHECK(strcmp(rel.tag, "v0.6.5") == 0);
    CHECK(strcmp(rel.cia_url, "http://h/stable.cia") == 0);
    CHECK(!rel.prerelease);

    CHECK(Updater_PickRelease(kJson, true, &rel));
    CHECK(strcmp(rel.tag, "v0.7.0") == 0);
    CHECK(strcmp(rel.cia_url, "http://h/beta.cia") == 0);
    CHECK(rel.prerelease);

    CHECK(!Updater_PickRelease("[]", true, &rel));
    CHECK(!Updater_PickRelease("{\"message\":\"rate limited\"}", true, &rel));
    /* Truncated mid-URL: the incomplete release must be skipped, not garbled. */
    CHECK(!Updater_PickRelease("[{\"tag_name\":\"v1.0.0\",\"assets\":[{\"browser_download_url\":\"http://h/x.ci", true, &rel));

    TestNewerBuild();
    TestCollectNotes();

    if (sFailures == 0) printf("updater_parse_test: OK\n");
    return sFailures ? 1 : 0;
}
