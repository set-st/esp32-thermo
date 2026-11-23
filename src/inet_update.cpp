#include "inet_update.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include "fw_pubkey.h"
#include "lock.h"

#ifndef GH_REPO
#define GH_REPO "set-st/esp32-thermo"
#endif

static const char* USER_AGENT = "ThermoHub/" FW_VERSION;

enum class Phase : uint8_t { Idle, Downloading, Verifying, Done, Error };

static struct {
  String latest, notes, pageUrl, binUrl, sigUrl, error;
  uint32_t binSize = 0;
  bool available = false;
  uint32_t checkedAt = 0;  // millis() of the last check, 0 = never
} st;

static volatile Phase phase = Phase::Idle;
static volatile int progress = 0;
static String installError;
static uint32_t doneAt = 0;
static volatile bool checking = false;

// ---- versions --------------------------------------------------------------------

// "1.2.3", "v1.2.3", "1.2.3-4-gabc" -> {1,2,3}
static void parseVersion(const String& v, int out[3]) {
  out[0] = out[1] = out[2] = 0;
  const char* p = v.c_str();
  if (*p == 'v' || *p == 'V') p++;
  for (int i = 0; i < 3 && *p; i++) {
    out[i] = atoi(p);
    while (isdigit((unsigned char)*p)) p++;
    if (*p != '.') break;
    p++;
  }
}

static bool isNewer(const String& latest, const String& current) {
  int a[3], b[3];
  parseVersion(latest, a);
  parseVersion(current, b);
  for (int i = 0; i < 3; i++)
    if (a[i] != b[i]) return a[i] > b[i];
  return false;
}

// ---- check -------------------------------------------------------------------------

static bool setCheckError(const String& e) {
  StateGuard g;
  st.checkedAt = millis() | 1;
  st.error = e;
  return false;
}

bool inetUpdateCheck() {
  if (WiFi.status() != WL_CONNECTED) return setCheckError("Немає підключення до мережі");

  // TLS is used without certificate pinning: authenticity of the firmware itself
  // is guaranteed by the signature check before installing.
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setUserAgent(USER_AGENT);
  http.setTimeout(15000);
  if (!http.begin(client, "https://api.github.com/repos/" GH_REPO "/releases/latest"))
    return setCheckError("Не вдалося з'єднатися з GitHub");
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != 200) {
    http.end();
    return setCheckError(code == 404 ? String("Релізів ще немає") : "GitHub відповів HTTP " + String(code));
  }
  String body = http.getString();
  http.end();

  JsonDocument filter;
  filter["tag_name"] = true;
  filter["body"] = true;
  filter["html_url"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  filter["assets"][0]["size"] = true;
  JsonDocument d;
  if (deserializeJson(d, body, DeserializationOption::Filter(filter)))
    return setCheckError("Некоректна відповідь GitHub");

  StateGuard g;
  st.checkedAt = millis() | 1;
  st.error = "";
  st.latest = d["tag_name"] | "";
  st.notes = d["body"] | "";
  if (st.notes.length() > 2000) st.notes = st.notes.substring(0, 2000) + "…";
  st.pageUrl = d["html_url"] | "";
  st.binUrl = st.sigUrl = "";
  for (JsonObject a : d["assets"].as<JsonArray>()) {
    String name = a["name"] | "";
    if (name == "firmware.bin") {
      st.binUrl = a["browser_download_url"] | "";
      st.binSize = a["size"] | 0;
    } else if (name == "firmware.sig") {
      st.sigUrl = a["browser_download_url"] | "";
    }
  }
  st.available = isNewer(st.latest, FW_VERSION);
  if (st.available && (st.binUrl.isEmpty() || st.sigUrl.isEmpty())) {
    st.error = "У релізі " + st.latest + " немає firmware.bin або firmware.sig";
    st.available = false;
  }
  Serial.printf("[upd] latest %s, current %s, available=%d\n", st.latest.c_str(), FW_VERSION,
                st.available);
  return true;
}

static void checkTask(void*) {
  inetUpdateCheck();
  checking = false;
  vTaskDelete(nullptr);
}

bool inetUpdateCheckAsync() {
  if (checking) return true;
  checking = true;
  if (xTaskCreatePinnedToCore(checkTask, "updcheck", 10240, nullptr, 1, nullptr, 0) != pdPASS) {
    checking = false;
    return false;
  }
  return true;
}

// ---- install -----------------------------------------------------------------------

static bool httpGetBegin(HTTPClient& http, WiFiClientSecure& client, const String& url) {
  client.setInsecure();
  http.setUserAgent(USER_AGENT);
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub assets redirect to a CDN
  return http.begin(client, url);
}

static String sigUrl, binUrl;  // copied from `st` when the install starts

static String downloadSignature(uint8_t* sig, size_t cap, size_t& len) {
  WiFiClientSecure client;
  HTTPClient http;
  if (!httpGetBegin(http, client, sigUrl)) return "Не вдалося завантажити підпис";
  int code = http.GET();
  if (code != 200) {
    http.end();
    return "Підпис: HTTP " + String(code);
  }
  String s = http.getString();
  http.end();
  if (s.length() == 0 || s.length() > cap) return "Некоректний файл підпису";
  memcpy(sig, s.c_str(), s.length());
  len = s.length();
  return "";
}

