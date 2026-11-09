// Wintek Smart Water Tank Controller
// Core shared types. Hardware independent: compiled both for the ESP32-C3
// target and for the host-side (native) unit tests.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace wintek {

constexpr uint8_t kMaxProbes = 4;  // level probes excluding the common and the HH pair

// ---------------------------------------------------------------------------
// Wrap-safe millisecond helpers (millis() wraps after ~49.7 days).
// ---------------------------------------------------------------------------
inline uint32_t elapsedMs(uint32_t now, uint32_t since) { return now - since; }
inline bool hasElapsed(uint32_t now, uint32_t since, uint32_t duration) {
  return static_cast<uint32_t>(now - since) >= duration;
}

// ---------------------------------------------------------------------------
// Motor control state machine
// ---------------------------------------------------------------------------
enum class MotorState : uint8_t {
  BOOT_SELF_TEST = 0,
  AUTO_IDLE,
  AUTO_FILLING,
  MANUAL_IDLE,
  MANUAL_RUNNING,
  SCHEDULE_WAIT,
  FAULT_LOCKOUT,
  OVERFLOW_LOCKOUT,
  SENSOR_FAULT,
  OTA_MAINTENANCE,
};

// Effective control mode. OFF means the local selector switch is in OFF
// (or the local panel is unreadable) and the motor is locked off.
enum class ControlMode : uint8_t { AUTO = 0, MANUAL = 1, OFF = 2 };

// Position of the physical three-position selector on the enclosure.
enum class LocalSwitch : uint8_t { OFF = 0, AUTO = 1, MANUAL = 2, UNKNOWN = 3 };

enum class FaultCode : uint8_t {
  NONE = 0,
  // Sensor faults (auto-recovering, state SENSOR_FAULT)
  SENSOR_SEQUENCE,       // impossible wet/dry combination (e.g. 75% wet, 50% dry)
  SENSOR_CHAIN,          // electrical self-test of a probe channel failed
  SENSOR_STALE,          // acquisition stopped producing samples
  // Overflow (latched, state OVERFLOW_LOCKOUT)
  OVERFLOW_HH,           // independent 100% high-high cutoff tripped
  // Equipment faults (latched, state FAULT_LOCKOUT)
  MOTOR_OVERLOAD,
  EMERGENCY_STOP,
  CONTACTOR_NO_PULL_IN,  // commanded ON, feedback never closed
  CONTACTOR_WELDED,      // commanded OFF, feedback stays closed
  CONTACTOR_DROPOUT,     // feedback opened while running without a known cause
  MAX_RUNTIME,
  NO_LEVEL_RISE,         // pump running but tank level not rising (dry run / supply)
  // Non-latching warnings
  LOCAL_PANEL_FAULT,
};

enum class StopReason : uint8_t {
  NONE = 0,
  LEVEL_REACHED,
  USER_OFF,
  LOCAL_STOP,
  MODE_CHANGE,
  LOCAL_SWITCH_OFF,
  SCHEDULE_WINDOW_END,
  SAFETY_TRIP,
  SENSOR_FAULT,
  OVERFLOW,
  OTA,
};

enum class StartReason : uint8_t {
  NONE = 0,
  AUTO_LOW_LEVEL,
  AUTO_SCHEDULED_FILL,
  AUTO_USER_FILL,
  MANUAL_USER,
  MANUAL_LOCAL,
  MODE_HANDOVER,
};

// ---------------------------------------------------------------------------
// Commands (identical semantics for local panel, local API and cloud MQTT)
// ---------------------------------------------------------------------------
enum class CommandAction : uint8_t {
  MOTOR_ON = 0,
  MOTOR_OFF,
  SET_MODE,
  RESET_FAULT,
  ENTER_OTA,
  EXIT_OTA,
};

enum class CommandSource : uint8_t { LOCAL_PANEL = 0, LOCAL_API, CLOUD, SCHEDULER, SYSTEM };

enum class CommandStatus : uint8_t {
  ACCEPTED = 0,
  REJECTED,
  DUPLICATE,
  EXPIRED,
  INVALID,
  UNAUTHORIZED,
  TIMEOUT,
};

enum class RejectReason : uint8_t {
  NONE = 0,
  TANK_FULL,
  OVERFLOW_ACTIVE,
  OVERLOAD_ACTIVE,
  EMERGENCY_STOP_ACTIVE,
  SENSOR_INVALID,
  FAULT_LOCKOUT_ACTIVE,
  CONDITION_NOT_CLEARED,
  MIN_OFF_TIME,
  START_RATE_LIMIT,
  RESTART_DELAY,
  LOCAL_SWITCH_OFF,
  LOCAL_SWITCH_MANUAL,
  OTA_IN_PROGRESS,
  NOT_IN_LOCKOUT,
  NOT_IN_OTA,
  ALREADY_IN_STATE,
  BOOTING,
  MOTOR_NOT_STOPPED,
  UNSUPPORTED,
};

struct Command {
  CommandAction action = CommandAction::MOTOR_OFF;
  CommandSource source = CommandSource::SYSTEM;
  ControlMode mode = ControlMode::AUTO;  // SET_MODE target / MOTOR_ON requested mode
  bool modeSpecified = false;            // MOTOR_ON with explicit "mode"
};

struct CommandOutcome {
  CommandStatus status = CommandStatus::REJECTED;
  RejectReason reason = RejectReason::NONE;
  uint32_t retryAfterMs = 0;  // for MIN_OFF_TIME / RESTART_DELAY / START_RATE_LIMIT
};

// ---------------------------------------------------------------------------
// Sensor types
// ---------------------------------------------------------------------------
enum class ProbeState : uint8_t { DRY = 0, WET = 1, FAULT = 2 };

struct LevelReading {
  bool valid = false;
  FaultCode fault = FaultCode::SENSOR_STALE;
  uint8_t wetCount = 0;      // number of contiguous wet probes counted from the bottom
  uint8_t probeCount = 0;
  uint8_t levelPercent = 0;  // discrete: position of highest wet probe (0 if none)
  uint8_t rangeLowPercent = 0;   // water is somewhere in [rangeLow, rangeHigh)
  uint8_t rangeHighPercent = 0;
};

// ---------------------------------------------------------------------------
// String conversion (stable identifiers used in JSON, logs and the app).
// ---------------------------------------------------------------------------
const char* toString(MotorState s);
const char* toString(ControlMode m);
const char* toString(LocalSwitch s);
const char* toString(FaultCode f);
const char* toString(StopReason r);
const char* toString(StartReason r);
const char* toString(CommandAction a);
const char* toString(CommandSource s);
const char* toString(CommandStatus s);
const char* toString(RejectReason r);
const char* toString(ProbeState p);

bool parseControlMode(const char* s, ControlMode& out);
bool parseCommandAction(const char* s, CommandAction& out);

bool isLatchingFault(FaultCode f);
bool isSensorFault(FaultCode f);

}  // namespace wintek
