// LCD pages and encoder menu.
//
// View mode:  rotate = switch page, click = next page, long press = calibration menu
//   page 1: four DS18B20 temperatures (4-row LCD: one per line with names)
//   page 2: AHT20 temperature/humidity + BMP280 pressure/temperature
//   page 3: (4-row LCD only) overview: everything on one screen
//   last:   network info (IP / SSID / RSSI / uptime or AP name)
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
static int sel = 0;  // 0..3 = T1..T4, 4 = exit
static float editOffset = 0;
static uint32_t lastInput = 0, lastDraw = 0, msgUntil = 0;
static bool dirty = true;

// ---- character encoding ------------------------------------------------------
// All LCD texts are written in UTF-8 Russian and converted to the display's
// character ROM:
//   charset 1 (default): HD44780 clone with Cyrillic ROM (Б=0xA0, Г=0xA1, ...);
//            letters that look like Latin ones (А, В, Е, К, М, Н, О, Р, С, Т, Х ...)
//            use the Latin glyphs, exactly like Cyrillic LCD fonts are designed.
//   charset 0: standard ROM A00 without Cyrillic -> transliteration.
static constexpr uint8_t DEG_A00 = 0xDF, DEG_CYR = 0xEF;
#define DEG ((char)(cfg.lcdCharset == 1 ? DEG_CYR : DEG_A00))

// U+0410..U+042F (А..Я) and U+0430..U+044F (а..я) -> ROM code
static const uint8_t CYR_UP[32] = {'A',  0xA0, 'B',  0xA1, 0xE0, 'E',  0xA3, 0xA4, 0xA5, 0xA6, 'K',
                                   0xA7, 'M',  'H',  'O',  0xA8, 'P',  'C',  'T',  0xA9, 0xAA, 'X',
                                   0xE1, 0xAB, 0xAC, 0xE2, 0xAD, 0xAE, 0xC4, 0xAF, 0xB0, 0xB1};
static const uint8_t CYR_LO[32] = {'a',  0xB2, 0xB3, 0xB4, 0xE3, 'e',  0xB6, 0xB7, 0xB8, 0xB9, 0xBA,
                                   0xBB, 0xBC, 0xBD, 'o',  0xBE, 'p',  'c',  0xBF, 'y',  0xE4, 'x',
                                   0xE5, 0xC0, 0xC1, 0xE6, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7};
static constexpr uint8_t CYR_YO_UP = 0xA2, CYR_YO_LO = 0xB5;

// Ukrainian letters missing in the Cyrillic ROM are drawn into CGRAM slots 1..6
// (slot 0 is avoided: code 0x00 would terminate C strings). І/і use Latin I/i.
struct UkrGlyph { uint16_t cp; uint8_t rows[8]; };
static const UkrGlyph UKR[] = {
    {0x404, {0b01110, 0b10001, 0b10000, 0b11110, 0b10000, 0b10001, 0b01110, 0}},  // Є
    {0x454, {0, 0, 0b01110, 0b10000, 0b11100, 0b10000, 0b01110, 0}},              // є
    {0x407, {0b01010, 0, 0b01110, 0b00100, 0b00100, 0b00100, 0b01110, 0}},        // Ї
    {0x457, {0b01010, 0, 0b01100, 0b00100, 0b00100, 0b00100, 0b01110, 0}},        // ї
    {0x490, {0b00001, 0b11111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0}},  // Ґ
    {0x491, {0, 0b00001, 0b11111, 0b10000, 0b10000, 0b10000, 0b10000, 0}},        // ґ
};
static constexpr int UKR_COUNT = sizeof(UKR) / sizeof(UKR[0]);
static int ukrSlot(uint32_t cp) {
  for (int i = 0; i < UKR_COUNT; i++)
    if (UKR[i].cp == cp) return i + 1;
  return 0;
}

// Ukrainian national transliteration (charset 0 / no Cyrillic ROM)

