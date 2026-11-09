#include "motor/MotorStateMachine.h"

#include "sensors/LevelEstimator.h"

namespace wintek {

namespace {
inline uint8_t levelOf(const LevelReading& r) { return r.valid ? r.levelPercent : 0; }
}  // namespace

void MotorStateMachine::configure(const DeviceConfig& cfg) {
  cfg_ = cfg;
  SafetyConfig sc;
  sc.feedbackInstalled = cfg.contactorFeedbackInstalled;
  sc.overloadInstalled = cfg.overloadInputInstalled;
  sc.feedbackTimeoutMs = cfg.contactorFeedbackTimeoutMs;
  sc.maxRuntimeMs = cfg.maxRuntimeMs;
  sc.noRiseTimeoutMs = cfg.noRiseTimeoutMs;
  safety_.configure(sc);
}

void MotorStateMachine::begin(uint32_t nowMs, ControlMode savedMode, FaultCode restoredLatch) {
  state_ = MotorState::BOOT_SELF_TEST;
  softwareMode_ = (savedMode == ControlMode::MANUAL) ? ControlMode::MANUAL : ControlMode::AUTO;
  effMode_ = softwareMode_;
  motor_ = false;  // never restore a running motor
  bootMs_ = nowMs;
  onSinceMs_ = nowMs;
  // The restart delay governs the first start after boot; treat the
  // minimum OFF time as already satisfied.
  offSinceMs_ = nowMs - cfg_.minOffMs;
  lastNowMs_ = nowMs;
  latched_ = FaultCode::NONE;
  overflowLatched_ = false;
  pendingLatch_ = isLatchingFault(restoredLatch) ? restoredLatch : FaultCode::NONE;
  fillDemand_ = forceFill_ = userInhibit_ = false;
  sensorValidTracking_ = false;
  startCount_ = startHead_ = 0;
  evHead_ = evCount_ = 0;
  safety_.resetSupervision(nowMs);
}

// ---------------------------------------------------------------------------
FaultCode MotorStateMachine::latchedFault() const {
  if (latched_ != FaultCode::NONE) return latched_;
  if (overflowLatched_) return FaultCode::OVERFLOW_HH;
  return pendingLatch_;
}

FaultCode MotorStateMachine::activeFault() const {
  const FaultCode l = latchedFault();
  if (l != FaultCode::NONE && state_ != MotorState::BOOT_SELF_TEST) return l;
  if (state_ == MotorState::SENSOR_FAULT) return verdict_.sensorCode;
  return FaultCode::NONE;
}

ControlMode MotorStateMachine::computeEffectiveMode(LocalSwitch sw) const {
  if (!cfg_.localPanelInstalled) return softwareMode_;
  switch (sw) {
    case LocalSwitch::AUTO: return softwareMode_;
    case LocalSwitch::MANUAL: return ControlMode::MANUAL;
    case LocalSwitch::OFF:
    case LocalSwitch::UNKNOWN:  // unreadable panel: fail safe to OFF
    default: return ControlMode::OFF;
  }
}

MotorState MotorStateMachine::idleStateFor(ControlMode m) const {
  return m == ControlMode::AUTO ? MotorState::AUTO_IDLE : MotorState::MANUAL_IDLE;
}

bool MotorStateMachine::isNormalOperatingState() const {
  switch (state_) {
    case MotorState::AUTO_IDLE:
    case MotorState::AUTO_FILLING:
    case MotorState::MANUAL_IDLE:
    case MotorState::MANUAL_RUNNING:
    case MotorState::SCHEDULE_WAIT:
      return true;
    default:
      return false;
  }
}

uint8_t MotorStateMachine::startsInLastHour(uint32_t now) const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < startCount_; ++i) {
    if (!hasElapsed(now, startTimes_[i], 3600000UL)) ++n;
  }
  return n;
}

