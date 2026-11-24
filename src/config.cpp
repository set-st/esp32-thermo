#include "config.h"
#include <Preferences.h>

Config cfg;

static const char* NVS_NS = "thermo";
static const char* NVS_KEY = "cfg";

// ESP32-WROOM: GPIO 6..11 are wired to flash, 1/3 are UART0 (USB serial),
// 20/24/28..31 do not exist, 34..39 are input-only without pull-ups.
bool gpioValid(int p) {
  return p == 0 || p == 2 || (p >= 4 && p <= 5) || (p >= 12 && p <= 19) ||
         (p >= 21 && p <= 23) || (p >= 25 && p <= 27) || (p >= 32 && p <= 39);
}
bool gpioOutputCapable(int p) { return gpioValid(p) && p < 34; }

void configDefaults(Config& c) {
  memset(&c, 0, sizeof(c));
  strlcpy(c.hostname, "thermohub", sizeof(c.hostname));
  strlcpy(c.apPass, "thermohub", sizeof(c.apPass));
  c.pinSda = 21;
  c.pinScl = 22;
  c.pinOneWire = 4;
  c.pinEncA = 32;    // CLK
  c.pinEncB = 35;    // DT  (input-only, no internal pull-up: module pull-ups, + to 3.3 V)
  c.pinEncBtn = 33;  // SW
  c.lcdAddr = 0;
  c.lcdCols = 20;
  c.lcdRows = 4;
  c.lcdCharset = 0;
  c.lcdBlMode = 0;
  c.lcdBlTimeout = 300;
  c.encReverse = false;
  c.encStepsPerDetent = 4;
  c.pollMs = 2000;
  c.pressUnit = 1;
  c.updAuto = true;
  for (int i = 0; i < DS_COUNT; i++) {
    snprintf(c.ds[i].name, sizeof(c.ds[i].name), "T%d", i + 1);
    c.ds[i].addr[0] = 0;
    c.ds[i].offset = 0;
    c.ds[i].maxTemp = 100;
  }
}

String validatePins(const Config& c) {
  struct P { const char* name; int pin; bool needOut; bool optional; };
  P pins[] = {
      {"I2C SDA", c.pinSda, true, false},     {"I2C SCL", c.pinScl, true, false},
      {"1-Wire", c.pinOneWire, true, false},  {"Енкодер A", c.pinEncA, false, true},
      {"Енкодер B", c.pinEncB, false, true},  {"Кнопка енкодера", c.pinEncBtn, false, true},
  };
  const int n = sizeof(pins) / sizeof(pins[0]);
  for (int i = 0; i < n; i++) {
    if (pins[i].pin < 0) {
      if (pins[i].optional) continue;
      return String(pins[i].name) + ": пін обов'язковий";
    }
    if (!gpioValid(pins[i].pin))
      return String(pins[i].name) + ": GPIO" + pins[i].pin + " не можна використовувати";
    if (pins[i].needOut && !gpioOutputCapable(pins[i].pin))
      return String(pins[i].name) + ": GPIO" + pins[i].pin + " працює лише на вхід";
    for (int k = 0; k < i; k++)
      if (pins[k].pin == pins[i].pin)
        return String(pins[i].name) + " і " + pins[k].name + " на одному GPIO" + pins[i].pin;
  }
  return "";
}

static bool validAddr(const char* s) {
  size_t n = strlen(s);
  if (n == 0) return true;
  if (n != 16) return false;
  for (size_t i = 0; i < n; i++)
    if (!isxdigit((unsigned char)s[i])) return false;
  return true;
}

