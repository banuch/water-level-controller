// Applies the state machine's motor decision to the contactor driver and
// keeps lifetime counters (runtime hours, number of starts).
#pragma once

#include <stdint.h>

#include "hal/BoardIo.h"

namespace wintek {

class MotorController {
 public:
  void begin(BoardIo* io, uint32_t totalRuntimeS, uint32_t totalStarts);
  // Returns true when counters changed enough to be persisted (on each stop).
  bool apply(bool on, uint32_t nowMs);
  uint32_t totalRuntimeS(uint32_t nowMs) const;
  uint32_t totalStarts() const { return starts_; }

 private:
  BoardIo* io_ = nullptr;
  bool on_ = false;
  uint32_t onSinceMs_ = 0;
  uint32_t runtimeS_ = 0;
  uint32_t starts_ = 0;
};

}  // namespace wintek
