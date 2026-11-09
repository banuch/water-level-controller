#include "hal/BoardIo.h"

#include <Arduino.h>
#include <Wire.h>

#include "app_config.h"
#include "board_config.h"
#include "diagnostics/Logger.h"

namespace wintek {

namespace {
constexpr uint8_t kRegInput0 = 0x00;
constexpr uint8_t kRegOutput1 = 0x03;
constexpr uint8_t kRegConfig0 = 0x06;  // 1 = input
constexpr uint8_t kRegConfig1 = 0x07;

inline bool readActive(uint8_t pin, bool activeLow) {
  const int v = digitalRead(pin);
  return activeLow ? (v == LOW) : (v == HIGH);
}

// 3-sample debounce for direct inputs.
inline void debounce(bool raw, bool& state, uint8_t& cnt) {
  if (raw == state) {
    cnt = 0;
  } else if (++cnt >= 3) {
    state = raw;
    cnt = 0;
  }
}
}  // namespace

void BoardIo::earlySafeInit() {
  // Drive LOW before switching to output so no glitch reaches the driver.
  digitalWrite(board::kPinContactorDrive, board::kContactorDriveActiveHigh ? LOW : HIGH);
  pinMode(board::kPinContactorDrive, OUTPUT);
  digitalWrite(board::kPinContactorDrive, board::kContactorDriveActiveHigh ? LOW : HIGH);
  // No probe excitation current until the front-end is initialised.
  digitalWrite(board::kPinExcCommon, LOW);
  digitalWrite(board::kPinExcBias, LOW);
  pinMode(board::kPinExcCommon, OUTPUT);
  pinMode(board::kPinExcBias, OUTPUT);
}

bool BoardIo::begin(bool panelInstalled) {
  panelInstalled_ = panelInstalled;
  setContactor(false);
  // External 10k pull-ups are fitted on the isolated inputs; the internal
  // pull-up is enabled as well so an unpopulated dev board reads "inactive".
  pinMode(board::kPinContactorFeedback, INPUT_PULLUP);
  pinMode(board::kPinOverload, INPUT_PULLUP);
  pinMode(board::kPinServiceButton, INPUT_PULLUP);
  fbState_ = readActive(board::kPinContactorFeedback, board::kContactorFeedbackActiveLow);
  olState_ = readActive(board::kPinOverload, board::kOverloadActiveLow);
  svcState_ = false;

  if (!panelInstalled_) return true;
  Wire.begin(board::kPinI2cSda, board::kPinI2cScl, board::kI2cClockHz);
  Wire.setTimeOut(10);
  panelPresent_ = writeExpanderReg(kRegConfig0, 0xFF) && writeExpanderReg(kRegConfig1, 0x00) &&
                  writeExpanderReg(kRegOutput1, 0x00);
  lastOutPort_ = 0x00;
  if (!panelPresent_) LOGE("io", "TCA9555 not responding at 0x%02X", board::kExpanderAddress);
  return panelPresent_;
}

void BoardIo::setContactor(bool on) {
  contactorOn_ = on;
  const bool level = board::kContactorDriveActiveHigh ? on : !on;
  digitalWrite(board::kPinContactorDrive, level ? HIGH : LOW);
}

void BoardIo::pollDirectInputs() {
  debounce(readActive(board::kPinContactorFeedback, board::kContactorFeedbackActiveLow), fbState_, fbCnt_);
  debounce(readActive(board::kPinOverload, board::kOverloadActiveLow), olState_, olCnt_);
  debounce(digitalRead(board::kPinServiceButton) == LOW, svcState_, svcCnt_);
}

bool BoardIo::writeExpanderReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(board::kExpanderAddress);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool BoardIo::readExpanderPort0(uint8_t& v) {
  Wire.beginTransmission(board::kExpanderAddress);
  Wire.write(kRegInput0);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<uint16_t>(board::kExpanderAddress), static_cast<uint8_t>(1)) != 1) return false;
  v = static_cast<uint8_t>(Wire.read());
  return true;
}

bool BoardIo::pollPanel(PanelInputs& out) {
  out = PanelInputs();
  if (!panelInstalled_) return false;
  uint8_t raw = 0;
  if (!readExpanderPort0(raw)) {
    if (panelFailures_ < 255) ++panelFailures_;
    if (panelFailures_ >= app::kPanelFaultReads) {
      panelPresent_ = false;
      return false;
    }
    raw = panelStable_;  // hold last good value through isolated glitches
  } else {
    if (!panelPresent_) {
      // Expander came back (e.g. after a brown-out): reinitialise it.
      panelPresent_ = writeExpanderReg(kRegConfig0, 0xFF) && writeExpanderReg(kRegConfig1, 0x00);
      lastOutPort_ = 0xFF;
    }
    panelFailures_ = 0;
  }
  // Whole-port debounce: the port value must be stable for 3 reads (60 ms).
  if (raw == panelLast_) {
    if (panelCnt_ < 3) ++panelCnt_;
  } else {
    panelLast_ = raw;
    panelCnt_ = 0;
  }
  if (panelCnt_ >= 2) panelStable_ = raw;

  const uint8_t p = panelStable_;
  auto low = [p](uint8_t bit) { return (p & (1U << bit)) == 0; };
  const bool a = low(board::exp::kInModeAuto);
  const bool m = low(board::exp::kInModeManual);
  if (a && m) {
    out.selector = LocalSwitch::UNKNOWN;  // both contacts closed: wiring fault
  } else if (a) {
    out.selector = LocalSwitch::AUTO;
  } else if (m) {
    out.selector = LocalSwitch::MANUAL;
  } else {
    out.selector = LocalSwitch::OFF;
  }
  out.startPressed = low(board::exp::kInStart);
  out.stopPressed = !low(board::exp::kInStopNc);
  out.hhTripped = low(board::exp::kInHighHigh);
  out.estopOpen = low(board::exp::kInEstop);
  out.hhCircuitOk = low(board::exp::kInHhCircuitOk);
  out.ok = true;
  return true;
}

void BoardIo::writePanel(const PanelOutputs& o) {
  if (!panelInstalled_ || !panelPresent_) return;
  uint8_t v = 0;
  if (o.ledWifi) v |= 1U << board::exp::kOutLedWifi;
  if (o.ledMotor) v |= 1U << board::exp::kOutLedMotor;
  if (o.ledFault) v |= 1U << board::exp::kOutLedFault;
  if (o.buzzer) v |= 1U << board::exp::kOutBuzzer;
  if (v == lastOutPort_) return;
  if (writeExpanderReg(kRegOutput1, v)) lastOutPort_ = v;
}

}  // namespace wintek
