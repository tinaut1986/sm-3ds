#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "updater.h"
#include "updater_parse.h"
#include "version.h"

extern void Main_RequestQuit(void);   // main.c

#define UPDATER_DEFAULT_URL "https://api.github.com/repos/tinaut1986/sm-3ds/releases?per_page=8"
#define UPDATER_DIR "update"   // in the data folder, which is the working directory
#define UPDATER_CIA_PATH UPDATER_DIR "/sm-update.cia"
#define UPDATER_NOTES_PATH UPDATER_DIR "/notes.txt"   /* the last notes, for when there is no network */
#define UPDATER_JSON_MAX (192 * 1024)
#define UPDATER_CHUNK (64 * 1024)
#define UPDATER_MAX_REDIRECTS 5
#define UPDATER_STACK (64 * 1024)
#define UPDATER_SOC_SIZE 0x100000 /* also bounds the TCP window */
#define UPDATER_FILE_BUF (256 * 1024)
#define UPDATER_MSG_MAX 64
#define UPDATER_NOTES_MAX 6144

typedef enum {
    JOB_CHECK,             /* manual: the result stays in the state; a newer build asks */
    JOB_AUTO_CHECK,        /* at boot: the same, silent when it fails */
    JOB_INSTALL
} UpdaterJob;

static volatile UpdState sState = UPD_IDLE;
static volatile int sProgress = 0;
static volatile bool sBusy = false;
static volatile bool sKeptCia = false; /* failed install left the CIA on the SD */
static volatile UpdPrompt sPrompt = UPD_PROMPT_NONE;
static bool sBeta = false;
static char sRemoteTag[32] = "";
static char sMessage[96] = "";
static char sNotes[UPDATER_NOTES_MAX] = "";   /* under sTextLock */
static UpdaterRelease sRelease;
static LightLock sTextLock;
static bool sLockReady = false;
static u32* sSocBuf = NULL;

static void EnsureLock(void) {
    if (!sLockReady) {
        LightLock_Init(&sTextLock);
        sLockReady = true;
    }
}

static void SetMessage(const char* fmt, const char* arg) {
    EnsureLock();
    LightLock_Lock(&sTextLock);
    snprintf(sMessage, sizeof(sMessage), fmt, arg ? arg : "");
    LightLock_Unlock(&sTextLock);
}

static void Fail(const char* msg, Result rc) {
    char buf[96];
    if (rc != 0) {
        snprintf(buf, sizeof(buf), "%s 0x%08lX", msg, (unsigned long)rc);
        SetMessage("%s", buf);
    } else {
        SetMessage("%s", msg);
    }
    sState = UPD_ERROR;
}

/* ------------------------------------------------------------------------- */
/* HTTP (libcurl + mbedtls: the console's own TLS cannot talk to GitHub)     */
/* ------------------------------------------------------------------------- */

typedef struct {
    /* Called once the final 200 response is known; total is the
     * Content-Length (0 when unknown). Return false to abort. */
    bool (*begin)(void* user, u32 total);
    bool (*data)(void* user, const u8* buf, u32 size);
    void* user;
} HttpSink;

typedef struct {
    const HttpSink* sink;
    CURL* curl;
    bool started;
    bool aborted;
    u32 got;
} CurlCtx;

static size_t CurlWrite(char* ptr, size_t size, size_t nmemb, void* userdata) {
    CurlCtx* c = (CurlCtx*)userdata;
    size_t n = size * nmemb;

    if (!c->started) {
        long status = 0;
        curl_off_t total = -1;

        curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &status);
        if (status != 200) return n; /* body of an error page: ignore */
        curl_easy_getinfo(c->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &total);
        c->started = true;
        if (c->sink->begin && !c->sink->begin(c->sink->user, total > 0 ? (u32)total : 0)) {
            c->aborted = true;
            return 0;
        }
    }
    if (!c->sink->data(c->sink->user, (const u8*)ptr, (u32)n)) {
        c->aborted = true;
        return 0;
    }
    c->got += (u32)n;
    return n;
}

