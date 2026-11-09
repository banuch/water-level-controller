// The control task is the single authority over the motor output.
// It runs every 20 ms at high priority and never blocks on networking.
#pragma once

#include <stdint.h>

#include "core/Types.h"

namespace wintek {

struct ControlBootInfo {
  FaultCode restoredLatch = FaultCode::NONE;
  ControlMode restoredMode = ControlMode::AUTO;
  uint32_t totalRuntimeS = 0;
  uint32_t totalStarts = 0;
  bool powerOnReset = false;  // raise POWER_RESTORED alert
};

class ControlTask {
 public:
  static void start(const ControlBootInfo& info);
  // True once the control loop has completed its self test window with
  // valid sensors (used by the OTA image self-test).
  static bool healthy();
};

}  // namespace wintek