uint32_t MotorStateMachine::startBlockedForMs(uint32_t now) const {
  uint32_t wait = 0;
  if (state_ == MotorState::BOOT_SELF_TEST) {
    const uint32_t d = cfg_.restartDelayMs > kMinSelfTestMs ? cfg_.restartDelayMs : kMinSelfTestMs;
    const uint32_t e = elapsedMs(now, bootMs_);
    if (e < d) wait = d - e;
  }
  if (!motor_) {
    const uint32_t e = elapsedMs(now, offSinceMs_);
    if (e < cfg_.minOffMs && cfg_.minOffMs - e > wait) wait = cfg_.minOffMs - e;
  }
  if (startsInLastHour(now) >= cfg_.maxStartsPerHour) {
    uint32_t oldestRemaining = 0xFFFFFFFFUL;
    for (uint8_t i = 0; i < startCount_; ++i) {
      const uint32_t e = elapsedMs(now, startTimes_[i]);
      if (e < 3600000UL && 3600000UL - e < oldestRemaining) oldestRemaining = 3600000UL - e;
    }
    if (oldestRemaining != 0xFFFFFFFFUL && oldestRemaining > wait) wait = oldestRemaining;
  }
  return wait;
}

RejectReason MotorStateMachine::canStart(const MachineInputs& in, uint32_t& retry) const {
  retry = 0;
  if (in.estopActive) return RejectReason::EMERGENCY_STOP_ACTIVE;
  if (safety_.overloadActive(in)) return RejectReason::OVERLOAD_ACTIVE;
  if (in.hhTripped) return RejectReason::OVERFLOW_ACTIVE;
  if (!in.level.valid) return RejectReason::SENSOR_INVALID;
  if (isAtOrAboveOffThreshold(in.level, cfg_.offProbeIndex)) return RejectReason::TANK_FULL;
  if (!safety_.feedbackConfirmsOff(in)) return RejectReason::MOTOR_NOT_STOPPED;
  if (!hasElapsed(in.nowMs, offSinceMs_, cfg_.minOffMs)) {
    retry = startBlockedForMs(in.nowMs);
    return RejectReason::MIN_OFF_TIME;
  }
  if (startsInLastHour(in.nowMs) >= cfg_.maxStartsPerHour) {
    retry = startBlockedForMs(in.nowMs);
    return RejectReason::START_RATE_LIMIT;
  }
  return RejectReason::NONE;
}

// ---------------------------------------------------------------------------
void MotorStateMachine::pushEvent(const MachineEvent& e) {
  const uint8_t idx = static_cast<uint8_t>((evHead_ + evCount_) % kEventQueueSize);
  events_[idx] = e;
  if (evCount_ < kEventQueueSize) {
    ++evCount_;
  } else {
    evHead_ = static_cast<uint8_t>((evHead_ + 1) % kEventQueueSize);  // drop oldest
  }
}

bool MotorStateMachine::popEvent(MachineEvent& out) {
  if (evCount_ == 0) return false;
  out = events_[evHead_];
  evHead_ = static_cast<uint8_t>((evHead_ + 1) % kEventQueueSize);
  --evCount_;
  return true;
}

void MotorStateMachine::transition(MotorState s, uint32_t now) {
  if (s == state_) return;
  MachineEvent e;
  e.type = MachineEventType::STATE_CHANGED;
  e.atMs = now;
  e.fromState = state_;
  e.toState = s;
  e.mode = effMode_;
  state_ = s;
  pushEvent(e);
}

void MotorStateMachine::start(StartReason r, uint32_t now, uint8_t level) {
  if (motor_) return;
  motor_ = true;
  onSinceMs_ = now;
  lastStart_ = r;
  runtimeWarned_ = false;
  startTimes_[startHead_] = now;
  startHead_ = static_cast<uint8_t>((startHead_ + 1) % kMaxStartHistory);
  if (startCount_ < kMaxStartHistory) ++startCount_;
  MachineEvent e;
  e.type = MachineEventType::MOTOR_STARTED;
  e.atMs = now;
  e.startReason = r;
  e.mode = effMode_;
  e.levelPercent = level;
  e.fromState = e.toState = state_;
  pushEvent(e);
}

