#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class BtnEvent { None, Click, Long };

void encoderBegin();
// Detents turned since the last call (sign = direction).
int encoderDelta();
BtnEvent encoderButton();
// True if the button is held right now (used at boot to force AP mode).
bool encoderButtonDown();

// ---- diagnostics ----
// Event log (ring buffer). type: 'R' turn right, 'L' turn left, 'D' button down,
// 'U' button up, 'C' click, 'H' long press. src: 0 = physical encoder, 1 = web.
void encoderLog(char type, uint8_t src, int16_t value = 0);
// Live pin levels, counters and events with seq > since.
void encoderDiagToJson(JsonObject o, uint32_t since);
