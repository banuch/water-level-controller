#include "services/AlertManager.h"

namespace wintek {

const char* toString(AlertType t) {
  switch (t) {
    case AlertType::TANK_FULL: return "TANK_FULL";
    case AlertType::MOTOR_STARTED: return "MOTOR_STARTED";
    case AlertType::MOTOR_STOPPED: return "MOTOR_STOPPED";
    case AlertType::SENSOR_FAULT: return "SENSOR_FAULT";
    case AlertType::MOTOR_OVERLOAD: return "MOTOR_OVERLOAD";
    case AlertType::EXCESSIVE_RUNTIME: return "EXCESSIVE_RUNTIME";
    case AlertType::POWER_RESTORED: return "POWER_RESTORED";
    case AlertType::OVERFLOW_CUTOFF: return "OVERFLOW_CUTOFF";
    case AlertType::CONTACTOR_FAULT: return "CONTACTOR_FAULT";
    case AlertType::NO_LEVEL_RISE: return "NO_LEVEL_RISE";
    case AlertType::EMERGENCY_STOP: return "EMERGENCY_STOP";
    case AlertType::LOCAL_PANEL_FAULT: return "LOCAL_PANEL_FAULT";
    case AlertType::FAULT_CLEARED: return "FAULT_CLEARED";
    case AlertType::RUNTIME_WARNING: return "RUNTIME_WARNING";
  }
  return "UNKNOWN";
}

const char* toString(AlertSeverity s) {
  switch (s) {
    case AlertSeverity::INFO: return "INFO";
    case AlertSeverity::WARNING: return "WARNING";
    case AlertSeverity::CRITICAL: return "CRITICAL";
  }
  return "UNKNOWN";
}

AlertType AlertManager::alertForFault(FaultCode f) {
  switch (f) {
    case FaultCode::SENSOR_SEQUENCE:
    case FaultCode::SENSOR_CHAIN:
    case FaultCode::SENSOR_STALE: return AlertType::SENSOR_FAULT;
    case FaultCode::OVERFLOW_HH: return AlertType::OVERFLOW_CUTOFF;
    case FaultCode::MOTOR_OVERLOAD: return AlertType::MOTOR_OVERLOAD;
    case FaultCode::EMERGENCY_STOP: return AlertType::EMERGENCY_STOP;
    case FaultCode::CONTACTOR_NO_PULL_IN:
    case FaultCode::CONTACTOR_WELDED:
    case FaultCode::CONTACTOR_DROPOUT: return AlertType::CONTACTOR_FAULT;
    case FaultCode::MAX_RUNTIME: return AlertType::EXCESSIVE_RUNTIME;
    case FaultCode::NO_LEVEL_RISE: return AlertType::NO_LEVEL_RISE;
    case FaultCode::LOCAL_PANEL_FAULT: return AlertType::LOCAL_PANEL_FAULT;
    case FaultCode::NONE: break;
  }
  return AlertType::SENSOR_FAULT;
}

AlertSeverity AlertManager::severityFor(AlertType t) {
  switch (t) {
    case AlertType::OVERFLOW_CUTOFF:
    case AlertType::MOTOR_OVERLOAD:
    case AlertType::CONTACTOR_FAULT:
    case AlertType::EMERGENCY_STOP:
    case AlertType::EXCESSIVE_RUNTIME:
    case AlertType::NO_LEVEL_RISE: return AlertSeverity::CRITICAL;
    case AlertType::SENSOR_FAULT:
    case AlertType::LOCAL_PANEL_FAULT:
    case AlertType::RUNTIME_WARNING:
    case AlertType::POWER_RESTORED: return AlertSeverity::WARNING;
    default: return AlertSeverity::INFO;
  }
}

bool AlertManager::admit(Alert& a) {
  // Informational motor events are never suppressed: they form the history.
  const bool suppressible = a.type != AlertType::MOTOR_STARTED && a.type != AlertType::MOTOR_STOPPED &&
                            a.type != AlertType::TANK_FULL;
  if (suppressible) {
    Memo* slot = nullptr;
    Memo* oldest = &memo_[0];
    for (auto& m : memo_) {
      if (m.used && m.type == a.type && m.fault == a.fault) {
        slot = &m;
        break;
      }
      if (!m.used) {
        oldest = &m;
      } else if (oldest->used && static_cast<int32_t>(m.lastMs - oldest->lastMs) < 0) {
        oldest = &m;
      }
    }
    if (slot != nullptr && !hasElapsed(a.atMs, slot->lastMs, kRepeatSuppressMs)) {
      if (slot->suppressed < 0xFFFF) ++slot->suppressed;
      return false;
    }
    if (slot == nullptr) {
      slot = oldest;
      slot->suppressed = 0;
    }
    a.suppressedRepeats = slot->suppressed;
    slot->used = true;
    slot->type = a.type;
    slot->fault = a.fault;
    slot->lastMs = a.atMs;
    slot->suppressed = 0;
  }
  a.severity = severityFor(a.type);
  a.seq = ++seq_;
  return true;
}

bool AlertManager::raise(AlertType t, FaultCode f, uint32_t nowMs, uint8_t level, Alert& out) {
  out = Alert();
  out.type = t;
  out.fault = f;
  out.atMs = nowMs;
  out.levelPercent = level;
  return admit(out);
}

bool AlertManager::fromEvent(const MachineEvent& e, Alert& out) {
  out = Alert();
  out.atMs = e.atMs;
  out.levelPercent = e.levelPercent;
  out.runtimeMs = e.runtimeMs;
  switch (e.type) {
    case MachineEventType::MOTOR_STARTED:
      out.type = AlertType::MOTOR_STARTED;
      out.startReason = e.startReason;
      break;
    case MachineEventType::MOTOR_STOPPED:
      out.type = AlertType::MOTOR_STOPPED;
      out.stopReason = e.stopReason;
      break;
    case MachineEventType::TANK_FULL:
      out.type = AlertType::TANK_FULL;
      break;
    case MachineEventType::FAULT_RAISED:
      out.type = alertForFault(e.fault);
      out.fault = e.fault;
      break;
    case MachineEventType::FAULT_CLEARED:
      out.type = AlertType::FAULT_CLEARED;
      out.fault = e.fault;
      break;
    case MachineEventType::RUNTIME_WARNING:
      out.type = AlertType::RUNTIME_WARNING;
      break;
    default:
      return false;  // state/mode changes are reported via the state topic
  }
  return admit(out);
}

}  // namespace wintek