static const char* const TRANSLIT[32] = {"a", "b", "v", "h", "d", "e", "zh", "z", "y", "i", "k",
                                         "l", "m", "n", "o", "p", "r", "s", "t", "u", "f", "kh",
                                         "ts", "ch", "sh", "shch", "", "y", "", "e", "yu", "ya"};
static const char* translitExtra(uint32_t cp) {
  switch (cp) {
    case 0x404: case 0x454: return "ye";
    case 0x406: case 0x456: return "i";
    case 0x407: case 0x457: return "yi";
    case 0x490: case 0x491: return "g";
    case 0x401: case 0x451: return "yo";
    default: return nullptr;
  }
}

// UTF-8 -> display ROM bytes (one byte per character on the LCD).
static void lcdText(char* out, size_t n, const char* in) {
  bool cyr = cfg.lcdCharset == 1;
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && o + 1 < n) {
    uint32_t cp = *p;
    int len = 1;
    if (cp >= 0xC0 && cp < 0xE0 && (p[1] & 0xC0) == 0x80) {
      cp = ((cp & 0x1F) << 6) | (p[1] & 0x3F);
      len = 2;
    } else if (cp >= 0xE0 && cp < 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      cp = ((cp & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      len = 3;
    } else if (cp >= 0xF0) {  // emoji etc.: unsupported
      len = 4;
      cp = '?';
    }
    for (int k = 0; k < len && *p; k++) p++;

    if (cp == 0xB0) { out[o++] = DEG; continue; }  // °
    if (cp == 0x2BC || cp == 0x2019 || cp == 0x2018) cp = '\'';  // Ukrainian apostrophe
    if (cp == 0x2013 || cp == 0x2014) cp = '-';
    bool up = cp >= 0x410 && cp <= 0x42F, lo = cp >= 0x430 && cp <= 0x44F;
    bool yo = cp == 0x401 || cp == 0x451;
    if (cyr) {
      if (up) out[o++] = (char)CYR_UP[cp - 0x410];
      else if (lo) out[o++] = (char)CYR_LO[cp - 0x430];
      else if (yo) out[o++] = (char)(cp == 0x401 ? CYR_YO_UP : CYR_YO_LO);
      else if (cp == 0x406) out[o++] = 'I';
      else if (cp == 0x456) out[o++] = 'i';
      else if (int slot = ukrSlot(cp)) out[o++] = (char)slot;
      else out[o++] = (cp >= 0x20 && cp < 0x7F) ? (char)cp : '?';
      continue;
    }
    const char* rep = up ? TRANSLIT[cp - 0x410] : lo ? TRANSLIT[cp - 0x430] : translitExtra(cp);
    bool capital = up || (cp >= 0x400 && cp < 0x410) || cp == 0x490;
    if (rep) {
      for (int k = 0; rep[k] && o + 1 < n; k++) out[o++] = (k == 0 && capital) ? toupper(rep[k]) : rep[k];
    } else {
      out[o++] = (cp >= 0x20 && cp < 0x7F) ? (char)cp : '?';
    }
  }
  out[o] = 0;
}

// Reverse mapping for the web mirror: ROM byte -> UTF-8 ("" if unknown).
static const char* romToUtf8(uint8_t b) {
  static char buf[4];
  if (b == (uint8_t)DEG) return "°";
  if (b >= 0x20 && b < 0x7F) {
    buf[0] = b;
    buf[1] = 0;
    return buf;
  }
  uint32_t ukr = (cfg.lcdCharset == 1 && b >= 1 && b <= UKR_COUNT) ? UKR[b - 1].cp : 0;
  if (cfg.lcdCharset == 1 && (b >= 0x80 || ukr)) {
    uint32_t cp = ukr;
    if (b == CYR_YO_UP) cp = 0x401;
    else if (b == CYR_YO_LO) cp = 0x451;
    for (int i = 0; i < 32 && !cp; i++) {
      if (CYR_UP[i] == b && b != 0xC4) cp = 0x410 + i;  // 0xC4 is "ь"
      else if (CYR_LO[i] == b) cp = 0x430 + i;
    }
    if (cp) {
      buf[0] = 0xC0 | (cp >> 6);
      buf[1] = 0x80 | (cp & 0x3F);
      buf[2] = 0;
      return buf;
    }
  }
  return "?";
}

// Converted copy of a UTF-8 literal/format, valid until 8 more L() calls.
static const char* L(const char* utf8) {
  static char buf[8][64];
  static uint8_t idx = 0;
  idx = (idx + 1) % 8;
  lcdText(buf[idx], sizeof(buf[idx]), utf8);
  return buf[idx];
}

// Sensor name as shown on the LCD.
static const char* lcdName(int i) {
  static char buf[DS_COUNT][64];
  lcdText(buf[i], sizeof(buf[i]), cfg.ds[i].name);
  return buf[i];
}

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

// ---- web mirror / virtual encoder -------------------------------------------

static int virtDelta = 0;
static BtnEvent virtBtn = BtnEvent::None;

void uiVirtualInput(int turn, int press) {
  virtDelta += constrain(turn, -10, 10);
  if (press == 1) virtBtn = BtnEvent::Click;
  else if (press == 2) virtBtn = BtnEvent::Long;
}

void uiScreenToJson(JsonObject o) {
  o["cols"] = cfg.lcdCols;
  o["rows"] = cfg.lcdRows;
  o["present"] = lcd != nullptr;
  JsonArray lines = o["lines"].to<JsonArray>();
  for (int r = 0; r < cfg.lcdRows && r < 4; r++) {
    String line;
    for (int c = 0; c < cfg.lcdCols && c < 40; c++) {
      uint8_t ch = (uint8_t)shown[r][c];
      line += romToUtf8(ch ? ch : ' ');
    }
    lines.add(line);
  }
}

static bool big() { return cfg.lcdRows >= 4; }

static uint8_t frameRows = 0;  // rows written during the current frame
static bool frameDemo = false;  // put demo marker 'D' into the last column of row 0
static bool frameScroll = false;  // some field is scrolling -> redraw more often

static void putLine(int row, const char* text) {
  // The frame is always rendered into `shown` (used by the web display mirror),
  // the physical LCD is only touched if present.
  if (row >= cfg.lcdRows || row >= 4) return;
  frameRows |= 1 << row;
  char buf[41];
  int cols = min<int>(cfg.lcdCols, 40);
  int n = strlen(text);
  for (int i = 0; i < cols; i++) buf[i] = i < n ? text[i] : ' ';
  buf[cols] = 0;
  if (row == 0 && frameDemo) buf[cols - 1] = 'D';
  if (strcmp(buf, shown[row]) == 0) return;  // avoid flicker
  strcpy(shown[row], buf);
  if (!lcd) return;
  lcd->setCursor(0, row);
  lcd->print(buf);
}

static void frameBegin() {
  frameRows = 0;
  frameDemo = false;
  frameScroll = false;
}
static void frameEnd() {
  for (int r = 0; r < cfg.lcdRows && r < 4; r++)
    if (!(frameRows & (1 << r))) putLine(r, "");
}

// Shows up to 4 lines for `ms` milliseconds. A 2-line message on a 4-row LCD
// is centred vertically; extra lines on a 2-row LCD are dropped.
static void showMessage(const char* const* lines, int n, uint32_t ms) {
  frameBegin();
  int r0 = (big() && n <= 2) ? 1 : 0;
  for (int i = 0; i < n; i++) putLine(r0 + i, lines[i]);
  frameEnd();
  msgUntil = millis() + ms;
}

void uiMessage(const char* l1, const char* l2, uint32_t ms) {
  const char* lines[] = {L(l1), L(l2)};
  showMessage(lines, 2, ms);
}

// "<prefix><value>" if it fits the display width, otherwise just the value.
static void fit(char* out, size_t n, const char* prefix, const char* value) {
  if (strlen(prefix) + strlen(value) <= cfg.lcdCols) snprintf(out, n, "%s%s", prefix, value);
  else snprintf(out, n, "%s", value);
}

static bool apHasPass() { return strlen(cfg.apPass) >= 8; }

static void fmtApPass(char* out, size_t n) {
  if (apHasPass()) fit(out, n, "Pass ", cfg.apPass);
  else snprintf(out, n, "No password");
}

static int pageCount();
static bool jumpToNetPage = false;

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
  if (key[0] == 'S') {
    {
      String ipLine = "IP " + String(key.c_str() + 1);
      uiMessage("WiFi connected", ipLine.c_str(), 5000);
    }
    return;
  }
  // Setup access point: show everything needed to connect, then stay on the network page.
  char ssid[41], pass[41], ip[41];
  String apSsid = netApSsid();
  fit(ssid, sizeof(ssid), "WiFi ", apSsid.c_str());
  fmtApPass(pass, sizeof(pass));
  fit(ip, sizeof(ip), "Open ", key.c_str() + 1);
  const char* lines[] = {"WiFi setup mode", ssid, pass, ip};
  if (big()) showMessage(lines, 4, 8000);
  else showMessage(lines + 1, 2, 8000);
  jumpToNetPage = true;
}

