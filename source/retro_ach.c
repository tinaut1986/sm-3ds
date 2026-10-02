#include "retro_ach.h"

#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "rc_client.h"
#include "src/types.h"
#include "src/sm_rtl.h"
#include "version.h"

// The RA hash of a SNES game is the MD5 of the headerless ROM. The port only runs the
// Super Metroid (Japan, USA) ROM whose sha1 is kRomExpectedSha1 (rom_loader.c), so this
// is that ROM's MD5 and not worth computing on the console.
static const char kRomMd5[] = "21f3e98df4780ee1c667b84e57d88675";

#define RA_INI "retroachievements.ini"
#define RA_LOG "debug/retroachievements.log"

enum { kMaxPending = 8, kRequestMax = 1024, kResponseMax = 65536, kMaxAchievements = 256, kToastMs = 3000 };

static rc_client_t *g_client;
static bool g_enabled;
static char g_user[64], g_token[64];
static RaStatus g_status = kRaOff;
static char g_message[96];
static bool g_cheats_used;
static uint32_t g_version;

static RaAchievement g_list[kMaxAchievements];
static int g_count;
static RaAchievement g_toast;
static u64 g_toast_until;

static void Changed(void) { g_version++; }

static void LogLine(const char *fmt, ...) {
  mkdir("debug", 0777);
  FILE *f = fopen(RA_LOG, "a");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

static void SetMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_message, sizeof(g_message), fmt, ap);
  va_end(ap);
  Changed();
}

// ---- Settings file ------------------------------------------------------------------

static void SaveIni(void) {
  FILE *f = fopen(RA_INI, "w");
  if (!f) return;
  // The token logs in as the player: keep this file to yourself.
  fprintf(f, "# RetroAchievements login (written by the bottom screen)\nenabled=%d\nuser=%s\ntoken=%s\n", g_enabled,
          g_user, g_token);
  fclose(f);
}

static void LoadIni(void) {
  FILE *f = fopen(RA_INI, "r");
  if (!f) return;
  char line[160];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    char *eq = strchr(line, '=');
    if (!eq || line[0] == '#') continue;
    *eq = 0;
    const char *v = eq + 1;
    if (!strcmp(line, "enabled")) g_enabled = atoi(v) != 0;
    else if (!strcmp(line, "user")) snprintf(g_user, sizeof(g_user), "%s", v);
    else if (!strcmp(line, "token")) snprintf(g_token, sizeof(g_token), "%s", v);
  }
  fclose(f);
}

// ---- SNES memory ----------------------------------------------------------------------
// rcheevos' SNES map: 0x000000-0x01FFFF is WRAM ($7E:0000-$7F:FFFF), cartridge RAM from
// 0x020000 (Super Metroid's SRAM is 8 KB).

enum { kWramSize = 0x20000, kSramBase = 0x20000, kSramSize = 0x2000 };

static uint32_t ReadMemory(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client) {
  (void)client;
  uint32_t done = 0;
  for (; done < num_bytes; done++) {
    const uint32_t at = address + done;
    if (at < kWramSize) buffer[done] = g_ram[at];
    else if (at >= kSramBase && at < kSramBase + kSramSize && g_sram) buffer[done] = g_sram[at - kSramBase];
    else break;   // a short read tells rcheevos the address is not backed
  }
  return done;
}

// ---- HTTP -----------------------------------------------------------------------------
// The same client as mzm's: httpc (http:C is granted in resources/template.rsf), with a
// fallback to plain HTTP when the console's old TLS stack cannot talk to the server.

static bool g_http_ready;
static bool g_tls_unusable;

static void EnsureHttp(void) {
  if (g_http_ready) return;
  const Result r = httpcInit(0x20000);
  if (R_SUCCEEDED(r)) g_http_ready = true;
  else LogLine("httpcInit failed: 0x%08lX", (unsigned long)r);
}

