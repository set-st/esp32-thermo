// Async web server (ESPAsyncWebServer). Callbacks run in the AsyncTCP task on
// core 0, independent from the main loop on core 1, so a slow or idle browser
// connection never freezes the LCD, encoder or sensors (and vice versa).
// Every handler that touches shared state takes StateGuard; nothing here blocks
// for long — Wi-Fi scan and update check run in background tasks.

#include "web.h"
#include "config.h"
#include "sensors.h"
#include "net.h"
#include "ui.h"
#include "index_html_gz.h"  // generated from web_page.h
#include "inet_update.h"
#include "encoder.h"
#include "lock.h"
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <Update.h>

static AsyncWebServer server(80);
static volatile uint32_t rebootAt = 0;
static constexpr size_t MAX_BODY = 8192;

static void scheduleReboot(uint32_t ms = 1000) { rebootAt = millis() + ms; }

static void sendJson(AsyncWebServerRequest* r, JsonDocument& d, int code = 200, bool cors = false) {
  String s;
  serializeJson(d, s);
  AsyncWebServerResponse* resp = r->beginResponse(code, "application/json", s);
  resp->addHeader("Cache-Control", "no-store");
  if (cors) resp->addHeader("Access-Control-Allow-Origin", "*");
  r->send(resp);
}

static void sendError(AsyncWebServerRequest* r, int code, const String& msg) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = msg;
  sendJson(r, d, code);
}

static void sendOk(AsyncWebServerRequest* r) {
  JsonDocument d;
  d["ok"] = true;
  sendJson(r, d);
}

// POST with a JSON body: chunks are collected into request->_tempObject (freed by
// the request), the handler runs once the whole body has arrived.
using JsonHandler = std::function<void(AsyncWebServerRequest*, JsonDocument&)>;

static void onJsonPost(const char* path, JsonHandler fn) {
  server.on(
      path, HTTP_POST,
      [fn](AsyncWebServerRequest* r) {
        if (!r->_tempObject) return sendError(r, 400, "Потрібне тіло запиту у форматі JSON");
        JsonDocument body;
        if (DeserializationError e = deserializeJson(body, (const char*)r->_tempObject))
          return sendError(r, 400, String("Некоректний JSON: ") + e.c_str());
        fn(r, body);
      },
      nullptr,
      [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
        if (total > MAX_BODY) return;
        if (index == 0) {
          r->_tempObject = malloc(total + 1);
          if (!r->_tempObject) return;
        }
        if (!r->_tempObject) return;
        memcpy((char*)r->_tempObject + index, data, len);
        if (index + len == total) ((char*)r->_tempObject)[total] = 0;
      });
}

// ---- monitoring ---------------------------------------------------------------------

static void handleSensors(AsyncWebServerRequest* r) {
  JsonDocument d;
  {
    StateGuard g;
    sensorsToJson(d.to<JsonObject>());
  }
  d["uptime_s"] = millis() / 1000;
  JsonObject w = d["wifi"].to<JsonObject>();
  w["connected"] = netStaConnected();
  w["rssi"] = netStaConnected() ? WiFi.RSSI() : 0;
  w["ip"] = netIp();
  sendJson(r, d, 200, true);
}

static void metricLine(String& out, const char* name, const String& labels, float v) {
  if (isnan(v)) return;
  out += name;
  if (labels.length()) out += "{" + labels + "}";
  out += " " + String(v, 2) + "\n";
}

static void handleMetrics(AsyncWebServerRequest* r) {
  String m;
  m.reserve(1024);
  {
    StateGuard g;
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
  }
  metricLine(m, "thermohub_wifi_rssi_dbm", "", netStaConnected() ? WiFi.RSSI() : NAN);
  metricLine(m, "thermohub_uptime_seconds", "", millis() / 1000);
  metricLine(m, "thermohub_free_heap_bytes", "", ESP.getFreeHeap());
  r->send(200, "text/plain; version=0.0.4", m);
}

static void handleDisplay(AsyncWebServerRequest* r) {
  uint32_t since = r->hasParam("since") ? r->getParam("since")->value().toInt() : 0;
  JsonDocument d;
  {
    StateGuard g;
    uiScreenToJson(d.to<JsonObject>());
    encoderDiagToJson(d["enc"].to<JsonObject>(), since);
  }
  sendJson(r, d);
}

