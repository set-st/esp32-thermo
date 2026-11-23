#include "sensors.h"
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

SensorState sens;

static OneWire* ow = nullptr;
static DallasTemperature* dallas = nullptr;
static Adafruit_AHTX0 aht;
static Adafruit_BMP280 bmp(&Wire);
static bool ahtPresent = false, bmpPresent = false;

static uint32_t lastPoll = 0, convStart = 0, lastI2cProbe = 0;
static bool converting = false;

static constexpr uint32_t CONVERSION_MS = 800;  // 12 bit = 750 ms
static constexpr uint8_t MAX_ERRORS = 3;

static bool parseAddr(const char* s, DeviceAddress a) {
  if (strlen(s) != 16) return false;
  for (int i = 0; i < 8; i++) {
    char b[3] = {s[2 * i], s[2 * i + 1], 0};
    char* end;
    a[i] = (uint8_t)strtoul(b, &end, 16);
    if (*end) return false;
  }
  return true;
}

static void addrToStr(const uint8_t* a, char* out) {
  for (int i = 0; i < 8; i++) sprintf(out + 2 * i, "%02X", a[i]);
  out[16] = 0;
}

static bool isDsFamily(uint8_t f) { return f == 0x28 || f == 0x10 || f == 0x22 || f == 0x3B; }

static bool i2cPresent(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static void probeI2c() {
  if (!ahtPresent && i2cPresent(AHTX0_I2CADDR_DEFAULT)) {
    ahtPresent = aht.begin(&Wire);
    Serial.printf("[i2c] AHT20 %s\n", ahtPresent ? "found" : "init failed");
  }
  if (!bmpPresent) {
    const uint8_t addrs[] = {0x77, 0x76};
    for (uint8_t a : addrs) {
      if (i2cPresent(a) && bmp.begin(a)) {
        bmpPresent = true;
        sens.bmpAddr = a;
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X2,
                        Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::FILTER_X16,
                        Adafruit_BMP280::STANDBY_MS_500);
        Serial.printf("[i2c] BMP280 found at 0x%02X\n", a);
        break;
      }
    }
  }
}

// Puts sensors that are on the bus but not assigned to any slot into empty slots.
static void autoAssign() {
  uint8_t a[8];
  bool changed = false;
  ow->reset_search();
  while (ow->search(a)) {
    if (OneWire::crc8(a, 7) != a[7] || !isDsFamily(a[0])) continue;
    char s[17];
    addrToStr(a, s);
    bool known = false;
    for (int i = 0; i < DS_COUNT; i++)
      if (strcasecmp(cfg.ds[i].addr, s) == 0) known = true;
    if (known) continue;
    for (int i = 0; i < DS_COUNT; i++) {
      if (!cfg.ds[i].addr[0]) {
        strlcpy(cfg.ds[i].addr, s, sizeof(cfg.ds[i].addr));
        Serial.printf("[1w] sensor %s auto-assigned to slot %d\n", s, i + 1);
        changed = true;
        break;
      }
    }
  }
  if (changed) configSave();
}

void sensorsBegin() {
  for (int i = 0; i < DS_COUNT; i++) sens.ds[i] = {false, NAN, 0};
  sens.ahtTemp = sens.ahtHum = sens.bmpTemp = sens.bmpPress = NAN;

  ow = new OneWire(cfg.pinOneWire);
  dallas = new DallasTemperature(ow);
  dallas->begin();
  dallas->setResolution(12);
  dallas->setWaitForConversion(false);
  Serial.printf("[1w] %d device(s) on GPIO%d\n", dallas->getDeviceCount(), cfg.pinOneWire);
  autoAssign();

  probeI2c();
  lastI2cProbe = millis();
}

static void readI2c() {
  if (ahtPresent) {
    sensors_event_t h, t;
    if (aht.getEvent(&h, &t)) {
      sens.ahtOk = true;
      sens.ahtTemp = t.temperature;
      sens.ahtHum = h.relative_humidity;
    } else {
      sens.ahtOk = false;
      ahtPresent = false;  // re-probe later
    }
  } else {
    sens.ahtOk = false;
  }

  if (bmpPresent && i2cPresent(sens.bmpAddr)) {
    float t = bmp.readTemperature();
    float p = bmp.readPressure();
    if (!isnan(t) && !isnan(p) && p > 10000) {
      sens.bmpOk = true;
      sens.bmpTemp = t;
      sens.bmpPress = p / 100.0f;
    } else {
      sens.bmpOk = false;
    }
  } else {
    sens.bmpOk = false;
    bmpPresent = false;
  }
}