void MotorStateMachine::stop(StopReason r, uint32_t now, uint8_t level) {
  if (!motor_) return;
  motor_ = false;
  offSinceMs_ = now;
  lastStop_ = r;
  MachineEvent e;
  e.type = MachineEventType::MOTOR_STOPPED;
  e.atMs = now;
  e.stopReason = r;
  e.runtimeMs = elapsedMs(now, onSinceMs_);
  e.mode = effMode_;
  e.levelPercent = level;
  e.fromState = e.toState = state_;
  pushEvent(e);
}

void MotorStateMachine::enterLockout(MotorState lockState, FaultCode code, uint32_t now, uint8_t level) {
  stop(lockState == MotorState::OVERFLOW_LOCKOUT ? StopReason::OVERFLOW : StopReason::SAFETY_TRIP, now, level);
  if (lockState == MotorState::OVERFLOW_LOCKOUT) {
    overflowLatched_ = true;
  } else {
    latched_ = code;
  }
  latchDirty_ = true;
  fillDemand_ = forceFill_ = false;
  MachineEvent e;
  e.type = MachineEventType::FAULT_RAISED;
  e.atMs = now;
  e.fault = code;
  e.levelPercent = level;
  e.fromState = state_;
  e.toState = lockState;
  pushEvent(e);
  transition(lockState, now);
}

void MotorStateMachine::leaveLockoutToIdle(uint32_t now) {
  MachineEvent e;
  e.type = MachineEventType::FAULT_CLEARED;
  e.atMs = now;
  e.fault = latchedFault();
  latched_ = FaultCode::NONE;
  overflowLatched_ = false;
  pendingLatch_ = FaultCode::NONE;
  latchDirty_ = true;
  userInhibit_ = false;
  safety_.resetSupervision(now);
  pushEvent(e);
  transition(idleStateFor(effMode_), now);
}

// ---------------------------------------------------------------------------
void MotorStateMachine::applyEffectiveMode(ControlMode eff, const MachineInputs& in) {
  if (eff == effMode_) return;
  const uint32_t now = in.nowMs;
  const uint8_t lvl = levelOf(in.level);
  effMode_ = eff;
  MachineEvent e;
  e.type = MachineEventType::MODE_CHANGED;
  e.atMs = now;
  e.mode = eff;
  e.fromState = e.toState = state_;
  pushEvent(e);

  if (!isNormalOperatingState()) return;  // lockout/boot/OTA/sensor states keep the motor off anyway

  if (eff == ControlMode::AUTO) {
    userInhibit_ = false;
    if (motor_) {
      // MANUAL -> AUTO while running: continue as an automatic fill to the
      // OFF threshold rather than cycling the contactor.
      fillDemand_ = !isAtOrAboveOffThreshold(in.level, cfg_.offProbeIndex);
      pendingStartReason_ = StartReason::MODE_HANDOVER;
      if (fillDemand_) transition(MotorState::AUTO_FILLING, now);
    } else {
      transition(MotorState::AUTO_IDLE, now);
    }
  } else {
    if (motor_) stop(eff == ControlMode::OFF ? StopReason::LOCAL_SWITCH_OFF : StopReason::MODE_CHANGE, now, lvl);
    fillDemand_ = forceFill_ = userInhibit_ = false;
    transition(MotorState::MANUAL_IDLE, now);
  }
}

