#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>

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

// Human readable reason why the STA link is down ("" if connected / nothing to report).
const char* netStaProblem(bool shortText) {
  if (!cfg.ssid[0] || WiFi.status() == WL_CONNECTED) return "";
  switch (lastReason) {
    case 0: return "";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_MIC_FAILURE:
      return shortText ? "Wrong password?" : "Роутер не принимает пароль — проверьте пароль Wi-Fi";
    case WIFI_REASON_NO_AP_FOUND:
      return shortText ? "Network not found" : "Сеть не найдена (опечатка в имени или сеть только 5 ГГц)";
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_ASSOC_EXPIRE:
    case WIFI_REASON_BEACON_TIMEOUT:
      return shortText ? "Weak signal?" : "Нет ответа от роутера — слабый сигнал?";
    default:
      return shortText ? "Connect failed" : "Ошибка подключения";
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