static bool verifySignature(const uint8_t hash[32], const uint8_t* sig, size_t sigLen) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int r = mbedtls_pk_parse_public_key(&pk, (const unsigned char*)FW_PUBKEY_PEM,
                                      strlen(FW_PUBKEY_PEM) + 1);
  if (r == 0) r = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sigLen);
  mbedtls_pk_free(&pk);
  if (r) Serial.printf("[upd] signature check failed: -0x%04X\n", -r);
  return r == 0;
}

static String doInstall() {
  uint8_t sig[160];
  size_t sigLen = 0;
  String err = downloadSignature(sig, sizeof(sig), sigLen);
  if (err.length()) return err;

  WiFiClientSecure client;
  HTTPClient http;
  if (!httpGetBegin(http, client, binUrl)) return "Не вдалося завантажити прошивку";
  int code = http.GET();
  if (code != 200) {
    http.end();
    return "Прошивка: HTTP " + String(code);
  }
  int total = http.getSize();
  if (total <= 0) {
    http.end();
    return "Невідомий розмір прошивки";
  }
  if (!Update.begin(total)) {
    http.end();
    return String("Немає місця для оновлення: ") + Update.errorString();
  }

  mbedtls_md_context_t md;
  mbedtls_md_init(&md);
  mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&md);

  static uint8_t buf[2048];
  WiFiClient* s = http.getStreamPtr();
  int got = 0;
  uint32_t lastData = millis();
  while (got < total) {
    size_t avail = s->available();
    if (avail) {
      int n = s->readBytes(buf, min(avail, sizeof(buf)));
      if (n <= 0) continue;
      mbedtls_md_update(&md, buf, n);
      if (Update.write(buf, n) != (size_t)n) {
        err = String("Помилка запису: ") + Update.errorString();
        break;
      }
      got += n;
      progress = (int)((int64_t)got * 100 / total);
      lastData = millis();
    } else if (!s->connected() || millis() - lastData > 20000) {
      err = "Обрив з'єднання під час завантаження";
      break;
    } else {
      delay(2);
    }
  }
  http.end();

  uint8_t hash[32];
  mbedtls_md_finish(&md, hash);
  mbedtls_md_free(&md);
  if (err.isEmpty() && got != total) err = "Завантажено не повністю";
  if (err.length()) {
    Update.abort();
    return err;
  }

  phase = Phase::Verifying;
  if (!verifySignature(hash, sig, sigLen)) {
    Update.abort();
    return "Невірний підпис прошивки — оновлення відхилено";
  }
  if (!Update.end()) return String("Помилка оновлення: ") + Update.errorString();
  return "";
}

static void installTask(void*) {
  String err = doInstall();
  if (err.length()) {
    installError = err;
    phase = Phase::Error;
    Serial.printf("[upd] install failed: %s\n", err.c_str());
  } else {
    phase = Phase::Done;
    doneAt = millis();
    Serial.println("[upd] installed, rebooting");
  }
  vTaskDelete(nullptr);
}

bool inetUpdateInstall(String& err) {
  if (phase == Phase::Downloading || phase == Phase::Verifying) {
    err = "Оновлення вже виконується";
    return false;
  }
  if (!st.available) {
    err = "Немає доступного оновлення — спершу перевірте";
    return false;
  }
  installError = "";
  sigUrl = st.sigUrl;
  binUrl = st.binUrl;
  progress = 0;
  phase = Phase::Downloading;
  if (xTaskCreate(installTask, "inetupd", 12288, nullptr, 1, nullptr) != pdPASS) {
    phase = Phase::Error;
    err = installError = "Не вдалося запустити оновлення";
    return false;
  }
  return true;
}

int inetUpdateProgress() {
  if (phase == Phase::Downloading) return progress;
  if (phase == Phase::Verifying || phase == Phase::Done) return 101;
  return -1;
}

void inetUpdateLoop() {
  uint32_t now = millis();
  if (phase == Phase::Done && now - doneAt > 1500) ESP.restart();

  // Auto-check one minute after boot, then once a day.
  if (!cfg.updAuto || phase == Phase::Downloading || phase == Phase::Verifying) return;
  if (WiFi.status() != WL_CONNECTED) return;
  bool due = st.checkedAt == 0 ? now > 60000UL : now - st.checkedAt > 24UL * 3600 * 1000;
  if (due && !checking) {
    st.checkedAt = now | 1;  // don't retrigger while the task runs
    inetUpdateCheckAsync();
  }
}

void inetUpdateToJson(JsonObject o) {
  o["current"] = FW_VERSION;
  o["repo"] = GH_REPO;
  o["autoCheck"] = cfg.updAuto;
  o["checked"] = st.checkedAt != 0;
  o["checking"] = (bool)checking;
  if (st.checkedAt) o["checkedAgo_s"] = (millis() - st.checkedAt) / 1000;
  o["latest"] = st.latest;
  o["available"] = st.available;
  o["notes"] = st.notes;
  o["url"] = st.pageUrl;
  o["size"] = st.binSize;
  o["error"] = st.error;
  static const char* const NAMES[] = {"idle", "downloading", "verifying", "done", "error"};
  o["phase"] = NAMES[(int)phase];
  o["progress"] = progress;
  o["installError"] = installError;
}
