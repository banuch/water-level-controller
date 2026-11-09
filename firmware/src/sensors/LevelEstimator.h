// Maps debounced probe states to a discrete tank level.
//
// Probes are ordered bottom (index 0) to top. Water is conductive, so the
// set of wet probes must be a contiguous run starting at the bottom. Any
// other pattern (e.g. 75% wet while 50% dry) is physically impossible and
// is reported as SENSOR_SEQUENCE.
//
// The level is DISCRETE: the reported percentage is the position of the
// highest wet probe and the true level lies in [that probe, next probe).
// No interpolation is performed between probe positions.
#pragma once

#include "core/Types.h"

namespace wintek {

struct ProbeLayout {
  uint8_t probeCount = 4;
  uint8_t positionPercent[kMaxProbes] = {25, 50, 75, 95};
};

LevelReading estimateLevel(const ProbeLayout& layout, const ProbeState states[kMaxProbes]);

// Threshold semantics (by probe index):
//   ON  threshold k: fill is demanded when probe k is DRY (water fell below it)
//   OFF threshold m: fill stops when probe m is WET  (water reached it)
inline bool isBelowOnThreshold(const LevelReading& r, uint8_t onIndex) {
  return r.valid && r.wetCount <= onIndex;
}
inline bool isAtOrAboveOffThreshold(const LevelReading& r, uint8_t offIndex) {
  return r.valid && r.wetCount >= static_cast<uint8_t>(offIndex + 1);
}

}  // namespace wintek
