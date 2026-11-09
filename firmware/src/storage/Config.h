// Device configuration model, defaults and validation (hardware independent).
// Persistence lives in ConfigStore (NVS); this file is unit-testable.
#pragma once

#include "core/Types.h"
#include "sensors/LevelEstimator.h"

namespace wintek {

constexpr uint8_t kMaxSchedules = 8;
constexpr uint16_t kConfigSchemaVersion = 1;

enum class ScheduleType : uint8_t {
  WINDOW = 0,  // AUTO filling is only permitted inside windows (when restriction enabled)
  FILL = 1,    // at startMinute, top the tank up to the OFF threshold
};

struct ScheduleEntry {
  bool enabled = false;
  ScheduleType type = ScheduleType::WINDOW;
  uint8_t daysMask = 0x7F;   // bit0 = Sunday ... bit6 = Saturday
  uint16_t startMinute = 0;  // minutes after local midnight, 0..1439
  uint16_t endMinute = 0;    // WINDOW only; may be < start (wraps past midnight)
};

struct DeviceConfig {
  uint16_t schemaVersion = kConfigSchemaVersion;
  char deviceName[32] = "Overhead Tank";

  // Tank geometry and probe installation (heights measured from tank bottom).
  uint16_t tankHeightMm = 1200;
  uint32_t tankCapacityLitres = 1000;
  uint8_t probeCount = 4;
  uint16_t probeHeightMm[kMaxProbes] = {300, 600, 900, 1140};  // 25/50/75/95 %

  // Thresholds are probe indices, so only installed probe positions can be chosen.
  uint8_t onProbeIndex = 0;   // start when this probe goes dry (default 25 %)
  uint8_t offProbeIndex = 3;  // stop when this probe goes wet  (default 95 %)

  // Sensing.
  uint16_t wetBelowKohm = 250;
  uint16_t dryAboveKohm = 500;
  uint16_t probeSettleUs = 2500;
  uint32_t sensorDebounceMs = 3000;
  uint32_t sensorFaultConfirmMs = 5000;
  uint32_t sensorRecoveryMs = 10000;

  // Motor timing protection.
  uint32_t minOnMs = 60000;
  uint32_t minOffMs = 120000;
  uint32_t maxRuntimeMs = 60UL * 60UL * 1000UL;
  uint32_t noRiseTimeoutMs = 20UL * 60UL * 1000UL;  // 0 disables
  uint32_t contactorFeedbackTimeoutMs = 2000;
  uint32_t restartDelayMs = 30000;
  uint32_t autoResumeAfterUserOffMs = 30UL * 60UL * 1000UL;
  uint8_t maxStartsPerHour = 6;

  // Installed hardware (lets the dev board run without the full I/O set).
  bool contactorFeedbackInstalled = true;
  bool overloadInputInstalled = true;
  bool localPanelInstalled = true;

  // Control.
  ControlMode savedMode = ControlMode::AUTO;  // software mode restored after reboot (never motor ON)

  // Scheduling.
  bool scheduleRestrictEnabled = false;
  bool scheduleStopAtWindowEnd = true;
  bool scheduleFailOpen = true;  // permit AUTO when local time is unknown
  char timezone[48] = "IST-5:30";  // POSIX TZ string
  ScheduleEntry schedules[kMaxSchedules];

  // Telemetry.
  uint16_t telemetryIntervalS = 30;
};

enum class ConfigError : uint8_t {
  OK = 0,
  TANK_HEIGHT,
  PROBE_COUNT,
  PROBE_ORDER,
  PROBE_SPACING,
  PROBE_TOP_CLEARANCE,
  THRESHOLD_INDEX,
  THRESHOLD_ORDER,
  SENSITIVITY,
  SETTLE_TIME,
  DEBOUNCE,
  FAULT_CONFIRM,
  RECOVERY,
  MIN_ON,
  MIN_OFF,
  MAX_RUNTIME,
  NO_RISE,
  FEEDBACK_TIMEOUT,
  RESTART_DELAY,
  AUTO_RESUME,
  STARTS_PER_HOUR,
  SCHEDULE,
  SCHEDULE_CONFLICT,
  TIMEZONE,
  TELEMETRY_INTERVAL,
  DEVICE_NAME,
  MODE,
  BAD_FIELD,  // wrong JSON type / unknown enum value
};

const char* toString(ConfigError e);

ConfigError validateConfig(const DeviceConfig& c);
ConfigError validateSchedules(const ScheduleEntry* entries, uint8_t count);

// Derived values.
uint8_t probePercent(const DeviceConfig& c, uint8_t index);
ProbeLayout makeProbeLayout(const DeviceConfig& c);

// Apply build-time hardware defaults (dev board vs product PCB).
void applyHardwareDefaults(DeviceConfig& c);

}  // namespace wintek
