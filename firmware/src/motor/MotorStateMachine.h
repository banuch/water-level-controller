// Explicit motor control finite-state machine (hardware independent).
// See docs/ARCHITECTURE.md §6 for the full transition table.
#pragma once

#include "core/Types.h"
#include "motor/SafetyManager.h"
#include "storage/Config.h"

namespace wintek {

struct MachineInputs : SafetyInputs {
  LocalSwitch localSwitch = LocalSwitch::AUTO;  // pass AUTO when no local panel is installed
  bool scheduleWindowOpen = true;               // true when schedule restriction disabled
  bool scheduleFillDue = false;                 // one-shot: a FILL schedule fired this tick
};

enum class MachineEventType : uint8_t {
  STATE_CHANGED = 0,
  MODE_CHANGED,
  MOTOR_STARTED,
  MOTOR_STOPPED,
  FAULT_RAISED,
  FAULT_CLEARED,
  TANK_FULL,
  RUNTIME_WARNING,  // 80 % of the maximum runtime reached
};

struct MachineEvent {
  MachineEventType type = MachineEventType::STATE_CHANGED;
  uint32_t atMs = 0;
  MotorState fromState = MotorState::BOOT_SELF_TEST;
  MotorState toState = MotorState::BOOT_SELF_TEST;
  ControlMode mode = ControlMode::AUTO;
  FaultCode fault = FaultCode::NONE;
  StartReason startReason = StartReason::NONE;
  StopReason stopReason = StopReason::NONE;
  uint32_t runtimeMs = 0;
  uint8_t levelPercent = 0;
};

class MotorStateMachine {
 public:
  static constexpr uint32_t kMinSelfTestMs = 3000;
  static constexpr uint8_t kEventQueueSize = 16;
  static constexpr uint8_t kMaxStartHistory = 30;

  void configure(const DeviceConfig& cfg);
  // restoredLatch: latched fault persisted in NVS before the reboot.
  void begin(uint32_t nowMs, ControlMode savedSoftwareMode, FaultCode restoredLatch);

  void update(const MachineInputs& in);
  CommandOutcome handleCommand(const Command& cmd, const MachineInputs& in);

  // Outputs / observers
  bool motorOutput() const { return motor_; }
  MotorState state() const { return state_; }
  ControlMode effectiveMode() const { return effMode_; }
  ControlMode softwareMode() const { return softwareMode_; }
  FaultCode activeFault() const;
  FaultCode latchedFault() const;
  StartReason lastStartReason() const { return lastStart_; }
  StopReason lastStopReason() const { return lastStop_; }
  bool fillDemand() const { return fillDemand_; }
  bool userInhibit() const { return userInhibit_; }
  uint32_t motorOnSinceMs() const { return onSinceMs_; }
  uint32_t motorOffSinceMs() const { return offSinceMs_; }
  uint32_t currentRunMs(uint32_t nowMs) const { return motor_ ? elapsedMs(nowMs, onSinceMs_) : 0; }
  // Remaining time before a start would be permitted by timing guards.
  uint32_t startBlockedForMs(uint32_t nowMs) const;
  const SafetyVerdict& lastVerdict() const { return verdict_; }

  // Persistence hooks (consumed by the control task).
  bool takeLatchDirty() { bool d = latchDirty_; latchDirty_ = false; return d; }
  bool takeModeDirty() { bool d = modeDirty_; modeDirty_ = false; return d; }

  bool popEvent(MachineEvent& out);

 private:
  ControlMode computeEffectiveMode(LocalSwitch sw) const;
  MotorState idleStateFor(ControlMode m) const;
  bool isNormalOperatingState() const;
  void applyEffectiveMode(ControlMode eff, const MachineInputs& in);
  RejectReason canStart(const MachineInputs& in, uint32_t& retryAfterMs) const;
  void start(StartReason r, uint32_t now, uint8_t level);
  void stop(StopReason r, uint32_t now, uint8_t level);
  void transition(MotorState s, uint32_t now);
  void enterLockout(MotorState lockState, FaultCode code, uint32_t now, uint8_t level);
  void leaveLockoutToIdle(uint32_t now);
  void runAuto(const MachineInputs& in);
  void runManual(const MachineInputs& in);
  void pushEvent(const MachineEvent& e);
  uint8_t startsInLastHour(uint32_t now) const;

  DeviceConfig cfg_;
  SafetyManager safety_;
  SafetyVerdict verdict_;

  MotorState state_ = MotorState::BOOT_SELF_TEST;
  ControlMode softwareMode_ = ControlMode::AUTO;
  ControlMode effMode_ = ControlMode::AUTO;
  bool motor_ = false;
  uint32_t bootMs_ = 0;
  uint32_t onSinceMs_ = 0;
  uint32_t offSinceMs_ = 0;
  uint32_t lastNowMs_ = 0;

  FaultCode latched_ = FaultCode::NONE;    // equipment fault latch
  bool overflowLatched_ = false;           // overflow latch
  FaultCode pendingLatch_ = FaultCode::NONE;  // restored from NVS, applied after self test
  bool latchDirty_ = false;
  bool modeDirty_ = false;

  bool fillDemand_ = false;
  bool forceFill_ = false;  // user-requested fill bypasses schedule windows
  bool userInhibit_ = false;
  uint32_t userInhibitSinceMs_ = 0;
  StartReason pendingStartReason_ = StartReason::AUTO_LOW_LEVEL;
  bool runtimeWarned_ = false;

  uint32_t sensorValidSinceMs_ = 0;
  bool sensorValidTracking_ = false;

  StartReason lastStart_ = StartReason::NONE;
  StopReason lastStop_ = StopReason::NONE;

  uint32_t startTimes_[kMaxStartHistory] = {};
  uint8_t startCount_ = 0;
  uint8_t startHead_ = 0;

  MachineEvent events_[kEventQueueSize];
  uint8_t evHead_ = 0;
  uint8_t evCount_ = 0;
};

}  // namespace wintek
