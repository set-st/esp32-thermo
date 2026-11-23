// LCD pages and encoder menu.
//
// View mode:  rotate = switch page, click = next page, long press = calibration menu
//   page 0: four DS18B20 temperatures
//   page 1: AHT20 temperature/humidity + BMP280 pressure/temperature
//   page 2: network info (IP / AP name)
// Calibration menu: rotate = select T1..T4 / Exit, click = edit, long = back
// Edit offset:      rotate = ±0.1 °C (fast spin ±0.5), click = save, long = cancel
// Menu/edit leave automatically after 30 s of inactivity (without saving).

#include "ui.h"
#include "config.h"
#include "sensors.h"
#include "encoder.h"
#include "net.h"
#include <Wire.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>

static LiquidCrystal_I2C* lcd = nullptr;
static uint8_t lcdAddr = 0;
static char shown[4][41];

enum class Mode { View, Menu, Edit };
static Mode mode = Mode::View;
static int page = 0;
static constexpr int PAGES = 3;
static int sel = 0;  // 0..3 = T1..T4, 4 = exit
static float editOffset = 0;
static uint32_t lastInput = 0, lastDraw = 0, msgUntil = 0;
static bool dirty = true;

static constexpr char DEG = (char)0xDF;  // degree sign in HD44780 ROM A00

