#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

struct DsReading {
  bool ok;
  float raw;       // °C without correction (last good value)
  uint8_t errors;  // consecutive read failures
};

struct SensorState {
  DsReading ds[DS_COUNT];
  bool ahtOk;
  float ahtTemp, ahtHum;
  bool bmpOk;
  float bmpTemp, bmpPress;  // °C, hPa
  uint8_t bmpAddr;
  uint32_t lastUpdate;      // millis()
};

extern SensorState sens;

void sensorsBegin();
void sensorsLoop();

// Corrected DS18B20 value (raw + offset). NAN if not available.
float dsTemp(int slot);

// Fills arr with ROM codes of all DS18x20 sensors found on the bus.
void sensorsScanOneWire(JsonArray arr);
// Fills arr with all I2C addresses that answered.
void sensorsScanI2c(JsonArray arr);
// Full sensor snapshot for the API.
void sensorsToJson(JsonObject o);

inline float hpaToMmHg(float hpa) { return hpa * 0.750061683f; }