void MotorStateMachine::runAuto(const MachineInputs& in) {
  const uint32_t now = in.nowMs;
  const uint8_t lvl = levelOf(in.level);
  const bool full = isAtOrAboveOffThreshold(in.level, cfg_.offProbeIndex);
  const bool below = isBelowOnThreshold(in.level, cfg_.onProbeIndex);

  if (userInhibit_ && hasElapsed(now, userInhibitSinceMs_, cfg_.autoResumeAfterUserOffMs)) {
    userInhibit_ = false;
  }

  if (full) {
    if (motor_) {
      stop(StopReason::LEVEL_REACHED, now, lvl);
      MachineEvent e;
      e.type = MachineEventType::TANK_FULL;
      e.atMs = now;
      e.levelPercent = lvl;
      e.fromState = e.toState = state_;
      pushEvent(e);
    }
    fillDemand_ = forceFill_ = false;
    userInhibit_ = false;  // cycle complete
  } else if (!fillDemand_ && !userInhibit_) {
    if (below) {
      fillDemand_ = true;
      pendingStartReason_ = StartReason::AUTO_LOW_LEVEL;
    } else if (in.scheduleFillDue) {
      fillDemand_ = true;
      pendingStartReason_ = StartReason::AUTO_SCHEDULED_FILL;
    }
  }

  if (!fillDemand_) {
    if (motor_) stop(StopReason::USER_OFF, now, lvl);  // defensive; demand cleared elsewhere
    transition(MotorState::AUTO_IDLE, now);
    return;
  }

  const bool windowOk = in.scheduleWindowOpen || forceFill_ ||
                        pendingStartReason_ == StartReason::AUTO_SCHEDULED_FILL;
  if (!windowOk) {
    if (motor_) {
      if (cfg_.scheduleStopAtWindowEnd && hasElapsed(now, onSinceMs_, cfg_.minOnMs)) {
        stop(StopReason::SCHEDULE_WINDOW_END, now, lvl);
        transition(MotorState::SCHEDULE_WAIT, now);
      } else {
        transition(MotorState::AUTO_FILLING, now);
      }
    } else {
      transition(MotorState::SCHEDULE_WAIT, now);
    }
    return;
  }

  if (motor_) {
    transition(MotorState::AUTO_FILLING, now);
    return;
  }
  uint32_t retry = 0;
  if (canStart(in, retry) == RejectReason::NONE) {
    transition(MotorState::AUTO_FILLING, now);
    start(pendingStartReason_, now, lvl);
  } else {
    transition(MotorState::AUTO_IDLE, now);  // waiting for timing guards
  }
}

void MotorStateMachine::runManual(const MachineInputs& in) {
  const uint32_t now = in.nowMs;
  const uint8_t lvl = levelOf(in.level);
  if (motor_ && isAtOrAboveOffThreshold(in.level, cfg_.offProbeIndex)) {
    stop(StopReason::LEVEL_REACHED, now, lvl);
    MachineEvent e;
    e.type = MachineEventType::TANK_FULL;
    e.atMs = now;
    e.levelPercent = lvl;
    e.fromState = e.toState = state_;
    pushEvent(e);
  }
  if (effMode_ == ControlMode::OFF && motor_) stop(StopReason::LOCAL_SWITCH_OFF, now, lvl);
  transition(motor_ ? MotorState::MANUAL_RUNNING : MotorState::MANUAL_IDLE, now);
}

