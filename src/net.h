#pragma once
#include <Arduino.h>

void netBegin(bool forceAp);
void netLoop();
bool netStaConnected();
bool netApActive();
String netApSsid();
String netIp();  // STA IP if connected, otherwise AP IP
uint8_t netLastReason();  // last STA disconnect reason (0 = none)
const char* netStaProblem(bool shortText);  // "" if nothing to report
