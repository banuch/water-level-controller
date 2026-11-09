#include "core/Types.h"

#include <string.h>

namespace wintek {

const char* toString(MotorState s) {
  switch (s) {
    case MotorState::BOOT_SELF_TEST: return "BOOT_SELF_TEST";
    case MotorState::AUTO_IDLE: return "AUTO_IDLE";
    case MotorState::AUTO_FILLING: return "AUTO_FILLING";
    case MotorState::MANUAL_IDLE: return "MANUAL_IDLE";
    case MotorState::MANUAL_RUNNING: return "MANUAL_RUNNING";
    case MotorState::SCHEDULE_WAIT: return "SCHEDULE_WAIT";
    case MotorState::FAULT_LOCKOUT: return "FAULT_LOCKOUT";
    case MotorState::OVERFLOW_LOCKOUT: return "OVERFLOW_LOCKOUT";
    case MotorState::SENSOR_FAULT: return "SENSOR_FAULT";
    case MotorState::OTA_MAINTENANCE: return "OTA_MAINTENANCE";
  }
  return "UNKNOWN";
}

const char* toString(ControlMode m) {
  switch (m) {
    case ControlMode::AUTO: return "AUTO";
    case ControlMode::MANUAL: return "MANUAL";
    case ControlMode::OFF: return "OFF";
  }
  return "UNKNOWN";
}

const char* toString(LocalSwitch s) {
  switch (s) {
    case LocalSwitch::OFF: return "OFF";
    case LocalSwitch::AUTO: return "AUTO";
    case LocalSwitch::MANUAL: return "MANUAL";
    case LocalSwitch::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* toString(FaultCode f) {
  switch (f) {
    case FaultCode::NONE: return "NONE";
    case FaultCode::SENSOR_SEQUENCE: return "SENSOR_SEQUENCE";
    case FaultCode::SENSOR_CHAIN: return "SENSOR_CHAIN";
    case FaultCode::SENSOR_STALE: return "SENSOR_STALE";
    case FaultCode::OVERFLOW_HH: return "OVERFLOW_HH";
    case FaultCode::MOTOR_OVERLOAD: return "MOTOR_OVERLOAD";
    case FaultCode::EMERGENCY_STOP: return "EMERGENCY_STOP";
    case FaultCode::CONTACTOR_NO_PULL_IN: return "CONTACTOR_NO_PULL_IN";
    case FaultCode::CONTACTOR_WELDED: return "CONTACTOR_WELDED";
    case FaultCode::CONTACTOR_DROPOUT: return "CONTACTOR_DROPOUT";
    case FaultCode::MAX_RUNTIME: return "MAX_RUNTIME";
    case FaultCode::NO_LEVEL_RISE: return "NO_LEVEL_RISE";
    case FaultCode::LOCAL_PANEL_FAULT: return "LOCAL_PANEL_FAULT";
  }
  return "UNKNOWN";
}

const char* toString(StopReason r) {
  switch (r) {
    case StopReason::NONE: return "NONE";
    case StopReason::LEVEL_REACHED: return "LEVEL_REACHED";
    case StopReason::USER_OFF: return "USER_OFF";
    case StopReason::LOCAL_STOP: return "LOCAL_STOP";
    case StopReason::MODE_CHANGE: return "MODE_CHANGE";
    case StopReason::LOCAL_SWITCH_OFF: return "LOCAL_SWITCH_OFF";
    case StopReason::SCHEDULE_WINDOW_END: return "SCHEDULE_WINDOW_END";
    case StopReason::SAFETY_TRIP: return "SAFETY_TRIP";
    case StopReason::SENSOR_FAULT: return "SENSOR_FAULT";
    case StopReason::OVERFLOW: return "OVERFLOW";
    case StopReason::OTA: return "OTA";
  }
  return "UNKNOWN";
}

const char* toString(StartReason r) {
  switch (r) {
    case StartReason::NONE: return "NONE";
    case StartReason::AUTO_LOW_LEVEL: return "AUTO_LOW_LEVEL";
    case StartReason::AUTO_SCHEDULED_FILL: return "AUTO_SCHEDULED_FILL";
    case StartReason::AUTO_USER_FILL: return "AUTO_USER_FILL";
    case StartReason::MANUAL_USER: return "MANUAL_USER";
    case StartReason::MANUAL_LOCAL: return "MANUAL_LOCAL";
    case StartReason::MODE_HANDOVER: return "MODE_HANDOVER";
  }
  return "UNKNOWN";
}

const char* toString(CommandAction a) {
  switch (a) {
    case CommandAction::MOTOR_ON: return "MOTOR_ON";
    case CommandAction::MOTOR_OFF: return "MOTOR_OFF";
    case CommandAction::SET_MODE: return "SET_MODE";
    case CommandAction::RESET_FAULT: return "RESET_FAULT";
    case CommandAction::ENTER_OTA: return "ENTER_OTA";
    case CommandAction::EXIT_OTA: return "EXIT_OTA";
  }
  return "UNKNOWN";
}

const char* toString(CommandSource s) {
  switch (s) {
    case CommandSource::LOCAL_PANEL: return "LOCAL_PANEL";
    case CommandSource::LOCAL_API: return "LOCAL_API";
    case CommandSource::CLOUD: return "CLOUD";
    case CommandSource::SCHEDULER: return "SCHEDULER";
    case CommandSource::SYSTEM: return "SYSTEM";
  }
  return "UNKNOWN";
}

const char* toString(CommandStatus s) {
  switch (s) {
    case CommandStatus::ACCEPTED: return "ACCEPTED";
    case CommandStatus::REJECTED: return "REJECTED";
    case CommandStatus::DUPLICATE: return "DUPLICATE";
    case CommandStatus::EXPIRED: return "EXPIRED";
    case CommandStatus::INVALID: return "INVALID";
    case CommandStatus::UNAUTHORIZED: return "UNAUTHORIZED";
    case CommandStatus::TIMEOUT: return "TIMEOUT";
  }
  return "UNKNOWN";
}

const char* toString(RejectReason r) {
  switch (r) {
    case RejectReason::NONE: return "NONE";
    case RejectReason::TANK_FULL: return "TANK_FULL";
    case RejectReason::OVERFLOW_ACTIVE: return "OVERFLOW_ACTIVE";
    case RejectReason::OVERLOAD_ACTIVE: return "OVERLOAD_ACTIVE";
    case RejectReason::EMERGENCY_STOP_ACTIVE: return "EMERGENCY_STOP_ACTIVE";
    case RejectReason::SENSOR_INVALID: return "SENSOR_INVALID";
    case RejectReason::FAULT_LOCKOUT_ACTIVE: return "FAULT_LOCKOUT_ACTIVE";
    case RejectReason::CONDITION_NOT_CLEARED: return "CONDITION_NOT_CLEARED";
    case RejectReason::MIN_OFF_TIME: return "MIN_OFF_TIME";
    case RejectReason::START_RATE_LIMIT: return "START_RATE_LIMIT";
    case RejectReason::RESTART_DELAY: return "RESTART_DELAY";
    case RejectReason::LOCAL_SWITCH_OFF: return "LOCAL_SWITCH_OFF";
    case RejectReason::LOCAL_SWITCH_MANUAL: return "LOCAL_SWITCH_MANUAL";
    case RejectReason::OTA_IN_PROGRESS: return "OTA_IN_PROGRESS";
    case RejectReason::NOT_IN_LOCKOUT: return "NOT_IN_LOCKOUT";
    case RejectReason::NOT_IN_OTA: return "NOT_IN_OTA";
    case RejectReason::ALREADY_IN_STATE: return "ALREADY_IN_STATE";
    case RejectReason::BOOTING: return "BOOTING";
    case RejectReason::MOTOR_NOT_STOPPED: return "MOTOR_NOT_STOPPED";
    case RejectReason::UNSUPPORTED: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

const char* toString(ProbeState p) {
  switch (p) {
    case ProbeState::DRY: return "DRY";
    case ProbeState::WET: return "WET";
    case ProbeState::FAULT: return "FAULT";
  }
  return "UNKNOWN";
}

bool parseControlMode(const char* s, ControlMode& out) {
  if (s == nullptr) return false;
  if (strcmp(s, "AUTO") == 0) { out = ControlMode::AUTO; return true; }
  if (strcmp(s, "MANUAL") == 0) { out = ControlMode::MANUAL; return true; }
  return false;  // OFF is a physical switch position only, never commandable
}

bool parseCommandAction(const char* s, CommandAction& out) {
  if (s == nullptr) return false;
  static const struct { const char* name; CommandAction a; } kMap[] = {
      {"MOTOR_ON", CommandAction::MOTOR_ON},       {"MOTOR_OFF", CommandAction::MOTOR_OFF},
      {"SET_MODE", CommandAction::SET_MODE},       {"RESET_FAULT", CommandAction::RESET_FAULT},
      {"ENTER_OTA", CommandAction::ENTER_OTA},     {"EXIT_OTA", CommandAction::EXIT_OTA},
  };
  for (const auto& e : kMap) {
    if (strcmp(s, e.name) == 0) { out = e.a; return true; }
  }
  return false;
}

bool isLatchingFault(FaultCode f) {
  switch (f) {
    case FaultCode::OVERFLOW_HH:
    case FaultCode::MOTOR_OVERLOAD:
    case FaultCode::EMERGENCY_STOP:
    case FaultCode::CONTACTOR_NO_PULL_IN:
    case FaultCode::CONTACTOR_WELDED:
    case FaultCode::CONTACTOR_DROPOUT:
    case FaultCode::MAX_RUNTIME:
    case FaultCode::NO_LEVEL_RISE:
      return true;
    default:
      return false;
  }
}

bool isSensorFault(FaultCode f) {
  return f == FaultCode::SENSOR_SEQUENCE || f == FaultCode::SENSOR_CHAIN ||
         f == FaultCode::SENSOR_STALE;
}

}  // namespace wintek
