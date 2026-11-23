#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

constexpr int DS_COUNT = 4;
constexpr float OFFSET_LIMIT = 20.0f;

struct DsSlot {
  char name[24];
  char addr[17];   // 16 hex chars ROM code, "" = slot empty
  float offset;    // correction, °C
};

struct Config {
  // Wi-Fi
  char ssid[33];
  char pass[65];
  char hostname[33];
  char apPass[65];   // fallback AP password (>= 8 chars, otherwise AP is open)
  // Pins
  int8_t pinSda, pinScl, pinOneWire;
  int8_t pinEncA, pinEncB, pinEncBtn;  // -1 = not used
  // LCD
  uint8_t lcdAddr;   // 0 = autodetect
  uint8_t lcdCols, lcdRows;
  // Encoder
  bool encReverse;
  uint8_t encStepsPerDetent;  // 1, 2 or 4
  // Misc
  uint16_t pollMs;
  uint8_t pressUnit;  // 0 = hPa, 1 = mmHg (LCD only)
  DsSlot ds[DS_COUNT];
};

extern Config cfg;

void configDefaults(Config& c);
void configLoad();
bool configSave();
void configReset();

// Serialises config. withSecrets=false hides passwords (for the web UI).
void configToJson(const Config& c, JsonObject o, bool withSecrets);
// Merges JSON into c. Unknown/missing keys are left untouched.
// Sets needReboot if a field that only applies on boot was changed.
// Returns "" on success or a human readable error.
String configFromJson(Config& c, JsonVariantConst j, bool& needReboot);
String validatePins(const Config& c);

bool gpioValid(int p);
bool gpioOutputCapable(int p);
