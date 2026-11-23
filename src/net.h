#pragma once
#include <Arduino.h>

void netBegin(bool forceAp);
void netLoop();
bool netStaConnected();
bool netApActive();
String netApSsid();
String netIp();  // STA IP if connected, otherwise AP IP
