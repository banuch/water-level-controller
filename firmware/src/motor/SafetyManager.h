// Firmware safety supervision layer.
//
// This is the SECOND line of defence. The first line is hardware: the
// independent high-high (100 %) cutoff relay, the thermal overload relay
// 95-96 contact and the emergency stop are wired in series with the
// contactor coil and open it without any firmware involvement.
//
// SafetyManager detects those hardware trips (via status inputs), supervises
// the contactor with its auxiliary feedback contact, enforces the maximum
// runtime and detects a pump that runs without the tank level rising.
#pragma once

#include "core/Types.h"

namespace wintek {

struct SafetyConfig {
  bool feedbackInstalled = true;
  bool overloadInstalled = true;
  uint32_t feedbackTimeoutMs = 2000;
  uint32_t maxRuntimeMs = 3600000;
  uint32_t noRiseTimeoutMs = 1200000;  // 0 = disabled
};

struct SafetyInputs {
  uint32_t nowMs = 0;
  LevelReading level;
  bool hhTripped = false;        // independent overflow cutoff active
  bool overloadTripped = false;  // thermal overload relay tripped (97-98)
  bool estopActive = false;      // emergency stop pressed / circuit open
  bool contactorClosed = false;  // contactor auxiliary NO contact
};

struct SafetyVerdict {
  FaultCode equipmentFault = FaultCode::NONE;  // -> FAULT_LOCKOUT (latched)
  bool overflow = false;                       // -> OVERFLOW_LOCKOUT (latched)
  bool sensorFault = false;                    // -> SENSOR_FAULT (auto recovery)
  FaultCode sensorCode = FaultCode::NONE;
};

class SafetyManager {
 public:
  void configure(const SafetyConfig& cfg) { cfg_ = cfg; }
  const SafetyConfig& config() const { return cfg_; }

  // motorCommanded: the output currently applied to the contactor driver.
  SafetyVerdict evaluate(const SafetyInputs& in, bool motorCommanded);

  // Live interlock checks used for start permission and fault reset.
  bool overloadActive(const SafetyInputs& in) const { return cfg_.overloadInstalled && in.overloadTripped; }
  bool contactorClosedWhileOff(const SafetyInputs& in, bool motorCommanded) const {
    return cfg_.feedbackInstalled && !motorCommanded && in.contactorClosed;
  }
  bool feedbackConfirmsOff(const SafetyInputs& in) const { return !cfg_.feedbackInstalled || !in.contactorClosed; }

  uint32_t currentRunMs(uint32_t nowMs) const { return running_ ? elapsedMs(nowMs, runStartMs_) : 0; }
  void resetSupervision(uint32_t nowMs);

 private:
  SafetyConfig cfg_;
  bool initialized_ = false;
  bool lastCommanded_ = false;
  uint32_t commandChangedMs_ = 0;
  bool running_ = false;
  bool pulledIn_ = false;
  uint32_t runStartMs_ = 0;
  uint8_t highestWet_ = 0;
  uint32_t lastRiseMs_ = 0;
  bool feedbackLast_ = false;
  uint32_t feedbackChangedMs_ = 0;
};

}  // namespace wintek