static void UserAgent(char *out, size_t size) {
  // Numeric version only (the server's check), then rcheevos' own clause.
  char ver[16];
  const char *src = APP_VERSION;
  size_t n = 0;
  while (*src && !(*src >= '0' && *src <= '9')) src++;
  while (*src && n + 1 < sizeof(ver) && ((*src >= '0' && *src <= '9') || *src == '.')) ver[n++] = *src++;
  while (n > 0 && ver[n - 1] == '.') n--;
  ver[n] = 0;
  if (!ver[0]) snprintf(ver, sizeof(ver), "0.0");
  int len = snprintf(out, size, "SuperMetroid3DS/%s (Nintendo 3DS) ", ver);
  if (len > 0 && (size_t)len < size && g_client) rc_client_get_user_agent_clause(g_client, out + len, size - len);
}

static int HttpOnce(const char *url, const char *post, const char *content_type, char *out, size_t size,
                    int *status_out, char *why, size_t why_size) {
  EnsureHttp();
  if (!g_http_ready) { snprintf(why, why_size, "httpc not available"); return -1; }
  httpcContext ctx;
  Result r = httpcOpenContext(&ctx, post ? HTTPC_METHOD_POST : HTTPC_METHOD_GET, url, 1);
  if (R_FAILED(r)) { snprintf(why, why_size, "open 0x%08lX", (unsigned long)r); return -2; }
  httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
  httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
  char ua[96];
  UserAgent(ua, sizeof(ua));
  httpcAddRequestHeaderField(&ctx, "User-Agent", ua);
  httpcAddRequestHeaderField(&ctx, "Accept", "*/*");
  httpcAddRequestHeaderField(&ctx, "Connection", "close");
  if (post) {
    httpcAddRequestHeaderField(&ctx, "Content-Type", content_type ? content_type : "application/x-www-form-urlencoded");
    httpcAddPostDataRaw(&ctx, (u32 *)(void *)post, (u32)strlen(post));
  }
  r = httpcBeginRequest(&ctx);
  if (R_FAILED(r)) {
    snprintf(why, why_size, "request 0x%08lX", (unsigned long)r);
    httpcCloseContext(&ctx);
    return -3;
  }
  u32 status = 0;
  r = httpcGetResponseStatusCodeTimeout(&ctx, &status, 15000000000ULL);
  if (R_FAILED(r) && status == 0) {
    snprintf(why, why_size, "response 0x%08lX", (unsigned long)r);
    httpcCloseContext(&ctx);
    return -4;
  }
  *status_out = (int)status;
  // Drain the whole body: httpcCloseContext hangs on a context with data pending.
  u32 total = 0;
  do {
    u32 got = 0;
    const u32 space = (u32)(size - 1) - total;
    if (!space) break;
    r = httpcDownloadData(&ctx, (u8 *)out + total, space, &got);
    total += got;
  } while (r == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);
  out[total] = 0;
  httpcCloseContext(&ctx);
  // RA always answers with a body: an empty one is a failed transfer (mzm saw HTTPS
  // answer 200 with nothing behind it).
  if (!total) { snprintf(why, why_size, "empty (http %lu)", (unsigned long)status); return -5; }
  return (int)total;
}

static int Http(const char *url, const char *post, const char *content_type, char *out, size_t size, int *status) {
  static const char kHttps[] = "https://";
  char why[48] = "";
  char plain[kRequestMax + 8];
  const bool https = !strncmp(url, kHttps, sizeof(kHttps) - 1);
  if (https) snprintf(plain, sizeof(plain), "http://%s", url + sizeof(kHttps) - 1);
  if (https && g_tls_unusable) return HttpOnce(plain, post, content_type, out, size, status, why, sizeof(why));
  int n = HttpOnce(url, post, content_type, out, size, status, why, sizeof(why));
  if (n >= 0 || !https) {
    if (n < 0) LogLine("HTTP failed: %s", why);
    return n;
  }
  LogLine("HTTPS failed (%s); retrying over HTTP", why);
  n = HttpOnce(plain, post, content_type, out, size, status, why, sizeof(why));
  if (n >= 0) {
    LogLine("HTTP works; using it for the rest of this session");
    g_tls_unusable = true;
  } else {
    LogLine("HTTP failed too: %s", why);
  }
  return n;
}

