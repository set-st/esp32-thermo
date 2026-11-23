#pragma once
#include <Arduino.h>

enum class BtnEvent { None, Click, Long };

void encoderBegin();
// Detents turned since the last call (sign = direction).
int encoderDelta();
BtnEvent encoderButton();
// True if the button is held right now (used at boot to force AP mode).
bool encoderButtonDown();
