#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Firmware updates from GitHub Releases (GH_REPO).
// Only images signed by CI (ECDSA P-256 over SHA-256, see keys/) are installed.

void inetUpdateLoop();       // daily auto-check, reboot after a finished install
bool inetUpdateCheck();      // blocking check of the latest release
bool inetUpdateInstall(String& err);  // starts the background install
int inetUpdateProgress();    // 0..100 while downloading, 101 verifying, -1 idle
void inetUpdateToJson(JsonObject o);
