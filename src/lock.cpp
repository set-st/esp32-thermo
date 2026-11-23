#include "lock.h"
#include <freertos/semphr.h>

static SemaphoreHandle_t mtx = nullptr;

void stateLockInit() {
  if (!mtx) mtx = xSemaphoreCreateRecursiveMutex();
}

void stateLock() { xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }

void stateUnlock() { xSemaphoreGiveRecursive(mtx); }