void configToJson(const Config& c, JsonObject o, bool withSecrets) {
  JsonObject w = o["wifi"].to<JsonObject>();
  w["ssid"] = c.ssid;
  w["hostname"] = c.hostname;
  if (withSecrets) {
    w["pass"] = c.pass;
    w["apPass"] = c.apPass;
  } else {
    w["hasPass"] = c.pass[0] != 0;
  }

  JsonObject p = o["pins"].to<JsonObject>();
  p["sda"] = c.pinSda;
  p["scl"] = c.pinScl;
  p["onewire"] = c.pinOneWire;
  p["encA"] = c.pinEncA;
  p["encB"] = c.pinEncB;
  p["encBtn"] = c.pinEncBtn;

  JsonObject l = o["lcd"].to<JsonObject>();
  l["addr"] = c.lcdAddr;
  l["cols"] = c.lcdCols;
  l["rows"] = c.lcdRows;
  l["charset"] = c.lcdCharset;
  l["blMode"] = c.lcdBlMode;
  l["blTimeout"] = c.lcdBlTimeout;

  JsonObject e = o["encoder"].to<JsonObject>();
  e["reverse"] = c.encReverse;
  e["stepsPerDetent"] = c.encStepsPerDetent;

  o["pollMs"] = c.pollMs;
  o["pressUnit"] = c.pressUnit;
  o["dsDemo"] = c.dsDemo;
  o["updAuto"] = c.updAuto;

  JsonArray ds = o["ds"].to<JsonArray>();
  for (int i = 0; i < DS_COUNT; i++) {
    JsonObject s = ds.add<JsonObject>();
    s["name"] = c.ds[i].name;
    s["addr"] = c.ds[i].addr;
    s["offset"] = roundf(c.ds[i].offset * 100) / 100.0;
    s["max"] = roundf(c.ds[i].maxTemp * 10) / 10.0;
  }
}

// ---- merge helpers -------------------------------------------------------

namespace {
struct Merger {
  String err;
  bool reboot = false;

  void str(char* dst, size_t cap, JsonVariantConst v, bool needsReboot, const char* field) {
    if (!v.is<const char*>()) return;
    const char* s = v.as<const char*>();
    if (strlen(s) >= cap) {
      if (err.isEmpty()) err = String(field) + ": задовге значення";
      return;
    }
    if (strcmp(dst, s) != 0) {
      strlcpy(dst, s, cap);
      if (needsReboot) reboot = true;
    }
  }

  template <typename T>
  void num(T& dst, JsonVariantConst v, long lo, long hi, bool needsReboot, const char* field) {
    if (!v.is<long>()) return;
    long x = v.as<long>();
    if (x < lo || x > hi) {
      if (err.isEmpty()) err = String(field) + ": поза допустимим діапазоном";
      return;
    }
    if ((long)dst != x) {
      dst = (T)x;
      if (needsReboot) reboot = true;
    }
  }

  void flag(bool& dst, JsonVariantConst v, bool needsReboot) {
    if (!v.is<bool>()) return;
    bool x = v.as<bool>();
    if (dst != x) {
      dst = x;
      if (needsReboot) reboot = true;
    }
  }
};
}  // namespace

