#include "encoder.h"
#include "config.h"

static volatile int32_t encRaw = 0;
static volatile uint32_t edgesA = 0, edgesB = 0, invalid = 0;
static volatile uint8_t encState = 0;
static int8_t pinA = -1, pinB = -1, pinBtn = -1;
static int32_t lastDetent = 0;

// Quadrature transition table: index = (prev << 2) | cur  (used for 1 step per detent)
static const int8_t QDEC[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

// State machines (after Ben Buxton's rotary decoder): a step is counted only after a
// complete, valid A/B sequence, so contact bounce or a single line twitching
// (e.g. noise when the button is pressed) never produces steps.
// Input = (B << 1) | A, rest position 11 (pull-ups).
static constexpr uint8_t DIR_CW = 0x10, DIR_CCW = 0x20;
// Full step: 4 transitions per detent, emits at 11.
static const uint8_t FULL[7][4] = {
    {0x0, 0x2, 0x4, 0x0},           // start
    {0x3, 0x0, 0x1, 0x0 | DIR_CW},  // cw final
    {0x3, 0x2, 0x0, 0x0},           // cw begin
    {0x3, 0x2, 0x1, 0x0},           // cw next
    {0x6, 0x0, 0x4, 0x0},           // ccw begin
    {0x6, 0x5, 0x0, 0x0 | DIR_CCW}, // ccw final
    {0x6, 0x5, 0x4, 0x0},           // ccw next
};
// Half step: 2 transitions per detent, emits at 00 and 11.
static const uint8_t HALF[6][4] = {
    {0x3, 0x2, 0x1, 0x0},            // start (00)
    {0x3 | DIR_CCW, 0x0, 0x1, 0x0},  // ccw begin
    {0x3 | DIR_CW, 0x2, 0x0, 0x0},   // cw begin
    {0x3, 0x5, 0x4, 0x0},            // start (11)
    {0x3, 0x3, 0x4, 0x0 | DIR_CW},   // cw begin (from 11)
    {0x3, 0x5, 0x3, 0x0 | DIR_CCW},  // ccw begin (from 11)
};
static volatile uint8_t fsmState = 0;
static volatile int32_t encDetents = 0;  // steps counted by the state machine

static volatile uint8_t lastAB = 0;

static void IRAM_ATTR encIsr() {
  uint8_t s = (digitalRead(pinA) << 1) | digitalRead(pinB);
  uint8_t ch = s ^ lastAB;
  lastAB = s;
  if (ch & 2) edgesA++;
  if (ch & 1) edgesB++;
  if (ch == 3) invalid++;  // both lines changed at once: missed step or noise
  encState = ((encState << 2) | s) & 0x0F;
  encRaw += QDEC[encState];

  // s = (A << 1) | B  ->  state machine input (B << 1) | A
  uint8_t in = ((s & 1) << 1) | (s >> 1);
  uint8_t idx = fsmState & 0x0F, st;
  if (cfg.encStepsPerDetent >= 4) {
    st = FULL[idx > 6 ? 0 : idx][in];
  } else {
    st = HALF[idx > 5 ? 3 : idx][in];  // setting may change at runtime
  }
  fsmState = st;
  if (st & DIR_CW) encDetents++;
  else if (st & DIR_CCW) encDetents--;
}

// ---- event log ----
struct EncEvent { uint32_t seq, t; char type; uint8_t src; int16_t value; };
static EncEvent evlog[32];
static uint32_t evSeq = 0;

void encoderLog(char type, uint8_t src, int16_t value) {
  EncEvent& e = evlog[evSeq % 32];
  e = {++evSeq, millis(), type, src, value};
}

void encoderDiagToJson(JsonObject o, uint32_t since) {
  JsonObject p = o["pins"].to<JsonObject>();
  p["a"] = pinA;
  p["b"] = pinB;
  p["btn"] = pinBtn;
  JsonObject lv = o["level"].to<JsonObject>();
  if (pinA >= 0) lv["a"] = digitalRead(pinA);
  if (pinB >= 0) lv["b"] = digitalRead(pinB);
  if (pinBtn >= 0) lv["btn"] = digitalRead(pinBtn);
  JsonObject pu = o["pullup"].to<JsonObject>();  // internal pull-up available?
  pu["a"] = pinA >= 0 && pinA < 34;
  pu["b"] = pinB >= 0 && pinB < 34;
  pu["btn"] = pinBtn >= 0 && pinBtn < 34;
  o["raw"] = encRaw;
  o["steps"] = encDetents;
  o["edgesA"] = edgesA;
  o["edgesB"] = edgesB;
  o["invalid"] = invalid;
  o["stepsPerDetent"] = cfg.encStepsPerDetent;
  o["seq"] = evSeq;
  JsonArray ev = o["events"].to<JsonArray>();
  uint32_t first = evSeq > 32 ? evSeq - 31 : 1;
  if (since + 1 > first) first = since + 1;
  uint32_t now = millis();
  for (uint32_t s = first; s <= evSeq; s++) {
    const EncEvent& e = evlog[(s - 1) % 32];
    JsonObject j = ev.add<JsonObject>();
    j["seq"] = e.seq;
    j["ago"] = now - e.t;
    j["type"] = String(e.type);
    j["src"] = e.src ? "web" : "hw";
    if (e.value) j["v"] = e.value;
  }
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
    lastAB = encState;
    fsmState = cfg.encStepsPerDetent >= 4 ? 0 : 3;  // both machines start at rest (11)
    attachInterrupt(digitalPinToInterrupt(pinA), encIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(pinB), encIsr, CHANGE);
  }
  if (pinBtn >= 0) setupInput(pinBtn);
}


int encoderDelta() {
  if (pinA < 0 || pinB < 0) return 0;
  int32_t det = cfg.encStepsPerDetent == 1 ? encRaw : encDetents;
  int d = det - lastDetent;
  lastDetent = det;
  if (cfg.encReverse) d = -d;
  if (d) encoderLog(d > 0 ? 'R' : 'L', 0, abs(d));
  return d;
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
    encoderLog(down ? 'D' : 'U', 0);
    if (down) {
      pressedAt = now;
      longFired = false;
    } else if (!longFired) {
      encoderLog('C', 0);
      return BtnEvent::Click;
    }
  } else if (down && !longFired && now - pressedAt >= 800) {
    longFired = true;
    encoderLog('H', 0);
    return BtnEvent::Long;
  }
  return BtnEvent::None;
}
