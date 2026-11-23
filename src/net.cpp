#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "lock.h"
#include <vector>
#include <algorithm>

static bool apActive = false;
static volatile uint8_t lastReason = 0;
static volatile uint16_t failCount = 0;

static const char* authName(wifi_auth_mode_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "?";
  }
}

// Logs what the radio actually sees for the configured SSID.
static void diagScan() {
  int n = WiFi.scanNetworks(false, true);
  int found = 0;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != cfg.ssid) continue;
    found++;
    Serial.printf("[net] seen '%s' %s ch%d %ddBm %s\n", cfg.ssid, WiFi.BSSIDstr(i).c_str(),
                  WiFi.channel(i), WiFi.RSSI(i), authName(WiFi.encryptionType(i)));
  }
  if (!found) {
    Serial.printf("[net] '%s' NOT visible (%d networks around). 5 GHz-only network? Name typo?\n",
                  cfg.ssid, n);
    for (int i = 0; i < n && i < 15; i++)
      Serial.printf("[net]   '%s' ch%d %ddBm\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i));
  }
  WiFi.scanDelete();
}

static void onWifiEvent(arduino_event_id_t ev, arduino_event_info_t info) {
  if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t r = info.wifi_sta_disconnected.reason;
    failCount++;
    if (r != lastReason || failCount % 10 == 1)
      Serial.printf("[net] STA disconnected, reason %u (%s), attempt %u\n", r,
                    WiFi.disconnectReasonName((wifi_err_reason_t)r), failCount);
    lastReason = r;
  } else if (ev == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    Serial.println("[net] STA associated, waiting for DHCP");
  }
}

uint8_t netLastReason() { return lastReason; }

// Scan works in any state: in AP-only mode the STA interface is enabled
// temporarily, and endless connect attempts (wrong password etc.) are paused,
// otherwise the driver refuses to scan (WIFI_SCAN_FAILED).
int netScan(JsonArray out) {
  wifi_mode_t mode = WiFi.getMode();
  bool connected = WiFi.status() == WL_CONNECTED;
  bool addedSta = !(mode & WIFI_MODE_STA);
  bool paused = false;

  if (addedSta) {
    WiFi.mode(mode == WIFI_MODE_AP ? WIFI_AP_STA : WIFI_STA);
  } else if (!connected) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    paused = true;
    delay(100);
  }

  int n = WIFI_SCAN_FAILED;
  for (int attempt = 0; attempt < 3 && n < 0; attempt++) {
    if (attempt) delay(300);
    n = WiFi.scanNetworks(false, true, false, 100);
  }
  Serial.printf("[net] scan: %d networks\n", n);

  // De-duplicate by SSID (mesh / several APs), keep the strongest, sort by signal.
  struct Net { String ssid; int32_t rssi; uint8_t ch; bool open; };
  std::vector<Net> nets;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;  // hidden network
    bool dup = false;
    for (auto& x : nets)
      if (x.ssid == ssid) {
        dup = true;
        if (WiFi.RSSI(i) > x.rssi) x = {ssid, WiFi.RSSI(i), (uint8_t)WiFi.channel(i), x.open};
      }
    if (!dup) nets.push_back({ssid, WiFi.RSSI(i), (uint8_t)WiFi.channel(i),
                              WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  std::sort(nets.begin(), nets.end(), [](const Net& a, const Net& b) { return a.rssi > b.rssi; });
  for (auto& x : nets) {
    JsonObject o = out.add<JsonObject>();
    o["ssid"] = x.ssid;
    o["rssi"] = x.rssi;
    o["channel"] = x.ch;
    o["open"] = x.open;
  }

  // Restore previous state.
  if (addedSta && !cfg.ssid[0]) WiFi.mode(mode);
  if (paused) {
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.ssid, cfg.pass);
  }
  return n;
}

// Human readable reason why the STA link is down ("" if connected / nothing to report).
const char* netStaProblem(bool shortText) {
  if (!cfg.ssid[0] || WiFi.status() == WL_CONNECTED) return "";
  switch (lastReason) {
    case 0: return "";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_MIC_FAILURE:
      return shortText ? "Wrong password?" : "Роутер не приймає пароль — перевірте пароль Wi-Fi";
    case WIFI_REASON_NO_AP_FOUND:
      return shortText ? "Network not found" : "Мережу не знайдено (помилка в назві або мережа лише 5 ГГц)";
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_ASSOC_EXPIRE:
    case WIFI_REASON_BEACON_TIMEOUT:
      return shortText ? "Weak signal?" : "Роутер не відповідає — слабкий сигнал?";
    default:
      return shortText ? "Connect failed" : "Помилка підключення";
  }
}
static bool apForced = false;
static uint32_t lostSince = 0, connectedSince = 0;

