// Alternating-polarity probe excitation and ADC acquisition (ESP32-C3).
#pragma once

#include <stdint.h>

#include "core/Types.h"

namespace wintek {

class ProbeFrontEnd {
 public:
  void begin();
  // One balanced acquisition: phase A then phase B, equal durations, then
  // both excitation lines low. Blocks for ~2 x settleUs + 8 ADC reads.
  void acquire(uint16_t settleUs, float vAMv[kMaxProbes], float vBMv[kMaxProbes]);
};

}  // namespace wintek
