#include "motor/SafetyManager.h"

namespace wintek {

void SafetyManager::resetSupervision(uint32_t nowMs) {
  initialized_ = true;
  lastCommanded_ = false;
  commandChangedMs_ = nowMs;
  running_ = false;
  pulledIn_ = false;
  runStartMs_ = nowMs;
  highestWet_ = 0;
  lastRiseMs_ = nowMs;
  feedbackChangedMs_ = nowMs;
}

SafetyVerdict SafetyManager::evaluate(const SafetyInputs& in, bool commanded) {
  const uint32_t now = in.nowMs;
  if (!initialized_) {
    resetSupervision(now);
    feedbackLast_ = in.contactorClosed;
  }

  // --- Track command edges -------------------------------------------------
  if (commanded != lastCommanded_) {
    lastCommanded_ = commanded;
    commandChangedMs_ = now;
    if (commanded) {
      running_ = true;
      pulledIn_ = false;
      runStartMs_ = now;
      highestWet_ = in.level.valid ? in.level.wetCount : 0;
      lastRiseMs_ = now;
    } else {
      running_ = false;
    }
  }
  if (in.contactorClosed != feedbackLast_) {
    feedbackLast_ = in.contactorClosed;
    feedbackChangedMs_ = now;
  }

  SafetyVerdict v;

  // --- Sensor validity -----------------------------------------------------
  if (!in.level.valid) {
    v.sensorFault = true;
    v.sensorCode = in.level.fault == FaultCode::NONE ? FaultCode::SENSOR_STALE : in.level.fault;
  }

  // --- Hardware trips (highest priority) -----------------------------------
  if (in.estopActive) {
    v.equipmentFault = FaultCode::EMERGENCY_STOP;
  } else if (cfg_.overloadInstalled && in.overloadTripped) {
    v.equipmentFault = FaultCode::MOTOR_OVERLOAD;
  }
  if (in.hhTripped) v.overflow = true;

  // --- Contactor supervision ---------------------------------------------
  if (cfg_.feedbackInstalled && v.equipmentFault == FaultCode::NONE && !v.overflow) {
    if (commanded) {
      if (in.contactorClosed) {
        pulledIn_ = true;
      } else if (!pulledIn_) {
        if (hasElapsed(now, commandChangedMs_, cfg_.feedbackTimeoutMs)) {
          v.equipmentFault = FaultCode::CONTACTOR_NO_PULL_IN;
        }
      } else if (hasElapsed(now, feedbackChangedMs_, cfg_.feedbackTimeoutMs)) {
        // Was closed, opened while still commanded, and no hardware trip is
        // reported: supply loss on the coil circuit or a wiring fault.
        v.equipmentFault = FaultCode::CONTACTOR_DROPOUT;
      }
    } else if (in.contactorClosed) {
      const uint32_t since = (static_cast<int32_t>(feedbackChangedMs_ - commandChangedMs_) > 0)
                                 ? feedbackChangedMs_
                                 : commandChangedMs_;
      if (hasElapsed(now, since, cfg_.feedbackTimeoutMs)) {
        v.equipmentFault = FaultCode::CONTACTOR_WELDED;
      }
    }
  }

  // --- Runtime supervision -------------------------------------------------
  if (running_ && v.equipmentFault == FaultCode::NONE) {
    if (in.level.valid && in.level.wetCount > highestWet_) {
      highestWet_ = in.level.wetCount;
      lastRiseMs_ = now;
    }
    if (hasElapsed(now, runStartMs_, cfg_.maxRuntimeMs)) {
      v.equipmentFault = FaultCode::MAX_RUNTIME;
    } else if (cfg_.noRiseTimeoutMs != 0 && hasElapsed(now, lastRiseMs_, cfg_.noRiseTimeoutMs)) {
      v.equipmentFault = FaultCode::NO_LEVEL_RISE;
    }
  }

  return v;
}

}  // namespace wintek