static void readDs() {
  for (int i = 0; i < DS_COUNT; i++) {
    DsReading& r = sens.ds[i];
    DeviceAddress a;
    if (!parseAddr(cfg.ds[i].addr, a)) {
      r = {false, NAN, 0};
      continue;
    }
    float t = dallas->getTempC(a);
    if (t == DEVICE_DISCONNECTED_C) {
      if (r.errors < 255) r.errors++;
      if (r.errors >= MAX_ERRORS) r.ok = false;
    } else {
      r.raw = t;
      r.ok = true;
      r.errors = 0;
    }
  }
}

void sensorsLoop() {
  uint32_t now = millis();
  if (!converting && now - lastPoll >= cfg.pollMs) {
    lastPoll = now;
    dallas->requestTemperatures();
    convStart = now;
    converting = true;
    readI2c();
  }
  if (converting && now - convStart >= CONVERSION_MS) {
    converting = false;
    readDs();
    sens.lastUpdate = now;
  }
  if ((!ahtPresent || !bmpPresent) && now - lastI2cProbe > 10000) {
    lastI2cProbe = now;
    probeI2c();
  }
}

float dsTemp(int i) {
  if (i < 0 || i >= DS_COUNT || !sens.ds[i].ok) return NAN;
  return sens.ds[i].raw + cfg.ds[i].offset;
}

void sensorsScanOneWire(JsonArray arr) {
  uint8_t a[8];
  ow->reset_search();
  while (ow->search(a)) {
    if (OneWire::crc8(a, 7) != a[7] || !isDsFamily(a[0])) continue;
    char s[17];
    addrToStr(a, s);
    JsonObject o = arr.add<JsonObject>();
    o["addr"] = s;
    int slot = 0;
    for (int i = 0; i < DS_COUNT; i++)
      if (strcasecmp(cfg.ds[i].addr, s) == 0) slot = i + 1;
    o["slot"] = slot;  // 0 = not assigned
  }
}

void sensorsScanI2c(JsonArray arr) {
  for (uint8_t a = 1; a < 0x7F; a++)
    if (i2cPresent(a)) arr.add(a);
}

static double r2(float v) { return roundf(v * 100) / 100.0; }

void sensorsToJson(JsonObject o) {
  JsonArray ds = o["ds18b20"].to<JsonArray>();
  for (int i = 0; i < DS_COUNT; i++) {
    JsonObject s = ds.add<JsonObject>();
    s["slot"] = i + 1;
    s["name"] = cfg.ds[i].name;
    s["addr"] = cfg.ds[i].addr;
    s["ok"] = sens.ds[i].ok;
    s["offset"] = r2(cfg.ds[i].offset);
    if (sens.ds[i].ok) {
      s["raw"] = r2(sens.ds[i].raw);
      s["temp"] = r2(dsTemp(i));
    } else {
      s["raw"] = nullptr;
      s["temp"] = nullptr;
    }
  }

  JsonObject a = o["aht20"].to<JsonObject>();
  a["ok"] = sens.ahtOk;
  if (sens.ahtOk) {
    a["temp"] = r2(sens.ahtTemp);
    a["humidity"] = r2(sens.ahtHum);
  } else {
    a["temp"] = nullptr;
    a["humidity"] = nullptr;
  }

  JsonObject b = o["bmp280"].to<JsonObject>();
  b["ok"] = sens.bmpOk;
  if (sens.bmpOk) {
    b["temp"] = r2(sens.bmpTemp);
    b["pressure_hpa"] = r2(sens.bmpPress);
    b["pressure_mmhg"] = r2(hpaToMmHg(sens.bmpPress));
  } else {
    b["temp"] = nullptr;
    b["pressure_hpa"] = nullptr;
    b["pressure_mmhg"] = nullptr;
  }
  o["age_ms"] = millis() - sens.lastUpdate;
}