// ---- configuration ----------------------------------------------------------------

static void handleGetConfig(AsyncWebServerRequest* r) {
  JsonDocument d;
  {
    StateGuard g;
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
    JsonObject u = i["update"].to<JsonObject>();
    inetUpdateToJson(u);
    u.remove("notes");
    i["uptime_s"] = millis() / 1000;
  }
  sendJson(r, d);
}

static void handlePostConfig(AsyncWebServerRequest* r, JsonDocument& body) {
  bool reboot = false;
  {
    StateGuard g;
    Config tmp = cfg;
    String err = configFromJson(tmp, body.as<JsonVariantConst>(), reboot);
    if (err.length()) return sendError(r, 400, err);
    cfg = tmp;
    if (!configSave()) return sendError(r, 500, "Не вдалося записати у флеш-пам'ять");
    if (reboot) uiMessage("Config saved", "Rebooting...");
  }
  JsonDocument d;
  d["ok"] = true;
  d["reboot"] = reboot;
  sendJson(r, d);
  if (reboot) scheduleReboot(1500);
}

// POST /api/offset {"slot":1..4, "offset":0.5}
static void handleOffset(AsyncWebServerRequest* r, JsonDocument& body) {
  int slot = body["slot"] | 0;
  if (slot < 1 || slot > DS_COUNT) return sendError(r, 400, "slot має бути від 1 до 4");
  if (!body["offset"].is<float>()) return sendError(r, 400, "Не вказано offset");
  float o = body["offset"];
  if (isnan(o) || fabsf(o) > OFFSET_LIMIT) return sendError(r, 400, "Поправка має бути в межах ±20");
  {
    StateGuard g;
    cfg.ds[slot - 1].offset = o;
    if (!configSave()) return sendError(r, 500, "Не вдалося записати у флеш-пам'ять");
  }
  sendOk(r);
}

// ---- scans --------------------------------------------------------------------------

static void handleScanOneWire(AsyncWebServerRequest* r) {
  JsonDocument d;
  {
    StateGuard g;  // 1-Wire bus is shared with the sensor polling
    sensorsScanOneWire(d["found"].to<JsonArray>());
  }
  sendJson(r, d);
}

static void handleScanI2c(AsyncWebServerRequest* r) {
  JsonDocument d;
  {
    StateGuard g;  // I2C bus is shared with the LCD and sensors
    sensorsI2cDiag(d.to<JsonObject>());
  }
  sendJson(r, d);
}

// GET /api/scan/wifi -> {"scanning":true} (poll again) or {"networks":[...]}
static void handleScanWifi(AsyncWebServerRequest* r) {
  JsonDocument d;
  int st = netScanAsync(d["networks"].to<JsonArray>());
  if (st < 0) return sendError(r, 503, "Не вдалося сканувати Wi-Fi, спробуйте ще раз");
  if (st == 1) {
    d.clear();
    d["scanning"] = true;
  }
  sendJson(r, d);
}

// ---- display ------------------------------------------------------------------------

// POST /api/display/input {"turn":1} | {"turn":-1} | {"press":"click"} | {"press":"long"}
static void handleDisplayInput(AsyncWebServerRequest* r, JsonDocument& body) {
  const char* p = body["press"] | "";
  int press = strcmp(p, "click") == 0 ? 1 : strcmp(p, "long") == 0 ? 2 : 0;
  int turn = body["turn"] | 0;
  if (!turn && !press) return sendError(r, 400, "Потрібно вказати turn або press");
  JsonDocument d;
  {
    StateGuard g;
    uiVirtualInput(turn, press);
    uiLoop();  // process now so the response shows the new screen
    uiScreenToJson(d.to<JsonObject>());
  }
  sendJson(r, d);
}

// POST /api/display/charset {"page":0|1|2} — character ROM test on the physical LCD
static void handleCharset(AsyncWebServerRequest* r, JsonDocument& body) {
  {
    StateGuard g;
    uiCharsetTest(body["page"] | 0);
  }
  sendOk(r);
}

// ---- internet updates ---------------------------------------------------------------

