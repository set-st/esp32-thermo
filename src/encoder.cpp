#include "encoder.h"
#include "config.h"

static volatile int32_t encRaw = 0;
static volatile uint8_t encState = 0;
static int8_t pinA = -1, pinB = -1, pinBtn = -1;
static int32_t lastDetent = 0;

// Quadrature transition table: index = (prev << 2) | cur
static const int8_t QDEC[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void IRAM_ATTR encIsr() {
  uint8_t s = (digitalRead(pinA) << 1) | digitalRead(pinB);
  encState = ((encState << 2) | s) & 0x0F;
  encRaw += QDEC[encState];
}

static void setupInput(int8_t pin) {
  // GPIO34..39 have no internal pull-ups: external resistors required.
  pinMode(pin, pin >= 34 ? INPUT : INPUT_PULLUP);
}

void encoderBegin() {
  pinA = cfg.pinEncA;
  pinB = cfg.pinEncB;
  pinBtn = cfg.pinEncBtn;
  if (pinA >= 0 && pinB >= 0) {
    setupInput(pinA);
    setupInput(pinB);
    encState = (digitalRead(pinA) << 1) | digitalRead(pinB);
    attachInterrupt(digitalPinToInterrupt(pinA), encIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(pinB), encIsr, CHANGE);
  }
  if (pinBtn >= 0) setupInput(pinBtn);
}

static int32_t floorDiv(int32_t a, int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

int encoderDelta() {
  if (pinA < 0 || pinB < 0) return 0;
  int32_t det = floorDiv(encRaw, cfg.encStepsPerDetent ? cfg.encStepsPerDetent : 4);
  int d = det - lastDetent;
  lastDetent = det;
  return cfg.encReverse ? -d : d;
}

bool encoderButtonDown() { return pinBtn >= 0 && digitalRead(pinBtn) == LOW; }

BtnEvent encoderButton() {
  static bool stable = false, lastRead = false, longFired = false;
  static uint32_t changedAt = 0, pressedAt = 0;
  if (pinBtn < 0) return BtnEvent::None;

  uint32_t now = millis();
  bool down = digitalRead(pinBtn) == LOW;
  if (down != lastRead) {
    lastRead = down;
    changedAt = now;
  }
  if (now - changedAt < 30) return BtnEvent::None;  // debounce

  if (down != stable) {
    stable = down;
    if (down) {
      pressedAt = now;
      longFired = false;
    } else if (!longFired) {
      return BtnEvent::Click;
    }
  } else if (down && !longFired && now - pressedAt >= 800) {
    longFired = true;
    return BtnEvent::Long;
  }
  return BtnEvent::None;
}