static bool SocketsUp(void) {
    if (!sSocBuf) {
        sSocBuf = (u32*)memalign(0x1000, UPDATER_SOC_SIZE);
        if (!sSocBuf) return false;
        if (R_FAILED(socInit(sSocBuf, UPDATER_SOC_SIZE))) {
            free(sSocBuf);
            sSocBuf = NULL;
            return false;
        }
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    return true;
}

static void SocketsDown(void) {
    if (sSocBuf) {
        curl_global_cleanup();
        socExit();
        free(sSocBuf);
        sSocBuf = NULL;
    }
}

/* GET `url` (following redirects) and feed the body to `sink`. Returns 0 on
 * success, otherwise a negative code and sets the message. */
static int HttpGet(const char* url, const HttpSink* sink) {
    CurlCtx ctx;
    CURL* curl;
    CURLcode cc;
    long status = 0;

    if (!SocketsUp()) {
        Fail("SOCKETS", 0);
        return -1;
    }
    curl = curl_easy_init();
    if (!curl) {
        Fail("CURL INIT", 0);
        return -2;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.sink = sink;
    ctx.curl = curl;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "sm-3ds-updater/" APP_VERSION);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)UPDATER_CHUNK);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    cc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (ctx.aborted) return -3; /* the sink already set the message */
    if (cc != CURLE_OK) {
        char buf[UPDATER_MSG_MAX];
        snprintf(buf, sizeof(buf), "CURL %d %s", (int)cc, curl_easy_strerror(cc));
        SetMessage("%s", buf);
        sState = UPD_ERROR;
        return -4;
    }
    if (status != 200) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%ld", status);
        SetMessage("HTTP %s", buf);
        sState = UPD_ERROR;
        return -5;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Sinks                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    char* buf;
    u32 len;
} JsonSink;

static bool JsonData(void* user, const u8* data, u32 size) {
    JsonSink* s = (JsonSink*)user;
    u32 room = UPDATER_JSON_MAX - 1 - s->len;
    if (size > room) size = room; /* the release list we want is at the top */
    memcpy(s->buf + s->len, data, size);
    s->len += size;
    s->buf[s->len] = '\0';
    return true;
}

/* The update has to land where the running title is installed: a CIA for a
 * title that lives on NAND is refused when written to SD, and vice versa.
 * Falls back to SD (the usual place) when the query fails, e.g. under the
 * Homebrew Launcher. Also reports the running title's ID (0 if unknown). */
static FS_MediaType InstalledMediaType(u64* outPid) {
    u64 pid = 0;
    u8 media = MEDIATYPE_SD;
    bool registered = false, loaded = false;
    APT_AppletAttr attr;

    if (R_SUCCEEDED(APT_GetAppletInfo(APPID_APPLICATION, &pid, &media, &registered, &loaded, &attr)) &&
        (media == MEDIATYPE_SD || media == MEDIATYPE_NAND)) {
        if (outPid) *outPid = pid;
        return (FS_MediaType)media;
    }
    if (outPid) *outPid = 0;
    return MEDIATYPE_SD;
}

/* The CIA is downloaded to the SD card first and installed from there. The
 * installer rejects a CIA for the title that is running when the two are
 * streamed together, and a whole file on the SD is what lets a failed install
 * be finished by hand with FBI instead of leaving the player with nothing. */
typedef struct {
    FILE* file;
    u32 total;
    u32 written;
} FileSink;

static bool FileBegin(void* user, u32 total) {
    FileSink* s = (FileSink*)user;

    if (total == 0) {
        Fail("NO CONTENT-LENGTH", 0);
        return false;
    }
    s->total = total;
    s->file = fopen(UPDATER_CIA_PATH, "wb");
    if (!s->file) {
        Fail("CANNOT WRITE SD", 0);
        return false;
    }
    /* libcurl hands over at most 16KB per callback; without a big buffer each
     * one becomes its own tiny SD write, which is far slower than the large
     * sequential writes an FTP upload gets. */
    setvbuf(s->file, NULL, _IOFBF, UPDATER_FILE_BUF);
    return true;
}

static bool FileData(void* user, const u8* data, u32 size) {
    FileSink* s = (FileSink*)user;

    if (fwrite(data, 1, size, s->file) != size) {
        Fail("SD FULL?", 0);
        return false;
    }
    s->written += size;
    /* Download is the first 70% of the bar, the install the rest. */
    sProgress = (int)(((u64)s->written * 70) / s->total);
    return true;
}

/* Streams the downloaded file into the installer. `overwrite` picks the
 * install-over-existing-title variant. */
