#include "web.h"
#include "config.h"
#include "sensors.h"
#include "net.h"
#include "ui.h"
#include "web_page.h"
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>

static WebServer server(80);
static uint32_t rebootAt = 0;

static void scheduleReboot(uint32_t ms = 1000) { rebootAt = millis() + ms; }

static void sendJson(JsonDocument& d, int code = 200) {
  String s;
  serializeJson(d, s);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", s);
}

static void sendError(int code, const String& msg) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = msg;
  sendJson(d, code);
}

static bool parseBody(JsonDocument& d) {
  if (!server.hasArg("plain")) {
    sendError(400, "JSON body required");
    return false;
  }
  if (DeserializationError e = deserializeJson(d, server.arg("plain"))) {
    sendError(400, String("Bad JSON: ") + e.c_str());
    return false;
  }
  return true;
}

static void handleSensors() {
  JsonDocument d;
  sensorsToJson(d.to<JsonObject>());
  d["uptime_s"] = millis() / 1000;
  JsonObject w = d["wifi"].to<JsonObject>();
  w["connected"] = netStaConnected();
  w["rssi"] = netStaConnected() ? WiFi.RSSI() : 0;
  w["ip"] = netIp();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  sendJson(d);
}

static void metricLine(String& out, const char* name, const String& labels, float v) {
  if (isnan(v)) return;
  out += name;
  if (labels.length()) out += "{" + labels + "}";
  out += " " + String(v, 2) + "\n";
}

static void handleMetrics() {
  String m;
  m.reserve(1024);
  m += "# TYPE thermohub_ds18b20_celsius gauge\n";
  for (int i = 0; i < DS_COUNT; i++) {
    String l = "slot=\"" + String(i + 1) + "\",name=\"" + cfg.ds[i].name + "\"";
    metricLine(m, "thermohub_ds18b20_celsius", l, dsTemp(i));
  }
  metricLine(m, "thermohub_ds18b20_demo", "", cfg.dsDemo ? 1 : 0);
  if (sens.ahtOk) {
    metricLine(m, "thermohub_aht20_celsius", "", sens.ahtTemp);
    metricLine(m, "thermohub_aht20_humidity_percent", "", sens.ahtHum);
  }
  if (sens.bmpOk) {
    metricLine(m, "thermohub_bmp280_celsius", "", sens.bmpTemp);
    metricLine(m, "thermohub_bmp280_pressure_hpa", "", sens.bmpPress);
  }
  metricLine(m, "thermohub_wifi_rssi_dbm", "", netStaConnected() ? WiFi.RSSI() : NAN);
  metricLine(m, "thermohub_uptime_seconds", "", millis() / 1000);
  metricLine(m, "thermohub_free_heap_bytes", "", ESP.getFreeHeap());
  server.send(200, "text/plain; version=0.0.4", m);
}

static void handleGetConfig() {
  JsonDocument d;
  configToJson(cfg, d["config"].to<JsonObject>(), false);
  JsonObject i = d["info"].to<JsonObject>();
  i["version"] = FW_VERSION;
  i["mac"] = WiFi.macAddress();
  i["ip"] = netIp();
  i["apSsid"] = netApSsid();
  i["apActive"] = netApActive();
  i["staConnected"] = netStaConnected();
  i["staReason"] = netLastReason();
  i["staProblem"] = netStaProblem(false);
  i["lcdAddr"] = uiLcdAddr();
  i["bmpAddr"] = sens.bmpAddr;
  i["freeHeap"] = ESP.getFreeHeap();
  i["uptime_s"] = millis() / 1000;
  sendJson(d);
}

static void handlePostConfig() {
  JsonDocument body;
  if (!parseBody(body)) return;

  Config tmp = cfg;
  bool reboot = false;
  String err = configFromJson(tmp, body.as<JsonVariantConst>(), reboot);
  if (err.length()) return sendError(400, err);

  cfg = tmp;
  if (!configSave()) return sendError(500, "Не вдалося записати у флеш-пам'ять");

  JsonDocument d;
  d["ok"] = true;
  d["reboot"] = reboot;
  sendJson(d);
  if (reboot) {
    uiMessage("Config saved", "Rebooting...");
    scheduleReboot(1500);
  }
}