static constexpr uint32_t AP_FALLBACK_MS = 20000;  // no STA link this long -> start AP
static constexpr uint32_t AP_LINGER_MS = 60000;    // STA up this long & nobody on AP -> stop AP

String netApSsid() {
  uint8_t m[6];
  WiFi.macAddress(m);
  char buf[24];
  snprintf(buf, sizeof(buf), "ThermoHub-%02X%02X", m[4], m[5]);
  return buf;
}

static void startAp() {
  WiFi.mode(cfg.ssid[0] ? WIFI_AP_STA : WIFI_AP);
  const char* pass = strlen(cfg.apPass) >= 8 ? cfg.apPass : nullptr;
  WiFi.softAP(netApSsid().c_str(), pass);
  apActive = true;
  Serial.printf("[net] AP %s started, http://%s\n", netApSsid().c_str(),
                WiFi.softAPIP().toString().c_str());
}

static void stopAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  Serial.println("[net] AP stopped");
}

void netBegin(bool forceAp) {
  WiFi.persistent(false);
  WiFi.setHostname(cfg.hostname);
  WiFi.setAutoReconnect(true);
  apForced = forceAp;

  if (forceAp || !cfg.ssid[0]) {
    startAp();
  } else {
    WiFi.mode(WIFI_STA);
  }
  // Modem sleep (default) wakes the radio only on AP beacons: +100..300 ms per
  // packet and multi-second stalls for the web UI. Mains-powered device -> off.
  WiFi.setSleep(false);
  WiFi.onEvent(onWifiEvent);
  if (cfg.ssid[0]) {
    diagScan();
    WiFi.begin(cfg.ssid, cfg.pass);
    Serial.printf("[net] connecting to '%s' (password %u chars)\n", cfg.ssid, strlen(cfg.pass));
  }
  lostSince = millis();

  if (MDNS.begin(cfg.hostname)) MDNS.addService("http", "tcp", 80);
}

void netLoop() {
  uint32_t now = millis();
  bool up = WiFi.status() == WL_CONNECTED;

  if (up) {
    if (lostSince) {
      Serial.printf("[net] connected, IP %s\n", WiFi.localIP().toString().c_str());
      connectedSince = now;
      WiFi.setSleep(false);  // re-apply after (re)connect
    }
    lostSince = 0;
    if (apActive && !apForced && now - connectedSince > AP_LINGER_MS &&
        WiFi.softAPgetStationNum() == 0)
      stopAp();
  } else {
    if (!lostSince) lostSince = now;
    if (!apActive && now - lostSince > AP_FALLBACK_MS) startAp();
  }
}

bool netStaConnected() { return WiFi.status() == WL_CONNECTED; }
bool netApActive() { return apActive; }

String netIp() {
  if (netStaConnected()) return WiFi.localIP().toString();
  if (apActive) return WiFi.softAPIP().toString();
  return "0.0.0.0";
}

// ---- background scan for the web UI ----------------------------------------------
// A scan takes 2-6 s, far too long for the async web server callback, so it runs
// in its own task and the client polls for the result.
static JsonDocument scanDoc;
static volatile uint8_t scanState = 0;  // 0 idle, 1 running, 2 done, 3 failed
static uint32_t scanDoneAt = 0;

static void scanTask(void*) {
  JsonDocument d;
  int n = netScan(d.to<JsonArray>());
  {
    StateGuard g;
    scanDoc = d;
    scanState = n < 0 ? 3 : 2;
    scanDoneAt = millis();
  }
  vTaskDelete(nullptr);
}

int netScanAsync(JsonArray out) {
  StateGuard g;
  if (scanState == 1) return 1;
  if (scanState == 3) {
    scanState = 0;
    return -1;
  }
  if (scanState == 2 && millis() - scanDoneAt < 15000) {
    for (JsonVariant v : scanDoc.as<JsonArray>()) out.add(v);
    scanState = 0;
    return 0;
  }
  scanState = 1;
  if (xTaskCreatePinnedToCore(scanTask, "wifiscan", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
    scanState = 0;
    return -1;
  }
  return 1;
}