// ---- Server calls on a worker thread ----------------------------------------------------

typedef struct {
  char url[kRequestMax];
  char post[kRequestMax];
  char content_type[64];
  bool has_post;
  rc_client_server_callback_t callback;
  void *callback_data;
  char *body;
  int body_length;
  int status;
} RaCall;

static RaCall g_pending[kMaxPending], g_done[kMaxPending];
static int g_pending_count, g_done_count;
static char g_response[kResponseMax];   // the worker's buffer, one call at a time
static LightLock g_lock;
static Thread g_worker;
static volatile bool g_worker_stop;

static void Worker(void *arg) {
  (void)arg;
  while (!g_worker_stop) {
    RaCall call;
    bool have = false;
    LightLock_Lock(&g_lock);
    if (g_pending_count > 0 && g_done_count < kMaxPending) {
      call = g_pending[0];
      memmove(&g_pending[0], &g_pending[1], (g_pending_count - 1) * sizeof(RaCall));
      g_pending_count--;
      have = true;
    }
    LightLock_Unlock(&g_lock);
    if (!have) {
      svcSleepThread(20000000ULL);
      continue;
    }
    int status = 0;
    const int n = Http(call.url, call.has_post ? call.post : NULL, call.has_post ? call.content_type : NULL,
                       g_response, sizeof(g_response), &status);
    call.body = n < 0 ? NULL : g_response;
    call.body_length = n < 0 ? 0 : n;
    call.status = n < 0 ? RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR : status;
    // Never the query string: it carries the token, or the password on a login.
    LogLine("%.*s -> http %d, %d bytes", (int)strcspn(call.url, "?"), call.url, call.status, n);
    if (n > 0 && (status < 200 || status >= 300)) LogLine("  body: %.200s", g_response);
    LightLock_Lock(&g_lock);
    g_done[g_done_count++] = call;
    LightLock_Unlock(&g_lock);
    // The body lives in g_response until the main thread has copied it.
    for (;;) {
      LightLock_Lock(&g_lock);
      const bool drained = g_done_count == 0;
      LightLock_Unlock(&g_lock);
      if (drained || g_worker_stop) break;
      svcSleepThread(5000000ULL);
    }
  }
}

static void ServerCall(const rc_api_request_t *request, rc_client_server_callback_t callback, void *callback_data,
                       rc_client_t *client) {
  (void)client;
  if (!g_worker) {
    g_worker_stop = false;
    g_worker = threadCreate(Worker, NULL, 32 * 1024, 0x31, -1, false);
  }
  RaCall call;
  memset(&call, 0, sizeof(call));
  snprintf(call.url, sizeof(call.url), "%s", request->url ? request->url : "");
  if (request->post_data && request->post_data[0]) {
    call.has_post = true;
    snprintf(call.post, sizeof(call.post), "%s", request->post_data);
    snprintf(call.content_type, sizeof(call.content_type), "%s",
             request->content_type ? request->content_type : "application/x-www-form-urlencoded");
  }
  call.callback = callback;
  call.callback_data = callback_data;
  bool queued = false;
  LightLock_Lock(&g_lock);
  if (g_worker && g_pending_count < kMaxPending) {
    g_pending[g_pending_count++] = call;
    queued = true;
  }
  LightLock_Unlock(&g_lock);
  if (!queued) {   // rcheevos would otherwise wait for an answer forever
    rc_api_server_response_t response;
    memset(&response, 0, sizeof(response));
    response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    callback(&response, callback_data);
  }
}

static void DrainResponses(void) {
  static char body[kResponseMax];
  for (;;) {
    RaCall call;
    bool have = false;
    LightLock_Lock(&g_lock);
    if (g_done_count > 0) {
      call = g_done[0];
      if (call.body) {
        memcpy(body, call.body, call.body_length);
        body[call.body_length] = 0;
        call.body = body;
      }
      memmove(&g_done[0], &g_done[1], (g_done_count - 1) * sizeof(RaCall));
      g_done_count--;
      have = true;
    }
    LightLock_Unlock(&g_lock);
    if (!have) return;
    rc_api_server_response_t response;
    memset(&response, 0, sizeof(response));
    response.body = call.body;
    response.body_length = call.body_length;
    response.http_status_code = call.status;
    call.callback(&response, call.callback_data);
  }
}