static void fmtTemp(char* out, size_t n, float t) {
  if (isnan(t)) snprintf(out, n, "--.-");
  else snprintf(out, n, "%.1f", t);
}

// ---- view pages ------------------------------------------------------------

enum class Page { Ds, I2c, Overview, Net };
// 2-row LCD: DS, I2C, Net.   4-row LCD: DS (with names), I2C, Overview, Net.
static Page pageAt(int i) {
  static const Page small[] = {Page::Ds, Page::I2c, Page::Net};
  static const Page large[] = {Page::Ds, Page::I2c, Page::Overview, Page::Net};
  return big() ? large[i % 4] : small[i % 3];
}
static int pageCount() { return big() ? 4 : 3; }

// Fixed-width value: "%5.1f" or a placeholder of the same width, so decimal
// points line up in every row ("-12.3", " 22.1", " --.-").
// Fits `text` into a field of `width` chars. Longer text scrolls inside the
// field (marquee): pause at the start, shift one char per step, pause at the
// end, repeat. The rest of the line stays still. Result is padded to width.
static constexpr uint32_t SCROLL_STEP_MS = 450, SCROLL_PAUSE_MS = 1800;
static void marquee(char* out, size_t n, const char* text, int width) {
  int len = strlen(text);
  if (width <= 0) {
    out[0] = 0;
    return;
  }
  if (len <= width) {
    snprintf(out, n, "%-*s", width, text);
    return;
  }
  frameScroll = true;
  uint32_t span = len - width;
  uint32_t cycle = 2 * SCROLL_PAUSE_MS + span * SCROLL_STEP_MS;
  uint32_t t = millis() % cycle;
  uint32_t pos = t < SCROLL_PAUSE_MS ? 0
               : t < SCROLL_PAUSE_MS + span * SCROLL_STEP_MS ? (t - SCROLL_PAUSE_MS) / SCROLL_STEP_MS
               : span;
  snprintf(out, n, "%.*s", width, text + pos);
}