static void handleUpdateState(AsyncWebServerRequest* r) {
  JsonDocument d;
  {
    StateGuard g;
    inetUpdateToJson(d.to<JsonObject>());
  }
  sendJson(r, d);
}

static void handleUpdateCheck(AsyncWebServerRequest* r) {
  inetUpdateCheckAsync();  // client polls GET /api/update until "checking" is false
  handleUpdateState(r);
}

static void handleUpdateInstall(AsyncWebServerRequest* r) {
  String err;
  bool ok;
  {
    StateGuard g;
    ok = inetUpdateInstall(err);
  }
  if (!ok) return sendError(r, 409, err);
  handleUpdateState(r);
}

// ---- system -------------------------------------------------------------------------

static void handleReboot(AsyncWebServerRequest* r) {
  {
    StateGuard g;
    uiMessage("Rebooting...");
  }
  sendOk(r);
  scheduleReboot();
}

static void handleFactoryReset(AsyncWebServerRequest* r) {
  {
    StateGuard g;
    configReset();
    uiMessage("Factory reset", "Rebooting...");
  }
  sendOk(r);
  scheduleReboot();
}

// Manual OTA from a file (multipart). Upload chunks are written straight to flash.
static void handleOtaDone(AsyncWebServerRequest* r) {
  if (Update.hasError() || !Update.isFinished())
    return sendError(r, 500, String("Помилка оновлення: ") + Update.errorString());
  {
    StateGuard g;
    uiMessage("Update OK", "Rebooting...");
  }
  sendOk(r);
  scheduleReboot();
}

static void handleOtaUpload(AsyncWebServerRequest* r, const String& filename, size_t index,
                            uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    Serial.printf("[ota] %s\n", filename.c_str());
    // A previous upload may have been cut off mid-way (flaky Wi-Fi): start clean.
    if (Update.isRunning()) Update.abort();
    {
      StateGuard g;
      uiMessage("Updating...", "", 60000);
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    // Connection dropped before the last chunk -> discard the partial image.
    r->onDisconnect([]() {
      if (Update.isRunning() && !Update.isFinished()) {
        Update.abort();
        Serial.println("[ota] upload aborted (client disconnected)");
        StateGuard g;
        uiMessage("Update aborted", "", 3000);
      }
    });
  }
  if (!Update.isRunning()) return;
  if (len && Update.write(data, len) != len) Update.printError(Serial);
  if (final && !Update.end(true)) Update.printError(Serial);
}

// ---- setup --------------------------------------------------------------------------

void webBegin() {
  // Admin page: pre-gzipped at build time (~4x smaller) with an ETag, so a reload
  // costs a 304 instead of the whole page.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (r->hasHeader("If-None-Match") && r->header("If-None-Match") == INDEX_HTML_ETAG) {
      r->send(304);
      return;
    }
    AsyncWebServerResponse* resp =
        r->beginResponse(200, "text/html; charset=utf-8", INDEX_HTML_GZ, sizeof(INDEX_HTML_GZ));
    resp->addHeader("Content-Encoding", "gzip");
    resp->addHeader("Cache-Control", "no-cache");
    resp->addHeader("ETag", INDEX_HTML_ETAG);
    r->send(resp);
  });
  server.on("/api/sensors", HTTP_GET, handleSensors);
  server.on("/metrics", HTTP_GET, handleMetrics);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  onJsonPost("/api/config", handlePostConfig);
  onJsonPost("/api/offset", handleOffset);
  server.on("/api/scan/onewire", HTTP_GET, handleScanOneWire);
  server.on("/api/scan/i2c", HTTP_GET, handleScanI2c);
  server.on("/api/scan/wifi", HTTP_GET, handleScanWifi);
  server.on("/api/display", HTTP_GET, handleDisplay);
  onJsonPost("/api/display/input", handleDisplayInput);
  onJsonPost("/api/display/charset", handleCharset);
  server.on("/api/update", HTTP_GET, handleUpdateState);
  server.on("/api/update/check", HTTP_POST, handleUpdateCheck);
  server.on("/api/update/install", HTTP_POST, handleUpdateInstall);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/update", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.onNotFound([](AsyncWebServerRequest* r) { sendError(r, 404, "Не знайдено"); });
  server.begin();
}

void webLoop() {
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    delay(100);
    ESP.restart();
  }
}