void MotorStateMachine::update(const MachineInputs& in) {
  const uint32_t now = in.nowMs;
  lastNowMs_ = now;
  const uint8_t lvl = levelOf(in.level);
  verdict_ = safety_.evaluate(in, motor_);

  // 1) Latched equipment faults (overload, E-stop, contactor, runtime).
  if (verdict_.equipmentFault != FaultCode::NONE && state_ != MotorState::FAULT_LOCKOUT) {
    enterLockout(MotorState::FAULT_LOCKOUT, verdict_.equipmentFault, now, lvl);
    return;
  }
  // 2) Independent overflow cutoff.
  if (verdict_.overflow && state_ != MotorState::FAULT_LOCKOUT && state_ != MotorState::OVERFLOW_LOCKOUT) {
    enterLockout(MotorState::OVERFLOW_LOCKOUT, FaultCode::OVERFLOW_HH, now, lvl);
    return;
  }
  if (state_ == MotorState::FAULT_LOCKOUT || state_ == MotorState::OVERFLOW_LOCKOUT) {
    if (motor_) stop(StopReason::SAFETY_TRIP, now, lvl);
    effMode_ = computeEffectiveMode(in.localSwitch);
    return;  // leaves only via RESET_FAULT
  }

  // 3) OTA maintenance: motor held off, exit via EXIT_OTA or reboot.
  if (state_ == MotorState::OTA_MAINTENANCE) {
    if (motor_) stop(StopReason::OTA, now, lvl);
    return;
  }

  // 4) Power-up self test and restart delay.
  if (state_ == MotorState::BOOT_SELF_TEST) {
    const uint32_t hold = cfg_.restartDelayMs > kMinSelfTestMs ? cfg_.restartDelayMs : kMinSelfTestMs;
    effMode_ = computeEffectiveMode(in.localSwitch);
    if (!hasElapsed(now, bootMs_, hold)) return;
    if (pendingLatch_ != FaultCode::NONE) {
      const FaultCode f = pendingLatch_;
      pendingLatch_ = FaultCode::NONE;
      enterLockout(f == FaultCode::OVERFLOW_HH ? MotorState::OVERFLOW_LOCKOUT : MotorState::FAULT_LOCKOUT,
                   f, now, lvl);
      return;
    }
    if (verdict_.sensorFault) {
      MachineEvent e;
      e.type = MachineEventType::FAULT_RAISED;
      e.atMs = now;
      e.fault = verdict_.sensorCode;
      e.fromState = state_;
      e.toState = MotorState::SENSOR_FAULT;
      pushEvent(e);
      sensorValidTracking_ = false;
      transition(MotorState::SENSOR_FAULT, now);
      return;
    }
    transition(idleStateFor(effMode_), now);
  }

  // 5) Sensor fault (auto-recovering).
  if (verdict_.sensorFault) {
    sensorValidTracking_ = false;
    if (state_ != MotorState::SENSOR_FAULT) {
      stop(StopReason::SENSOR_FAULT, now, lvl);
      fillDemand_ = forceFill_ = false;
      MachineEvent e;
      e.type = MachineEventType::FAULT_RAISED;
      e.atMs = now;
      e.fault = verdict_.sensorCode;
      e.fromState = state_;
      e.toState = MotorState::SENSOR_FAULT;
      pushEvent(e);
      transition(MotorState::SENSOR_FAULT, now);
    }
    effMode_ = computeEffectiveMode(in.localSwitch);
    return;
  }
  if (state_ == MotorState::SENSOR_FAULT) {
    effMode_ = computeEffectiveMode(in.localSwitch);
    if (!sensorValidTracking_) {
      sensorValidTracking_ = true;
      sensorValidSinceMs_ = now;
    }
    if (!hasElapsed(now, sensorValidSinceMs_, cfg_.sensorRecoveryMs)) return;
    MachineEvent e;
    e.type = MachineEventType::FAULT_CLEARED;
    e.atMs = now;
    e.fault = verdict_.sensorCode == FaultCode::NONE ? FaultCode::SENSOR_SEQUENCE : verdict_.sensorCode;
    pushEvent(e);
    transition(idleStateFor(effMode_), now);
  }

  // 6) Normal operation.
  applyEffectiveMode(computeEffectiveMode(in.localSwitch), in);

  if (motor_ && !runtimeWarned_ && hasElapsed(now, onSinceMs_, (cfg_.maxRuntimeMs / 5U) * 4U)) {
    runtimeWarned_ = true;
    MachineEvent e;
    e.type = MachineEventType::RUNTIME_WARNING;
    e.atMs = now;
    e.runtimeMs = elapsedMs(now, onSinceMs_);
    e.levelPercent = lvl;
    pushEvent(e);
  }

  if (effMode_ == ControlMode::AUTO) {
    runAuto(in);
  } else {
    runManual(in);
  }
}