static void val5(char* out, size_t n, float v) {
  if (isnan(v)) snprintf(out, n, " --.-");
  else snprintf(out, n, "%5.1f", v);
}

static void drawPageDs() {
  char l[41], a[12], b[12];
  if (big()) {
    // "1 Name      22.1°C D" — name column, value right-aligned at a fixed column,
    // last column reserved for the demo marker.
    int nameW = max(0, (int)cfg.lcdCols - 2 - 7 - 2);
    for (int i = 0; i < DS_COUNT; i++) {
      float t = dsTemp(i);
      if (!isnan(t)) snprintf(a, sizeof(a), "%5.1f%cC", t, DEG);
      else snprintf(a, sizeof(a), cfg.ds[i].addr[0] ? " --.-  " : "  ---  ");
      char name[41];
      marquee(name, sizeof(name), lcdName(i), nameW);
      snprintf(l, sizeof(l), "%d %s%s", i + 1, name, a);
      putLine(i, l);
    }
    return;
  }
  // "1: 22.1 2: 45.4 D"
  for (int row = 0; row < 2; row++) {
    val5(a, sizeof(a), dsTemp(row * 2));
    val5(b, sizeof(b), dsTemp(row * 2 + 1));
    snprintf(l, sizeof(l), "%d:%s %d:%s", row * 2 + 1, a, row * 2 + 2, b);
    putLine(row, l);
  }
}