// ---- Achievement list -------------------------------------------------------------------

static void RefreshList(void) {
  g_count = 0;
  Changed();
  if (!g_client) return;
  rc_client_achievement_list_t *list = rc_client_create_achievement_list(
      g_client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
  if (!list) return;
  for (uint32_t b = 0; b < list->num_buckets; b++) {
    for (uint32_t i = 0; i < list->buckets[b].num_achievements && g_count < kMaxAchievements; i++) {
      const rc_client_achievement_t *a = list->buckets[b].achievements[i];
      // rcheevos adds a 0-point warning for clients it does not know: noise here.
      if (a->points == 0 && a->title && !strcasecmp(a->title, "Warning: Unknown Emulator")) continue;
      RaAchievement *o = &g_list[g_count++];
      o->id = a->id;
      snprintf(o->title, sizeof(o->title), "%s", a->title ? a->title : "");
      snprintf(o->description, sizeof(o->description), "%s", a->description ? a->description : "");
      o->points = a->points;
      o->unlocked = a->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
    }
  }
  rc_client_destroy_achievement_list(list);
}

// ---- Events, login, game ----------------------------------------------------------------

static void EventHandler(const rc_client_event_t *event, rc_client_t *client) {
  (void)client;
  switch (event->type) {
  case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
    const rc_client_achievement_t *a = event->achievement;
    LogLine("UNLOCKED %lu '%s' (+%lu)", (unsigned long)a->id, a->title ? a->title : "", (unsigned long)a->points);
    memset(&g_toast, 0, sizeof(g_toast));
    g_toast.id = a->id;
    snprintf(g_toast.title, sizeof(g_toast.title), "%s", a->title ? a->title : "");
    g_toast.points = a->points;
    g_toast.unlocked = true;
    g_toast_until = osGetTime() + kToastMs;
    RefreshList();
    break;
  }
  case RC_CLIENT_EVENT_GAME_COMPLETED:
    RefreshList();
    break;
  case RC_CLIENT_EVENT_SERVER_ERROR:
    SetMessage("%s", event->server_error->error_message ? event->server_error->error_message : "server error");
    LogLine("server error (%s): %s", event->server_error->api, g_message);
    break;
  case RC_CLIENT_EVENT_DISCONNECTED:
    g_status = kRaOffline;
    Changed();
    break;
  case RC_CLIENT_EVENT_RECONNECTED:
    g_status = kRaOnline;
    Changed();
    break;
  default:
    break;
  }
}

static bool g_load_in_flight;
static int g_load_attempts;
static u64 g_load_retry_at;

static void LoadGameDone(int result, const char *error, rc_client_t *client, void *userdata) {
  (void)client, (void)userdata;
  g_load_in_flight = false;
  g_load_retry_at = osGetTime() + 10000;
  if (result == RC_OK) {
    const rc_client_game_t *game = rc_client_get_game_info(g_client);
    RefreshList();
    SetMessage("");
    LogLine("game loaded: id %lu '%s', %d achievements", (unsigned long)(game ? game->id : 0),
            game && game->title ? game->title : "", g_count);
  } else if (result == RC_NO_GAME_LOADED) {
    g_load_attempts = 99;   // an unknown hash stays unknown
    SetMessage("ROM not recognised by RetroAchievements");
    LogLine("game load: hash not recognised");
  } else {
    SetMessage("%s", error ? error : "game load failed");
    LogLine("game load failed (%d): %s", result, error ? error : "");
  }
}

static void BeginLoadGame(void) {
  if (!g_client || g_load_in_flight || rc_client_is_game_loaded(g_client)) return;
  if (osGetTime() < g_load_retry_at || g_load_attempts >= 5) return;
  g_load_in_flight = true;
  g_load_attempts++;
  rc_client_begin_load_game(g_client, kRomMd5, LoadGameDone, NULL);
}

