#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "sensors.h"
#include "encoder.h"
#include "ui.h"
#include "net.h"
#include "web.h"
#include "inet_update.h"

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\nThermoHub %s\n", FW_VERSION);

  configLoad();
  Serial.printf("[cfg] SDA=%d SCL=%d 1W=%d ENC=%d/%d BTN=%d\n", cfg.pinSda, cfg.pinScl,
                cfg.pinOneWire, cfg.pinEncA, cfg.pinEncB, cfg.pinEncBtn);

  Wire.begin(cfg.pinSda, cfg.pinScl, 100000);

  uiBegin();  // LCD + encoder
  delay(20);
  bool forceAp = encoderButtonDown();
  if (forceAp) {
    Serial.println("[net] button held at boot -> forcing AP mode");
    uiMessage("Setup AP mode", "192.168.4.1");
  }

  sensorsBegin();
  netBegin(forceAp);
  webBegin();
}

// Tiny serial console for debugging: "scan", "status", "reboot".
static void serialConsole() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (line.length() < 64) line += c;
      continue;
    }
    line.trim();
    if (line == "scan") {
      JsonDocument d;
      netScan(d.to<JsonArray>());
      serializeJson(d, Serial);
      Serial.println();
    } else if (line == "status") {
      Serial.printf("ssid='%s' connected=%d ap=%d ip=%s reason=%u heap=%u\n", cfg.ssid,
                    netStaConnected(), netApActive(), netIp().c_str(), netLastReason(),
                    ESP.getFreeHeap());
    } else if (line == "reboot") {
      ESP.restart();
    } else if (line.length()) {
      Serial.println("commands: scan, status, reboot");
    }
    line = "";
  }
}

void loop() {
  serialConsole();
  sensorsLoop();
  netLoop();
  webLoop();
  uiLoop();
  inetUpdateLoop();
  delay(2);
}