static void drawPageI2c() {
  char l[41], v[12];
  if (big()) {
    // label (11) | value (decimal point always in column 15) | unit
    if (sens.ahtOk) {
      snprintf(l, sizeof(l), "%-11s%6.1f%cC", "Temperature", sens.ahtTemp, DEG);
      putLine(0, l);
      snprintf(l, sizeof(l), "%-11s%6.1f %%", "Humidity", sens.ahtHum);
      putLine(1, l);
    } else {
      putLine(0, "AHT20: no data");
    }
    if (sens.bmpOk) {
      if (cfg.pressUnit == 1)
        snprintf(l, sizeof(l), "%-11s%6.1f mm", "Pressure", hpaToMmHg(sens.bmpPress));
      else
        snprintf(l, sizeof(l), "%-10s%7.1fhPa", "Pressure", sens.bmpPress);
      putLine(2, l);
      snprintf(l, sizeof(l), "%-11s%6.1f%cC", "BMP temp", sens.bmpTemp, DEG);
      putLine(3, l);
    } else {
      putLine(2, "BMP280: no data");
    }
    return;
  }
  // 2 rows: "T: 22.1°C H: 63%" / "P:740.5mm  22.7°"
  if (sens.ahtOk) snprintf(l, sizeof(l), "T:%5.1f%cC H:%3.0f%%", sens.ahtTemp, DEG, sens.ahtHum);
  else snprintf(l, sizeof(l), "%s", "AHT20: no data");
  putLine(0, l);
  if (sens.bmpOk) {
    if (cfg.pressUnit == 1) snprintf(l, sizeof(l), "Press %5.1f mm", hpaToMmHg(sens.bmpPress));
    else snprintf(l, sizeof(l), "Press %4.0f hPa", sens.bmpPress);
  } else {
    snprintf(l, sizeof(l), "%s", "BMP280: no data");
  }
  putLine(1, l);
}

// Everything on one screen (4-row LCD only). Two 10-column halves,
// decimal points at columns 5 and 15 in every row:
//   1: 22.1°C 2: 45.4°C
//   3: 62.0°C 4:  4.5°C
//   T: 22.1°C H: 62.8 %
//   P:740.5mm T: 22.7°C
static void drawPageOverview() {
  char l[41], a[12], b[12];
  for (int row = 0; row < 2; row++) {
    val5(a, sizeof(a), dsTemp(row * 2));
    val5(b, sizeof(b), dsTemp(row * 2 + 1));
    snprintf(l, sizeof(l), "%d:%s%cC %d:%s%cC", row * 2 + 1, a, DEG, row * 2 + 2, b, DEG);
    putLine(row, l);
  }
  if (sens.ahtOk) snprintf(l, sizeof(l), "T:%5.1f%cC H:%5.1f %%", sens.ahtTemp, DEG, sens.ahtHum);
  else snprintf(l, sizeof(l), "%s", "AHT20: no data");
  putLine(2, l);
  if (sens.bmpOk) {
    if (cfg.pressUnit == 1) snprintf(l, sizeof(l), "Pressure: %5.1f mm", hpaToMmHg(sens.bmpPress));
    else snprintf(l, sizeof(l), "Pressure: %6.1f hPa", sens.bmpPress);
  } else {
    snprintf(l, sizeof(l), "%s", "BMP280: no data");
  }
  putLine(3, l);
}

