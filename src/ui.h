#pragma once
#include <Arduino.h>

void uiBegin();
void uiLoop();
// Short message on the LCD (e.g. "Rebooting...").
void uiMessage(const char* l1, const char* l2 = "", uint32_t ms = 1200);
uint8_t uiLcdAddr();  // 0 if no LCD found
