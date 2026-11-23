#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void netBegin(bool forceAp);
void netLoop();
bool netStaConnected();
bool netApActive();
String netApSsid();
String netIp();  // STA IP if connected, otherwise AP IP
uint8_t netLastReason();  // last STA disconnect reason (0 = none)
const char* netStaProblem(bool shortText);  // "" if nothing to report
// Scans for networks (any state). Returns raw count or <0 on failure.
int netScan(JsonArray out);
// Non-blocking scan for the web UI: 1 = still scanning (poll again), 0 = result in out,
// -1 = failed. Starts a new scan when idle.
int netScanAsync(JsonArray out);
