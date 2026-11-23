#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>

static bool apActive = false;
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
  if (cfg.ssid[0]) {
    WiFi.begin(cfg.ssid, cfg.pass);
    Serial.printf("[net] connecting to %s\n", cfg.ssid);
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
