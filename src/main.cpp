#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "sensors.h"
#include "encoder.h"
#include "ui.h"
#include "net.h"
#include "web.h"

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

void loop() {
  sensorsLoop();
  netLoop();
  webLoop();
  uiLoop();
  delay(2);
}
