#include "sensors/LevelEstimator.h"

namespace wintek {

LevelReading estimateLevel(const ProbeLayout& layout, const ProbeState states[kMaxProbes]) {
  LevelReading r;
  r.probeCount = layout.probeCount;
  r.valid = false;

  if (layout.probeCount == 0 || layout.probeCount > kMaxProbes) {
    r.fault = FaultCode::SENSOR_CHAIN;
    return r;
  }

  for (uint8_t i = 0; i < layout.probeCount; ++i) {
    if (states[i] == ProbeState::FAULT) {
      r.fault = FaultCode::SENSOR_CHAIN;
      return r;
    }
  }

  uint8_t wet = 0;
  while (wet < layout.probeCount && states[wet] == ProbeState::WET) ++wet;
  for (uint8_t i = wet; i < layout.probeCount; ++i) {
    if (states[i] == ProbeState::WET) {  // wet probe above a dry probe
      r.fault = FaultCode::SENSOR_SEQUENCE;
      return r;
    }
  }

  r.valid = true;
  r.fault = FaultCode::NONE;
  r.wetCount = wet;
  if (wet == 0) {
    r.levelPercent = 0;
    r.rangeLowPercent = 0;
    r.rangeHighPercent = layout.positionPercent[0];
  } else {
    r.levelPercent = layout.positionPercent[wet - 1];
    r.rangeLowPercent = layout.positionPercent[wet - 1];
    r.rangeHighPercent = (wet < layout.probeCount) ? layout.positionPercent[wet] : 100;
  }
  return r;
}

}  // namespace wintek
