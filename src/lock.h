#pragma once
#include <Arduino.h>

// The main loop (core 1: sensors, LCD, encoder, Wi-Fi) and the async web server
// (core 0) share hardware buses (I2C, 1-Wire) and state (cfg, readings, LCD buffer).
// Everything that touches them runs under this recursive lock.
void stateLockInit();
void stateLock();
void stateUnlock();

struct StateGuard {
  StateGuard() { stateLock(); }
  ~StateGuard() { stateUnlock(); }
  StateGuard(const StateGuard&) = delete;
  StateGuard& operator=(const StateGuard&) = delete;
};
