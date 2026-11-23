#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void uiBegin();
void uiLoop();
// Short message on the LCD (e.g. "Rebooting...").
void uiMessage(const char* l1, const char* l2 = "", uint32_t ms = 1200);
uint8_t uiLcdAddr();  // 0 if no LCD found

// Web display mirror: current LCD contents (what is on the glass right now).
void uiScreenToJson(JsonObject o);
// Virtual encoder from the web: turn = detents (+/-), press: 0 none, 1 click, 2 long.
void uiVirtualInput(int turn, int press);
// Shows raw character ROM codes on the LCD for 30 s (page 0: A0-DF, 1: E0-FF, 80-9F).
void uiCharsetTest(int page);