static bool InstallFromFile(FS_MediaType media, bool overwrite) {
    FILE* f = fopen(UPDATER_CIA_PATH, "rb");
    Handle cia;
    Result r;
    u8* buf;
    u32 total, offset = 0;
    char msg[UPDATER_MSG_MAX];

    if (!f) {
        Fail("CANNOT READ SD", 0);
        return false;
    }
    fseek(f, 0, SEEK_END);
    total = (u32)ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (u8*)malloc(UPDATER_CHUNK);
    if (!buf || total == 0) {
        free(buf);
        fclose(f);
        Fail("OUT OF MEMORY", 0);
        return false;
    }

    r = overwrite ? AM_StartCiaInstallOverwrite(&cia, media) : AM_StartCiaInstall(media, &cia);
    if (R_FAILED(r)) {
        snprintf(msg, sizeof(msg), "AM START %s", overwrite ? "OW" : "ST");
        free(buf);
        fclose(f);
        Fail(msg, r);
        return false;
    }

    while (offset < total) {
        size_t n = fread(buf, 1, UPDATER_CHUNK, f);
        u32 wrote = 0;

        if (n == 0) {
            r = -1;
        } else {
            r = FSFILE_Write(cia, &wrote, offset, buf, (u32)n, 0);
        }
        if (R_FAILED(r) || wrote != n) {
            snprintf(msg, sizeof(msg), "AM WRITE %s @%lu", overwrite ? "OW" : "ST", (unsigned long)offset);
            AM_CancelCIAInstall(cia);
            free(buf);
            fclose(f);
            Fail(msg, r);
            return false;
        }
        offset += (u32)n;
        sProgress = 70 + (int)(((u64)offset * 30) / total);
    }
    free(buf);
    fclose(f);

    r = AM_FinishCiaInstall(cia);
    if (R_FAILED(r)) {
        snprintf(msg, sizeof(msg), "AM FINISH %s", overwrite ? "OW" : "ST");
        Fail(msg, r);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* Worker                                                                    */
/* ------------------------------------------------------------------------- */

/* `update_url.txt` in the data folder (one line) points the check at another server, e.g.
 * tools/update-mock-server.py, so a release can be tried without publishing it. It is a file
 * of its own: config.ini is rewritten whole by the UI and would lose the line. */
static const char* ResolveUrl(void) {
    static char url[256];
    FILE* f = fopen("update_url.txt", "r");
    size_t n = 0;

    if (f) {
        if (fgets(url, sizeof(url), f)) n = strcspn(url, "\r\n");
        fclose(f);
    }
    if (n < 8) return UPDATER_DEFAULT_URL;
    url[n] = '\0';
    return url;
}

/* notes.txt: the remote tag on its own line, then the notes. Written after every successful
 * check and read at boot, so WHAT'S NEW has something to show with no network. */
static void SaveNotesCache(void) {
    FILE* f = fopen(UPDATER_NOTES_PATH, "wb");

    if (!f) return;
    EnsureLock();
    LightLock_Lock(&sTextLock);
    fprintf(f, "%s\n%s", sRemoteTag, sNotes);
    LightLock_Unlock(&sTextLock);
    fclose(f);
}

static void LoadNotesCache(void) {
    static char buf[UPDATER_NOTES_MAX + sizeof(sRemoteTag)];
    FILE* f = fopen(UPDATER_NOTES_PATH, "rb");
    size_t n;
    char* nl;

    if (!f) return;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    nl = strchr(buf, '\n');
    if (!nl || nl - buf >= (long)sizeof(sRemoteTag)) return;
    *nl = '\0';
    EnsureLock();
    LightLock_Lock(&sTextLock);
    snprintf(sRemoteTag, sizeof(sRemoteTag), "%s", buf);
    snprintf(sNotes, sizeof(sNotes), "%s", nl + 1);
    LightLock_Unlock(&sTextLock);
}

static bool DoCheck(void) {
    JsonSink js;
    HttpSink sink = { NULL, JsonData, &js };
    UpdaterRelease rel;

    js.buf = (char*)malloc(UPDATER_JSON_MAX);
    js.len = 0;
    if (!js.buf) {
        Fail("OUT OF MEMORY", 0);
        return false;
    }
    js.buf[0] = '\0';

    sState = UPD_CHECKING;
    SetMessage("%s", "");
    if (HttpGet(ResolveUrl(), &sink) != 0) {
        free(js.buf);
        return false;
    }
    if (!Updater_PickRelease(js.buf, sBeta, &rel)) {
        free(js.buf);
        Fail("NO RELEASE FOUND", 0);
        return false;
    }

    /* The notes come out of the same JSON, before it goes. Up to date: list the latest
     * published releases instead, so the viewer always has something to show. */
    {
        char* notes = (char*)malloc(UPDATER_NOTES_MAX);

        if (notes) {
            if (Updater_CollectNotes(js.buf, sBeta, APP_VERSION, APP_IS_BETA, notes, UPDATER_NOTES_MAX) == 0) {
                Updater_CollectNotes(js.buf, sBeta, "v0.0.0", false, notes, UPDATER_NOTES_MAX);
            }
            EnsureLock();
            LightLock_Lock(&sTextLock);
            memcpy(sNotes, notes, UPDATER_NOTES_MAX);
            LightLock_Unlock(&sTextLock);
            free(notes);
        }
    }
    free(js.buf);

    EnsureLock();
    LightLock_Lock(&sTextLock);
    snprintf(sRemoteTag, sizeof(sRemoteTag), "%s", rel.tag);
    LightLock_Unlock(&sTextLock);
    SaveNotesCache();

    if (!Updater_IsNewerBuild(APP_VERSION, APP_IS_BETA, rel.tag, rel.prerelease)) {
        sState = UPD_UP_TO_DATE;
        return false;
    }
    sRelease = rel;
    sState = UPD_AVAILABLE;
    return true;
}

static void DoInstall(void) {
    FileSink fs;
    HttpSink sink = { FileBegin, FileData, &fs };
    FS_MediaType media;
    u64 pid = 0;
    Result r;
    int rc;

    memset(&fs, 0, sizeof(fs));
    sProgress = 0;
    sKeptCia = false;
    sState = UPD_DOWNLOADING;
    SetMessage("%s", "");

    rc = HttpGet(sRelease.cia_url, &sink);
    if (fs.file) fclose(fs.file);
    if (rc != 0) {
        remove(UPDATER_CIA_PATH);
        if (sState != UPD_ERROR) sState = UPD_ERROR;
        return;
    }

    r = amInit();
    if (R_FAILED(r)) {
        Fail("AM INIT (am:net?)", r);
        return;
    }
    media = InstalledMediaType(&pid);

    if (!InstallFromFile(media, true)) {
        /* Replacing the running title in place was refused. Remove the old
         * install (this process keeps running from memory) and install the
         * downloaded CIA fresh. If even that fails the file stays on the SD
         * so FBI can finish the job. */
        sState = UPD_DOWNLOADING;
        if (pid == 0 || R_FAILED(AM_DeleteTitle(media, pid)) || !InstallFromFile(media, false)) {
            sKeptCia = true;
            sState = UPD_ERROR;
            amExit();
            return;
        }
    }
    amExit();
    remove(UPDATER_CIA_PATH);
    sProgress = 100;
    sState = UPD_INSTALLED;
    sPrompt = UPD_PROMPT_ASK_RESTART;
}

static void WorkerMain(void* arg) {
    UpdaterJob job = (UpdaterJob)(uintptr_t)arg;

    if (job == JOB_INSTALL) {
        DoInstall();
    } else if (DoCheck()) {
        sPrompt = UPD_PROMPT_ASK_INSTALL;   // both checks only ask, the install is the player's
    }
    /* Only an install the player is watching reports its failure in the
     * prompt; a failed silent check (no Wi-Fi) must not pop anything up. */
    if (sState == UPD_ERROR && sPrompt == UPD_PROMPT_PROGRESS) {
        sPrompt = UPD_PROMPT_ERROR;
    }
    SocketsDown();
    sBusy = false;
}

static void StartJob(UpdaterJob job) {
    if (sBusy) return;
    sBusy = true;
    if (!threadCreate(WorkerMain, (void*)(uintptr_t)job, UPDATER_STACK, 0x30, -2, true)) {
        sBusy = false;
        Fail("THREAD", 0);
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void Updater_Init(bool auto_check, bool beta) {
    EnsureLock();
    sBeta = beta;
    mkdir(UPDATER_DIR, 0777);
    LoadNotesCache();
    if (auto_check) StartJob(JOB_AUTO_CHECK);
}

void Updater_SetBeta(bool beta) { sBeta = beta; }

void Updater_CheckNow(void) {
    if (sPrompt == UPD_PROMPT_NONE) StartJob(JOB_CHECK);
}

static void Install(void) {
    if (sState == UPD_AVAILABLE) StartJob(JOB_INSTALL);
}

static void Restart(void) {
    aptSetChainloaderToSelf();
    Main_RequestQuit();
}

UpdState Updater_State(void) { return sState; }
UpdPrompt Updater_Prompt(void) { return sPrompt; }
bool Updater_KeptCia(void) { return sKeptCia; }

void Updater_AnswerPrompt(bool yes) {
    switch (sPrompt) {
        case UPD_PROMPT_ASK_INSTALL:
            if (yes) {
                sPrompt = UPD_PROMPT_PROGRESS;
                Install();
            } else {
                sPrompt = UPD_PROMPT_NONE;
            }
            break;
        case UPD_PROMPT_ASK_RESTART:
            if (yes) {
                Restart(); /* the prompt goes away with the process */
            } else {
                sPrompt = UPD_PROMPT_NONE;
            }
            break;
        case UPD_PROMPT_ERROR:
            sPrompt = UPD_PROMPT_NONE;
            break;
        default:
            break;
    }
}

int Updater_Progress(void) { return sProgress; }
const char *Updater_RemoteTag(void) { return sRemoteTag; }
const char *Updater_Message(void) { return sMessage; }

size_t Updater_CopyNotes(char *out, size_t cap) {
    size_t n;

    if (cap == 0) return 0;
    EnsureLock();
    LightLock_Lock(&sTextLock);
    n = strlen(sNotes);
    if (n > cap - 1) n = cap - 1;
    memcpy(out, sNotes, n);
    out[n] = '\0';
    LightLock_Unlock(&sTextLock);
    return n;
}

uint32_t Updater_Version(void) {
    return ((uint32_t)sState << 24) | ((uint32_t)sPrompt << 16) | (uint32_t)sProgress;
}
