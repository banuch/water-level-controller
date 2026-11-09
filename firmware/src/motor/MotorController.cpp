#include "motor/MotorController.h"

namespace wintek {

void MotorController::begin(BoardIo* io, uint32_t totalRuntimeS, uint32_t totalStarts) {
  io_ = io;
  runtimeS_ = totalRuntimeS;
  starts_ = totalStarts;
  on_ = false;
  io_->setContactor(false);
}

bool MotorController::apply(bool on, uint32_t nowMs) {
  // Re-assert the output every cycle (guards against a corrupted GPIO latch).
  io_->setContactor(on);
  if (on == on_) return false;
  on_ = on;
  if (on) {
    onSinceMs_ = nowMs;
    ++starts_;
    return false;
  }
  runtimeS_ += (nowMs - onSinceMs_) / 1000U;
  return true;
}

uint32_t MotorController::totalRuntimeS(uint32_t nowMs) const {
  return runtimeS_ + (on_ ? (nowMs - onSinceMs_) / 1000U : 0U);
}

}  // namespace wintek