static void drawPageNet() {
  char l[41];
  uint32_t up = millis() / 60000UL;
  if (netStaConnected()) {
    String ip = WiFi.localIP().toString();
    // "IP " prefix only if the full address still fits (xxx.xxx.xxx.xxx = 15 chars)
    snprintf(l, sizeof(l), ip.length() + 3 <= cfg.lcdCols ? "IP %s" : "%s", ip.c_str());
    putLine(0, l);
    if (big()) {
      fit(l, sizeof(l), "WiFi ", L(WiFi.SSID().c_str()));
      putLine(1, l);
      snprintf(l, sizeof(l), "Signal %d dBm", WiFi.RSSI());
      putLine(2, l);
      snprintf(l, sizeof(l), "Uptime %lud %02lu:%02lu", up / 1440, up / 60 % 24, up % 60);
      putLine(3, l);
    } else {
      snprintf(l, sizeof(l), "Signal %d, up %luh", WiFi.RSSI(), up / 60);
      putLine(1, l);
    }
  } else if (netApActive()) {
    String apSsid = netApSsid();
    if (big()) {
      fit(l, sizeof(l), "WiFi ", apSsid.c_str());
      putLine(0, l);
      fmtApPass(l, sizeof(l));
      putLine(1, l);
      fit(l, sizeof(l), "Open ", WiFi.softAPIP().toString().c_str());
      putLine(2, l);
      if (cfg.ssid[0]) {
        const char* p = netStaProblem(true);
        if (*p) snprintf(l, sizeof(l), "%s", p);
        else fit(l, sizeof(l), "Retry ", L(cfg.ssid));
        putLine(3, l);
      }
    } else {
      // 2 rows: IP on top, SSID and password alternate every 3 s below
      fit(l, sizeof(l), "AP ", WiFi.softAPIP().toString().c_str());
      putLine(0, l);
      if ((millis() / 3000) % 2 == 0) fit(l, sizeof(l), "", apSsid.c_str());
      else fmtApPass(l, sizeof(l));
      putLine(1, l);
    }
  } else {
    putLine(0, "WiFi connecting");
    putLine(1, L(cfg.ssid));
    if (big()) putLine(2, netStaProblem(true));
  }
}

// ---- calibration menu ----------------------------------------------------

static void fmtRawCorr(char* l, size_t n, int slot, float offset) {
  char a[8], b[8];
  if (sens.ds[slot].ok) {
    fmtTemp(a, sizeof(a), sens.ds[slot].raw);
    fmtTemp(b, sizeof(b), sens.ds[slot].raw + offset);
    snprintf(l, n, "%s>%s%cC", a, b, DEG);
  } else {
    snprintf(l, n, "%s", cfg.ds[slot].addr[0] ? "sensor offline" : "slot empty");
  }
}

// Sensor name unless it is just the default "T<n>".
static const char* customName(int i) {
  char def[4];
  snprintf(def, sizeof(def), "T%d", i + 1);
  return strcmp(cfg.ds[i].name, def) == 0 ? "" : lcdName(i);
}

static void drawMenu() {
  char l[41];
  if (big()) {
    putLine(0, "== Calibration ==");
    if (sel < DS_COUNT) {
      snprintf(l, sizeof(l), "> T%d %s", sel + 1, customName(sel));
      putLine(1, l);
      snprintf(l, sizeof(l), "  Offset %+.2f", cfg.ds[sel].offset);
      putLine(2, l);
      char rc[24];
      fmtRawCorr(rc, sizeof(rc), sel, cfg.ds[sel].offset);
      snprintf(l, sizeof(l), "  %s", rc);
      putLine(3, l);
    } else {
      putLine(1, "> Exit");
    }
    return;
  }
  if (sel < DS_COUNT) {
    snprintf(l, sizeof(l), "Calib>T%d %+.2f", sel + 1, cfg.ds[sel].offset);
    putLine(0, l);
    fmtRawCorr(l, sizeof(l), sel, cfg.ds[sel].offset);
    putLine(1, l);
  } else {
    putLine(0, "Calib>Exit");
  }
}