static bool i2cPresent(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static uint8_t detectLcd() {
  // PCF8574 0x20-0x27, PCF8574A 0x38-0x3F (0x38 skipped: that's the AHT20)
  const uint8_t order[] = {0x27, 0x3F, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x20,
                           0x3E, 0x3D, 0x3C, 0x3B, 0x3A, 0x39};
  for (uint8_t a : order)
    if (i2cPresent(a)) return a;
  return 0;
}

uint8_t uiLcdAddr() { return lcdAddr; }

static void putLine(int row, const char* text) {
  if (!lcd || row >= cfg.lcdRows || row >= 4) return;
  char buf[41];
  int cols = min<int>(cfg.lcdCols, 40);
  int n = strlen(text);
  for (int i = 0; i < cols; i++) buf[i] = i < n ? text[i] : ' ';
  buf[cols] = 0;
  if (strcmp(buf, shown[row]) == 0) return;  // avoid flicker
  strcpy(shown[row], buf);
  lcd->setCursor(0, row);
  lcd->print(buf);
}

void uiMessage(const char* l1, const char* l2, uint32_t ms) {
  putLine(0, l1);
  putLine(1, l2);
  msgUntil = millis() + ms;
}

// Pops up the address on the LCD whenever the device gets a new IP
// (STA connected / reconnected with another lease, or fallback AP started).
static void watchNetwork() {
  static String lastShown;
  String key;
  if (netStaConnected()) key = "S" + WiFi.localIP().toString();
  else if (netApActive()) key = "A" + WiFi.softAPIP().toString();
  if (key.isEmpty() || key == lastShown) return;
  lastShown = key;
  if (mode == Mode::Edit) return;  // don't disturb offset editing
  if (key[0] == 'S') uiMessage("WiFi OK, IP:", key.c_str() + 1, 5000);
  else uiMessage(netApSsid().c_str(), key.c_str() + 1, 5000);  // "ThermoHub-XXXX" fits 16 cols
}

static void fmtTemp(char* out, size_t n, float t) {
  if (isnan(t)) snprintf(out, n, "--.-");
  else snprintf(out, n, "%.1f", t);
}

static void drawPageDs() {
  char a[8], b[8], l[41];
  for (int row = 0; row < 2; row++) {
    fmtTemp(a, sizeof(a), dsTemp(row * 2));
    fmtTemp(b, sizeof(b), dsTemp(row * 2 + 1));
    snprintf(l, sizeof(l), "%d:%-5s %d:%-5s", row * 2 + 1, a, row * 2 + 2, b);
    putLine(row, l);
  }
}

static void drawPageI2c() {
  char l[41];
  if (sens.ahtOk) snprintf(l, sizeof(l), "T%.1f%cC H%.1f%%", sens.ahtTemp, DEG, sens.ahtHum);
  else snprintf(l, sizeof(l), "AHT20: no data");
  putLine(0, l);

  if (sens.bmpOk) {
    if (cfg.pressUnit == 1)
      snprintf(l, sizeof(l), "P%.1fmm %.1f%cC", hpaToMmHg(sens.bmpPress), sens.bmpTemp, DEG);
    else
      snprintf(l, sizeof(l), "P%.1fhPa %.1f%c", sens.bmpPress, sens.bmpTemp, DEG);
  } else {
    snprintf(l, sizeof(l), "BMP280: no data");
  }
  putLine(1, l);
}

static void drawPageNet() {
  char l[41];
  if (netStaConnected()) {
    String ip = WiFi.localIP().toString();
    // "IP " prefix only if the full address still fits (xxx.xxx.xxx.xxx = 15 chars)
    snprintf(l, sizeof(l), ip.length() + 3 <= cfg.lcdCols ? "IP %s" : "%s", ip.c_str());
    putLine(0, l);
    snprintf(l, sizeof(l), "RSSI %d up %luh", WiFi.RSSI(), millis() / 3600000UL);
    putLine(1, l);
  } else if (netApActive()) {
    snprintf(l, sizeof(l), "AP %s", WiFi.softAPIP().toString().c_str());
    putLine(0, l);
    putLine(1, netApSsid().c_str());
  } else {
    putLine(0, "WiFi connecting");
    putLine(1, cfg.ssid);
  }
}

static void drawRawCorr(int slot, float offset) {
  char l[41], a[8], b[8];
  if (sens.ds[slot].ok) {
    fmtTemp(a, sizeof(a), sens.ds[slot].raw);
    fmtTemp(b, sizeof(b), sens.ds[slot].raw + offset);
    snprintf(l, sizeof(l), "%s>%s%cC", a, b, DEG);
  } else {
    snprintf(l, sizeof(l), cfg.ds[slot].addr[0] ? "sensor offline" : "slot empty");
  }
  putLine(1, l);
}

static void draw() {
  if (!lcd || millis() < msgUntil) return;
  char l[41];
  switch (mode) {
    case Mode::View:
      if (page == 0) drawPageDs();
      else if (page == 1) drawPageI2c();
      else drawPageNet();
      break;
    case Mode::Menu:
      if (sel < DS_COUNT) {
        snprintf(l, sizeof(l), "Calib>T%d %+.2f", sel + 1, cfg.ds[sel].offset);
        putLine(0, l);
        drawRawCorr(sel, cfg.ds[sel].offset);
      } else {
        putLine(0, "Calib>Exit");
        putLine(1, "");
      }
      break;
    case Mode::Edit:
      snprintf(l, sizeof(l), "Set T%d: %+.2f", sel + 1, editOffset);
      putLine(0, l);
      drawRawCorr(sel, editOffset);
      break;
  }
}

void uiBegin() {
  encoderBegin();
  lcdAddr = cfg.lcdAddr ? (i2cPresent(cfg.lcdAddr) ? cfg.lcdAddr : 0) : detectLcd();
  if (!lcdAddr) {
    Serial.println("[lcd] not found");
    return;
  }
  Serial.printf("[lcd] found at 0x%02X\n", lcdAddr);
  lcd = new LiquidCrystal_I2C(lcdAddr, cfg.lcdCols, cfg.lcdRows);
  lcd->init();
  lcd->backlight();
  lcd->clear();
  memset(shown, 0, sizeof(shown));
  uiMessage("ThermoHub", "v" FW_VERSION);
}

void uiLoop() {
  watchNetwork();
  uint32_t now = millis();
  int d = encoderDelta();
  BtnEvent b = encoderButton();

  if (d || b != BtnEvent::None) {
    lastInput = now;
    dirty = true;
    msgUntil = 0;
  }

  switch (mode) {
    case Mode::View:
      if (d) page = ((page + d) % PAGES + PAGES) % PAGES;
      if (b == BtnEvent::Click) page = (page + 1) % PAGES;
      if (b == BtnEvent::Long) {
        mode = Mode::Menu;
        sel = 0;
      }
      break;

    case Mode::Menu:
      if (d) sel = ((sel + d) % (DS_COUNT + 1) + DS_COUNT + 1) % (DS_COUNT + 1);
      if (b == BtnEvent::Click) {
        if (sel == DS_COUNT) {
          mode = Mode::View;
        } else {
          editOffset = cfg.ds[sel].offset;
          mode = Mode::Edit;
        }
      }
      if (b == BtnEvent::Long) mode = Mode::View;
      break;

    case Mode::Edit:
      if (d) {
        float step = abs(d) >= 3 ? 0.5f : 0.1f;
        editOffset = roundf((editOffset + d * step) * 10) / 10.0f;
        editOffset = constrain(editOffset, -OFFSET_LIMIT, OFFSET_LIMIT);
      }
      if (b == BtnEvent::Click) {
        cfg.ds[sel].offset = editOffset;
        bool ok = configSave();
        mode = Mode::Menu;
        uiMessage(ok ? "Saved" : "Save FAILED");
      }
      if (b == BtnEvent::Long) {
        mode = Mode::Menu;
        uiMessage("Cancelled");
      }
      break;
  }

  if (mode != Mode::View && now - lastInput > 30000) {
    mode = Mode::View;
    dirty = true;
  }

  if (dirty || now - lastDraw > 500) {
    dirty = false;
    lastDraw = now;
    draw();
  }
}