static void LoginDone(int result, const char *error, rc_client_t *client, void *userdata) {
  (void)client, (void)userdata;
  if (result != RC_OK) {
    g_status = result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN ? kRaLoginError : kRaOffline;
    SetMessage("%s", error ? error : "login failed");
    LogLine("login failed (%d): %s", result, error ? error : "");
    return;
  }
  const rc_client_user_t *user = rc_client_get_user_info(g_client);
  if (user) {
    snprintf(g_user, sizeof(g_user), "%s", user->username ? user->username : g_user);
    snprintf(g_token, sizeof(g_token), "%s", user->token ? user->token : "");
    SaveIni();
  }
  g_status = kRaOnline;
  SetMessage("");
  LogLine("login ok: %s", g_user);
  g_load_attempts = 0;
  g_load_retry_at = 0;
  BeginLoadGame();
}

static void BeginLogin(const char *password) {
  if (!g_client) return;
  g_status = kRaConnecting;
  Changed();
  if (password && password[0]) rc_client_begin_login_with_password(g_client, g_user, password, LoginDone, NULL);
  else if (g_user[0] && g_token[0]) rc_client_begin_login_with_token(g_client, g_user, g_token, LoginDone, NULL);
  else g_status = kRaNoAccount;
}

static void ClientLog(const char *message, const rc_client_t *client) {
  (void)client;
  LogLine("rcheevos: %s", message);
}

// ---- Public -----------------------------------------------------------------------------

void RetroAch_Init(void) {
  if (g_client) return;
  LoadIni();
  LightLock_Init(&g_lock);
  g_client = rc_client_create(ReadMemory, ServerCall);
  if (!g_client) {
    g_status = kRaLoginError;
    SetMessage("rcheevos failed to start");
    return;
  }
  rc_client_set_event_handler(g_client, EventHandler);
  rc_client_set_hardcore_enabled(g_client, 0);
  rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_WARN, ClientLog);
  if (!g_enabled) g_status = kRaOff;
  else if (g_user[0] && g_token[0]) {
    LogLine("--- Super Metroid 3DS %s ---", APP_VERSION);
    BeginLogin(NULL);
  } else {
    g_status = kRaNoAccount;
  }
  Changed();
}

void RetroAch_Shutdown(void) {
  g_worker_stop = true;
  if (g_worker) {
    threadJoin(g_worker, U64_MAX);
    threadFree(g_worker);
    g_worker = NULL;
  }
  if (g_client) {
    rc_client_destroy(g_client);
    g_client = NULL;
  }
  if (g_http_ready) {
    httpcExit();
    g_http_ready = false;
  }
}

void RetroAch_Update(void) {
  if (!g_client) return;
  DrainResponses();
  if (g_enabled && g_status == kRaOnline && !rc_client_is_game_loaded(g_client)) BeginLoadGame();
  // Keeps the session alive and retries pending unlocks when no game frame runs (pause).
  rc_client_idle(g_client);
  if (g_toast_until && osGetTime() >= g_toast_until) {
    g_toast_until = 0;
    Changed();
  }
}

void RetroAch_DoFrame(void) {
  if (!g_client || !g_enabled || g_cheats_used) return;
  rc_client_do_frame(g_client);
}

void RetroAch_GameReset(void) {
  if (g_client) rc_client_reset(g_client);
}

static void ProgressPath(int slot, char *out, size_t size) { snprintf(out, size, "saves/save%d.rap", slot); }

void RetroAch_StateSaved(int slot) {
  if (!g_client || !rc_client_is_game_loaded(g_client)) return;
  const size_t size = rc_client_progress_size(g_client);
  uint8_t *buf = size ? malloc(size) : NULL;
  if (!buf) return;
  char path[32];
  ProgressPath(slot, path, sizeof(path));
  if (rc_client_serialize_progress_sized(g_client, buf, size) == RC_OK) {
    FILE *f = fopen(path, "wb");
    if (f) {
      fwrite(buf, 1, size, f);
      fclose(f);
    }
  }
  free(buf);
}