String configFromJson(Config& c, JsonVariantConst j, bool& needReboot) {
  Merger m;

  JsonVariantConst w = j["wifi"];
  m.str(c.ssid, sizeof(c.ssid), w["ssid"], true, "SSID");
  m.str(c.pass, sizeof(c.pass), w["pass"], true, "Пароль Wi-Fi");
  m.str(c.hostname, sizeof(c.hostname), w["hostname"], true, "Ім'я хоста");
  m.str(c.apPass, sizeof(c.apPass), w["apPass"], true, "Пароль точки доступу");

  JsonVariantConst p = j["pins"];
  m.num(c.pinSda, p["sda"], -1, 39, true, "SDA");
  m.num(c.pinScl, p["scl"], -1, 39, true, "SCL");
  m.num(c.pinOneWire, p["onewire"], -1, 39, true, "1-Wire");
  m.num(c.pinEncA, p["encA"], -1, 39, true, "Енкодер A");
  m.num(c.pinEncB, p["encB"], -1, 39, true, "Енкодер B");
  m.num(c.pinEncBtn, p["encBtn"], -1, 39, true, "Кнопка енкодера");

  JsonVariantConst l = j["lcd"];
  m.num(c.lcdAddr, l["addr"], 0, 0x7F, true, "Адреса LCD");
  m.num(c.lcdCols, l["cols"], 8, 40, true, "LCD columns");
  m.num(c.lcdRows, l["rows"], 1, 4, true, "LCD rows");
  m.num(c.lcdCharset, l["charset"], 0, 1, false, "LCD charset");
  m.num(c.lcdBlMode, l["blMode"], 0, 2, false, "Підсвітка");
  m.num(c.lcdBlTimeout, l["blTimeout"], 10, 3600, false, "Час до вимкнення підсвітки");

  JsonVariantConst e = j["encoder"];
  m.flag(c.encReverse, e["reverse"], false);
  m.num(c.encStepsPerDetent, e["stepsPerDetent"], 1, 4, false, "Steps per detent");

  m.num(c.pollMs, j["pollMs"], 1000, 60000, false, "Інтервал опитування");
  m.num(c.pressUnit, j["pressUnit"], 0, 1, false, "Pressure unit");
  m.flag(c.dsDemo, j["dsDemo"], false);
  m.flag(c.updAuto, j["updAuto"], false);

  JsonArrayConst ds = j["ds"];
  for (int i = 0; i < DS_COUNT && i < (int)ds.size(); i++) {
    JsonVariantConst s = ds[i];
    m.str(c.ds[i].name, sizeof(c.ds[i].name), s["name"], false, "Назва датчика");
    if (s["addr"].is<const char*>()) {
      const char* a = s["addr"];
      if (!validAddr(a)) {
        if (m.err.isEmpty()) m.err = "Адреса датчика — 16 шістнадцяткових символів";
      } else {
        strlcpy(c.ds[i].addr, a, sizeof(c.ds[i].addr));
        for (char* q = c.ds[i].addr; *q; q++) *q = toupper(*q);
      }
    }
    if (s["max"].is<float>()) {
      float mx = s["max"];
      if (isnan(mx) || mx < 1 || mx > 999) {
        if (m.err.isEmpty()) m.err = "Максимальна температура: від 1 до 999 °C";
      } else {
        c.ds[i].maxTemp = mx;
      }
    }
    if (s["offset"].is<float>()) {
      float o = s["offset"];
      if (isnan(o) || fabsf(o) > OFFSET_LIMIT) {
        if (m.err.isEmpty()) m.err = "Поправка має бути в межах ±20";
      } else {
        c.ds[i].offset = o;
      }
    }
  }

  if (m.err.isEmpty()) m.err = validatePins(c);
  needReboot = m.reboot;
  return m.err;
}

void configLoad() {
  configDefaults(cfg);
  Preferences p;
  if (!p.begin(NVS_NS, true)) return;
  String s = p.getString(NVS_KEY, "");
  p.end();
  if (s.isEmpty()) return;

  JsonDocument d;
  if (deserializeJson(d, s)) {
    Serial.println("[cfg] corrupt config, using defaults");
    return;
  }
  bool rb;
  String err = configFromJson(cfg, d.as<JsonVariantConst>(), rb);
  if (err.length()) Serial.printf("[cfg] load warning: %s\n", err.c_str());
  if (validatePins(cfg).length()) {
    Config def;
    configDefaults(def);
    cfg.pinSda = def.pinSda;
    cfg.pinScl = def.pinScl;
    cfg.pinOneWire = def.pinOneWire;
    cfg.pinEncA = def.pinEncA;
    cfg.pinEncB = def.pinEncB;
    cfg.pinEncBtn = def.pinEncBtn;
    Serial.println("[cfg] invalid pins in config, reverted to defaults");
  }
}

bool configSave() {
  JsonDocument d;
  configToJson(cfg, d.to<JsonObject>(), true);
  String s;
  serializeJson(d, s);
  Preferences p;
  if (!p.begin(NVS_NS, false)) return false;
  bool ok = p.putString(NVS_KEY, s) == s.length();
  p.end();
  return ok;
}

void configReset() {
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.clear();
    p.end();
  }
  configDefaults(cfg);
}