// POST /api/offset {"slot":1..4, "offset":0.5}
static void handleOffset() {
  JsonDocument body;
  if (!parseBody(body)) return;
  int slot = body["slot"] | 0;
  if (slot < 1 || slot > DS_COUNT) return sendError(400, "slot must be 1..4");
  if (!body["offset"].is<float>()) return sendError(400, "offset required");
  float o = body["offset"];
  if (isnan(o) || fabsf(o) > OFFSET_LIMIT) return sendError(400, "Поправка має бути в межах ±20");
  cfg.ds[slot - 1].offset = o;
  if (!configSave()) return sendError(500, "Не вдалося записати у флеш-пам'ять");
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
}

static void handleScanOneWire() {
  JsonDocument d;
  sensorsScanOneWire(d["found"].to<JsonArray>());
  sendJson(d);
}

static void handleScanI2c() {
  JsonDocument d;
  sensorsScanI2c(d["found"].to<JsonArray>());
  sendJson(d);
}

static void handleScanWifi() {
  JsonDocument d;
  int n = netScan(d["networks"].to<JsonArray>());
  if (n < 0) return sendError(503, "Не вдалося сканувати Wi-Fi, спробуйте ще раз");
  sendJson(d);
}

static void handleDisplay() {
  JsonDocument d;
  uiScreenToJson(d.to<JsonObject>());
  sendJson(d);
}

// POST /api/display/input {"turn":1} | {"turn":-1} | {"press":"click"} | {"press":"long"}
static void handleDisplayInput() {
  JsonDocument body;
  if (!parseBody(body)) return;
  const char* p = body["press"] | "";
  int press = strcmp(p, "click") == 0 ? 1 : strcmp(p, "long") == 0 ? 2 : 0;
  int turn = body["turn"] | 0;
  if (!turn && !press) return sendError(400, "turn or press required");
  uiVirtualInput(turn, press);
  uiLoop();  // process immediately so the response reflects the new screen
  JsonDocument d;
  uiScreenToJson(d.to<JsonObject>());
  sendJson(d);
}

// POST /api/display/charset {"page":0|1} — character ROM test on the physical LCD
static void handleCharset() {
  JsonDocument body;
  if (!parseBody(body)) return;
  uiCharsetTest(body["page"] | 0);
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
}

static void handleReboot() {
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
  uiMessage("Rebooting...");
  scheduleReboot();
}

static void handleFactoryReset() {
  configReset();
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
  uiMessage("Factory reset", "Rebooting...");
  scheduleReboot();
}

static void handleOtaDone() {
  if (Update.hasError()) return sendError(500, String("Помилка оновлення: ") + Update.errorString());
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
  uiMessage("Update OK", "Rebooting...");
  scheduleReboot();
}

static void handleOtaUpload() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] %s\n", u.filename.c_str());
    uiMessage("Updating...");
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (Update.write(u.buf, u.currentSize) != u.currentSize) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

void webBegin() {
  server.on("/", HTTP_GET, []() {
      server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/sensors", HTTP_GET, handleSensors);
  server.on("/metrics", HTTP_GET, handleMetrics);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/offset", HTTP_POST, handleOffset);
  server.on("/api/scan/onewire", HTTP_GET, handleScanOneWire);
  server.on("/api/scan/i2c", HTTP_GET, handleScanI2c);
  server.on("/api/scan/wifi", HTTP_GET, handleScanWifi);
  server.on("/api/display", HTTP_GET, handleDisplay);
  server.on("/api/display/input", HTTP_POST, handleDisplayInput);
  server.on("/api/display/charset", HTTP_POST, handleCharset);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/update", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.onNotFound([]() { sendError(404, "Not found"); });
  server.begin();
}

void webLoop() {
  server.handleClient();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    delay(100);
    ESP.restart();
  }
}