void RetroAch_StateLoaded(int slot) {
  if (!g_client || !rc_client_is_game_loaded(g_client)) return;
  char path[32];
  ProgressPath(slot, path, sizeof(path));
  FILE *f = fopen(path, "rb");
  if (!f) {   // a state saved before achievements were on: start every one afresh
    rc_client_deserialize_progress_sized(g_client, NULL, 0);
    return;
  }
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *buf = size > 0 ? malloc(size) : NULL;
  if (buf && fread(buf, 1, size, f) == (size_t)size) rc_client_deserialize_progress_sized(g_client, buf, size);
  else rc_client_deserialize_progress_sized(g_client, NULL, 0);
  free(buf);
  fclose(f);
}

void RetroAch_NoteCheat(void) {
  if (g_cheats_used) return;
  g_cheats_used = true;
  LogLine("a cheat or the teleport was used: achievements paused until restart");
  Changed();
}

bool RetroAch_CheatsUsed(void) { return g_cheats_used; }

bool RetroAch_Enabled(void) { return g_enabled; }

void RetroAch_SetEnabled(bool on) {
  if (on == g_enabled) return;
  g_enabled = on;
  SaveIni();
  if (!g_client) return;
  if (!on) {
    rc_client_unload_game(g_client);
    g_load_attempts = 0;
    g_count = 0;
    g_status = kRaOff;
  } else if (rc_client_get_user_info(g_client)) {
    g_status = kRaOnline;
  } else if (g_user[0] && g_token[0]) {
    BeginLogin(NULL);
  } else {
    g_status = kRaNoAccount;
  }
  Changed();
}

void RetroAch_PromptLogin(void) {
  SwkbdState kb;
  char user[64] = "", pass[64] = "";
  swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, sizeof(user) - 1);
  swkbdSetHintText(&kb, "RetroAchievements user name");
  if (g_user[0]) swkbdSetInitialText(&kb, g_user);
  if (swkbdInputText(&kb, user, sizeof(user)) != SWKBD_BUTTON_CONFIRM || !user[0]) return;
  swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, sizeof(pass) - 1);
  swkbdSetPasswordMode(&kb, SWKBD_PASSWORD_HIDE_DELAY);
  swkbdSetHintText(&kb, "RetroAchievements password");
  if (swkbdInputText(&kb, pass, sizeof(pass)) != SWKBD_BUTTON_CONFIRM || !pass[0]) return;
  snprintf(g_user, sizeof(g_user), "%s", user);
  g_token[0] = 0;
  g_enabled = true;
  SaveIni();
  if (g_client && rc_client_get_user_info(g_client)) rc_client_logout(g_client);
  BeginLogin(pass);   // the password is used once and never stored
  memset(pass, 0, sizeof(pass));
}

void RetroAch_Logout(void) {
  if (g_client) rc_client_logout(g_client);
  g_token[0] = 0;
  g_count = 0;
  g_load_attempts = 0;
  g_status = g_enabled ? kRaNoAccount : kRaOff;
  SaveIni();
  SetMessage("");
}

RaStatus RetroAch_Status(void) { return g_status; }
const char *RetroAch_User(void) { return g_user; }
const char *RetroAch_Message(void) { return g_message; }
bool RetroAch_GameLoaded(void) { return g_client && rc_client_is_game_loaded(g_client); }
int RetroAch_Count(void) { return g_count; }
const RaAchievement *RetroAch_Get(int i) { return i >= 0 && i < g_count ? &g_list[i] : NULL; }

int RetroAch_UnlockedCount(void) {
  int n = 0;
  for (int i = 0; i < g_count; i++) n += g_list[i].unlocked;
  return n;
}

uint32_t RetroAch_Points(bool unlocked_only) {
  uint32_t n = 0;
  for (int i = 0; i < g_count; i++)
    if (!unlocked_only || g_list[i].unlocked) n += g_list[i].points;
  return n;
}

uint32_t RetroAch_Version(void) { return g_version; }

const RaAchievement *RetroAch_Toast(void) { return g_toast_until ? &g_toast : NULL; }