static void drawEdit() {
  char l[41];
  if (big()) {
    snprintf(l, sizeof(l), "T%d %s", sel + 1, customName(sel));
    putLine(0, l);
    snprintf(l, sizeof(l), "Offset: [%+.2f]", editOffset);
    putLine(1, l);
    fmtRawCorr(l, sizeof(l), sel, editOffset);
    putLine(2, l);
    putLine(3, cfg.lcdCols >= 20 ? "Click=save Hold=esc" : "Clk=OK Hold=esc");
    return;
  }
  snprintf(l, sizeof(l), "Set T%d: %+.2f", sel + 1, editOffset);
  putLine(0, l);
  fmtRawCorr(l, sizeof(l), sel, editOffset);
  putLine(1, l);
}

static void draw() {
  if (millis() < msgUntil) return;
  frameBegin();
  switch (mode) {
    case Mode::View:
      switch (pageAt(page)) {
        case Page::Ds: frameDemo = cfg.dsDemo; drawPageDs(); break;
        case Page::I2c: drawPageI2c(); break;
        case Page::Overview: drawPageOverview(); break;
        case Page::Net: drawPageNet(); break;
      }
      break;
    case Mode::Menu: drawMenu(); break;
    case Mode::Edit: drawEdit(); break;
  }
  frameEnd();
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
  for (int i = 0; i < UKR_COUNT; i++) lcd->createChar(i + 1, (uint8_t*)UKR[i].rows);
  lcd->clear();
  memset(shown, 0, sizeof(shown));
  uiMessage("ThermoHub", "v" FW_VERSION);
}

void uiLoop() {
  watchNetwork();
  if (jumpToNetPage) {
    jumpToNetPage = false;
    if (mode == Mode::View) page = pageCount() - 1;  // network page is always last
  }
  uint32_t now = millis();
  int d = encoderDelta() + virtDelta;
  BtnEvent b = encoderButton();
  if (b == BtnEvent::None) b = virtBtn;
  virtDelta = 0;
  virtBtn = BtnEvent::None;

  if (d || b != BtnEvent::None) {
    lastInput = now;
    dirty = true;
    msgUntil = 0;
  }

  switch (mode) {
    case Mode::View:
      if (d) page = ((page + d) % pageCount() + pageCount()) % pageCount();
      if (b == BtnEvent::Click) page = (page + 1) % pageCount();
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

  if (dirty || now - lastDraw > (frameScroll ? 150u : 500u)) {
    dirty = false;
    lastDraw = now;
    draw();
  }
}

// Character ROM test: rows of 16 raw codes with a hex label ("A0 ................").
// page 0: 0xA0..0xDF, page 1: 0xE0..0xFF + 0x80..0x9F. Shown for 30 s.
void uiCharsetTest(int page) {
  const uint8_t starts[2][4] = {{0xA0, 0xB0, 0xC0, 0xD0}, {0xE0, 0xF0, 0x80, 0x90}};
  if (page == 2) {
    // Alphabet check through the real encoder + degree sign candidates at the end of row 2.
    frameBegin();
    char l[41];
    putLine(0, L("АБВГҐДЕЄЖЗИІЇЙКЛМН"));
    snprintf(l, sizeof(l), "%s %c%c", L("ОПРСТУФХЦЧШЩЬЮЯ"), (char)0xDF, (char)0xEF);
    putLine(1, l);
    putLine(2, L("абвгґдеєжзиіїйклмн"));
    putLine(3, L("опрстуфхцчшщьюя 1-2"));
    frameEnd();
    msgUntil = millis() + 60000;
    return;
  }
  page = constrain(page, 0, 1);
  frameBegin();
  for (int r = 0; r < 4 && r < cfg.lcdRows; r++) {
    char l[41];
    uint8_t st = starts[page][r];
    int n = snprintf(l, sizeof(l), "%02X ", st);
    for (int k = 0; k < 16 && n < 40; k++) l[n++] = (char)(st + k);
    l[n] = 0;
    putLine(r, l);
  }
  frameEnd();
  msgUntil = millis() + 30000;
}