// ---------------------------------------------------------------------------
CommandOutcome MotorStateMachine::handleCommand(const Command& cmd, const MachineInputs& in) {
  CommandOutcome out;
  out.status = CommandStatus::REJECTED;
  const uint32_t now = in.nowMs;
  const uint8_t lvl = levelOf(in.level);

  switch (cmd.action) {
    // ---------------------------------------------------------------------
    case CommandAction::MOTOR_OFF: {
      // Stopping is always permitted from every source and in every state.
      stop(cmd.source == CommandSource::LOCAL_PANEL ? StopReason::LOCAL_STOP : StopReason::USER_OFF, now, lvl);
      if (isNormalOperatingState()) {
        if (effMode_ == ControlMode::AUTO) {
          fillDemand_ = forceFill_ = false;
          userInhibit_ = true;
          userInhibitSinceMs_ = now;
          transition(MotorState::AUTO_IDLE, now);
        } else {
          transition(MotorState::MANUAL_IDLE, now);
        }
      }
      out.status = CommandStatus::ACCEPTED;
      return out;
    }

    // ---------------------------------------------------------------------
    case CommandAction::MOTOR_ON: {
      switch (state_) {
        case MotorState::BOOT_SELF_TEST:
          out.reason = RejectReason::BOOTING;
          out.retryAfterMs = startBlockedForMs(now);
          return out;
        case MotorState::OTA_MAINTENANCE: out.reason = RejectReason::OTA_IN_PROGRESS; return out;
        case MotorState::FAULT_LOCKOUT: out.reason = RejectReason::FAULT_LOCKOUT_ACTIVE; return out;
        case MotorState::OVERFLOW_LOCKOUT: out.reason = RejectReason::OVERFLOW_ACTIVE; return out;
        case MotorState::SENSOR_FAULT: out.reason = RejectReason::SENSOR_INVALID; return out;
        default: break;
      }
      const LocalSwitch sw = cfg_.localPanelInstalled ? in.localSwitch : LocalSwitch::AUTO;
      if (sw == LocalSwitch::OFF || sw == LocalSwitch::UNKNOWN) {
        out.reason = RejectReason::LOCAL_SWITCH_OFF;
        return out;
      }
      if (sw == LocalSwitch::MANUAL && cmd.source != CommandSource::LOCAL_PANEL) {
        out.reason = RejectReason::LOCAL_SWITCH_MANUAL;  // local manual = local control only
        return out;
      }
      if (motor_) {
        out.status = CommandStatus::ACCEPTED;  // idempotent
        out.reason = RejectReason::ALREADY_IN_STATE;
        return out;
      }
      const RejectReason r = canStart(in, out.retryAfterMs);
      if (r != RejectReason::NONE) {
        out.reason = r;
        return out;
      }
      // Optional explicit mode in the command (only with selector in AUTO).
      if (sw == LocalSwitch::AUTO && cmd.modeSpecified && cmd.mode != softwareMode_) {
        softwareMode_ = cmd.mode;
        modeDirty_ = true;
        applyEffectiveMode(computeEffectiveMode(in.localSwitch), in);
      }
      if (effMode_ == ControlMode::AUTO) {
        fillDemand_ = true;
        forceFill_ = true;
        userInhibit_ = false;
        pendingStartReason_ = StartReason::AUTO_USER_FILL;
        transition(MotorState::AUTO_FILLING, now);
        start(StartReason::AUTO_USER_FILL, now, lvl);
      } else {
        transition(MotorState::MANUAL_RUNNING, now);
        start(cmd.source == CommandSource::LOCAL_PANEL ? StartReason::MANUAL_LOCAL : StartReason::MANUAL_USER, now, lvl);
      }
      out.status = CommandStatus::ACCEPTED;
      return out;
    }

    // ---------------------------------------------------------------------
    case CommandAction::SET_MODE: {
      if (cmd.mode != ControlMode::AUTO && cmd.mode != ControlMode::MANUAL) {
        out.status = CommandStatus::INVALID;
        return out;
      }
      if (state_ == MotorState::OTA_MAINTENANCE) {
        out.reason = RejectReason::OTA_IN_PROGRESS;
        return out;
      }
      if (cfg_.localPanelInstalled && in.localSwitch != LocalSwitch::AUTO) {
        out.reason = in.localSwitch == LocalSwitch::MANUAL ? RejectReason::LOCAL_SWITCH_MANUAL
                                                            : RejectReason::LOCAL_SWITCH_OFF;
        return out;
      }
      out.status = CommandStatus::ACCEPTED;
      if (cmd.mode == softwareMode_) {
        out.reason = RejectReason::ALREADY_IN_STATE;
        return out;
      }
      softwareMode_ = cmd.mode;
      modeDirty_ = true;
      applyEffectiveMode(computeEffectiveMode(in.localSwitch), in);
      return out;
    }

    // ---------------------------------------------------------------------
    case CommandAction::RESET_FAULT: {
      if (state_ == MotorState::FAULT_LOCKOUT) {
        if (in.estopActive || safety_.overloadActive(in) || in.hhTripped ||
            safety_.contactorClosedWhileOff(in, motor_)) {
          out.reason = RejectReason::CONDITION_NOT_CLEARED;
          return out;
        }
        leaveLockoutToIdle(now);
        out.status = CommandStatus::ACCEPTED;
        return out;
      }
      if (state_ == MotorState::OVERFLOW_LOCKOUT) {
        if (in.hhTripped) {
          out.reason = RejectReason::CONDITION_NOT_CLEARED;
          return out;
        }
        leaveLockoutToIdle(now);
        out.status = CommandStatus::ACCEPTED;
        return out;
      }
      out.reason = (state_ == MotorState::BOOT_SELF_TEST) ? RejectReason::BOOTING : RejectReason::NOT_IN_LOCKOUT;
      return out;
    }

    // ---------------------------------------------------------------------
    case CommandAction::ENTER_OTA: {
      if (state_ == MotorState::OTA_MAINTENANCE) {
        out.status = CommandStatus::ACCEPTED;
        out.reason = RejectReason::ALREADY_IN_STATE;
        return out;
      }
      if (state_ == MotorState::BOOT_SELF_TEST) {
        out.reason = RejectReason::BOOTING;
        out.retryAfterMs = startBlockedForMs(now);
        return out;
      }
      if (motor_) {
        // Step 1 of the OTA sequence: request a safe shutdown, then the
        // caller retries once the contactor feedback confirms OFF.
        stop(StopReason::OTA, now, lvl);
        fillDemand_ = forceFill_ = false;
        if (isNormalOperatingState()) transition(idleStateFor(effMode_), now);
        out.reason = RejectReason::MOTOR_NOT_STOPPED;
        out.retryAfterMs = cfg_.contactorFeedbackTimeoutMs;
        return out;
      }
      if (!safety_.feedbackConfirmsOff(in)) {
        out.reason = RejectReason::MOTOR_NOT_STOPPED;
        out.retryAfterMs = cfg_.contactorFeedbackTimeoutMs;
        return out;
      }
      fillDemand_ = forceFill_ = userInhibit_ = false;
      transition(MotorState::OTA_MAINTENANCE, now);
      out.status = CommandStatus::ACCEPTED;
      return out;
    }

    case CommandAction::EXIT_OTA: {
      if (state_ != MotorState::OTA_MAINTENANCE) {
        out.reason = RejectReason::NOT_IN_OTA;
        return out;
      }
      // Re-validate everything as after a power-up before resuming AUTO.
      pendingLatch_ = latchedFault();
      latched_ = FaultCode::NONE;
      overflowLatched_ = false;
      bootMs_ = now;
      safety_.resetSupervision(now);
      transition(MotorState::BOOT_SELF_TEST, now);
      out.status = CommandStatus::ACCEPTED;
      return out;
    }
  }
  out.status = CommandStatus::INVALID;
  out.reason = RejectReason::UNSUPPORTED;
  return out;
}

}  // namespace wintek
