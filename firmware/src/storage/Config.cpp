#include "storage/Config.h"

#include <string.h>

namespace wintek {

const char* toString(ConfigError e) {
  switch (e) {
    case ConfigError::OK: return "OK";
    case ConfigError::TANK_HEIGHT: return "TANK_HEIGHT";
    case ConfigError::PROBE_COUNT: return "PROBE_COUNT";
    case ConfigError::PROBE_ORDER: return "PROBE_ORDER";
    case ConfigError::PROBE_SPACING: return "PROBE_SPACING";
    case ConfigError::PROBE_TOP_CLEARANCE: return "PROBE_TOP_CLEARANCE";
    case ConfigError::THRESHOLD_INDEX: return "THRESHOLD_INDEX";
    case ConfigError::THRESHOLD_ORDER: return "THRESHOLD_ORDER";
    case ConfigError::SENSITIVITY: return "SENSITIVITY";
    case ConfigError::SETTLE_TIME: return "SETTLE_TIME";
    case ConfigError::DEBOUNCE: return "DEBOUNCE";
    case ConfigError::FAULT_CONFIRM: return "FAULT_CONFIRM";
    case ConfigError::RECOVERY: return "RECOVERY";
    case ConfigError::MIN_ON: return "MIN_ON";
    case ConfigError::MIN_OFF: return "MIN_OFF";
    case ConfigError::MAX_RUNTIME: return "MAX_RUNTIME";
    case ConfigError::NO_RISE: return "NO_RISE";
    case ConfigError::FEEDBACK_TIMEOUT: return "FEEDBACK_TIMEOUT";
    case ConfigError::RESTART_DELAY: return "RESTART_DELAY";
    case ConfigError::AUTO_RESUME: return "AUTO_RESUME";
    case ConfigError::STARTS_PER_HOUR: return "STARTS_PER_HOUR";
    case ConfigError::SCHEDULE: return "SCHEDULE";
    case ConfigError::SCHEDULE_CONFLICT: return "SCHEDULE_CONFLICT";
    case ConfigError::TIMEZONE: return "TIMEZONE";
    case ConfigError::TELEMETRY_INTERVAL: return "TELEMETRY_INTERVAL";
    case ConfigError::DEVICE_NAME: return "DEVICE_NAME";
    case ConfigError::MODE: return "MODE";
    case ConfigError::BAD_FIELD: return "BAD_FIELD";
  }
  return "UNKNOWN";
}

namespace {
bool inRange(uint32_t v, uint32_t lo, uint32_t hi) { return v >= lo && v <= hi; }
}  // namespace

ConfigError validateSchedules(const ScheduleEntry* e, uint8_t count) {
  if (count > kMaxSchedules) return ConfigError::SCHEDULE;
  for (uint8_t i = 0; i < count; ++i) {
    if (!e[i].enabled) continue;
    if ((e[i].daysMask & 0x7F) == 0 || (e[i].daysMask & 0x80) != 0) return ConfigError::SCHEDULE;
    if (e[i].startMinute > 1439) return ConfigError::SCHEDULE;
    if (e[i].type == ScheduleType::WINDOW) {
      if (e[i].endMinute > 1439 || e[i].endMinute == e[i].startMinute) return ConfigError::SCHEDULE;
    } else if (e[i].type != ScheduleType::FILL) {
      return ConfigError::SCHEDULE;
    }
    // Two FILL entries closer than 30 minutes on a shared day are a conflict:
    // the second one could only ever restart a just-stopped motor.
    if (e[i].type == ScheduleType::FILL) {
      for (uint8_t j = i + 1; j < count; ++j) {
        if (!e[j].enabled || e[j].type != ScheduleType::FILL) continue;
        if ((e[i].daysMask & e[j].daysMask) == 0) continue;
        int d = static_cast<int>(e[i].startMinute) - static_cast<int>(e[j].startMinute);
        if (d < 0) d = -d;
        if (d > 720) d = 1440 - d;
        if (d < 30) return ConfigError::SCHEDULE_CONFLICT;
      }
    }
  }
  return ConfigError::OK;
}

ConfigError validateConfig(const DeviceConfig& c) {
  const size_t nameLen = strnlen(c.deviceName, sizeof(c.deviceName));
  if (nameLen == 0 || nameLen >= sizeof(c.deviceName)) return ConfigError::DEVICE_NAME;

  if (!inRange(c.tankHeightMm, 300, 5000)) return ConfigError::TANK_HEIGHT;
  if (!inRange(c.probeCount, 2, kMaxProbes)) return ConfigError::PROBE_COUNT;
  for (uint8_t i = 0; i < c.probeCount; ++i) {
    if (c.probeHeightMm[i] == 0) return ConfigError::PROBE_ORDER;
    if (i > 0) {
      if (c.probeHeightMm[i] <= c.probeHeightMm[i - 1]) return ConfigError::PROBE_ORDER;
      // Electrodes closer than 50 mm are bridged by ripples/splash and foam.
      if (c.probeHeightMm[i] - c.probeHeightMm[i - 1] < 50) return ConfigError::PROBE_SPACING;
    }
  }
  // The independent 100 % high-high pair sits above the top level probe:
  // keep at least 30 mm between the top level probe and the tank top.
  if (c.probeHeightMm[c.probeCount - 1] + 30 > c.tankHeightMm) return ConfigError::PROBE_TOP_CLEARANCE;

  if (c.onProbeIndex >= c.probeCount || c.offProbeIndex >= c.probeCount) return ConfigError::THRESHOLD_INDEX;
  if (c.onProbeIndex >= c.offProbeIndex) return ConfigError::THRESHOLD_ORDER;

  if (!inRange(c.wetBelowKohm, 5, 3000) || !inRange(c.dryAboveKohm, 10, 5000) ||
      c.dryAboveKohm < c.wetBelowKohm + c.wetBelowKohm / 4) {
    return ConfigError::SENSITIVITY;
  }
  if (!inRange(c.probeSettleUs, 200, 10000)) return ConfigError::SETTLE_TIME;
  if (!inRange(c.sensorDebounceMs, 500, 30000)) return ConfigError::DEBOUNCE;
  if (!inRange(c.sensorFaultConfirmMs, 1000, 60000)) return ConfigError::FAULT_CONFIRM;
  if (!inRange(c.sensorRecoveryMs, 2000, 300000)) return ConfigError::RECOVERY;

  if (!inRange(c.minOnMs, 0, 600000)) return ConfigError::MIN_ON;
  if (!inRange(c.minOffMs, 30000, 1800000)) return ConfigError::MIN_OFF;
  if (!inRange(c.maxRuntimeMs, 5UL * 60000UL, 4UL * 3600000UL)) return ConfigError::MAX_RUNTIME;
  if (c.minOnMs >= c.maxRuntimeMs) return ConfigError::MIN_ON;
  if (c.noRiseTimeoutMs != 0 &&
      (c.noRiseTimeoutMs < 2UL * 60000UL || c.noRiseTimeoutMs > c.maxRuntimeMs)) {
    return ConfigError::NO_RISE;
  }
  if (!inRange(c.contactorFeedbackTimeoutMs, 500, 10000)) return ConfigError::FEEDBACK_TIMEOUT;
  if (!inRange(c.restartDelayMs, 5000, 600000)) return ConfigError::RESTART_DELAY;
  if (!inRange(c.autoResumeAfterUserOffMs, 60000, 24UL * 3600000UL)) return ConfigError::AUTO_RESUME;
  if (!inRange(c.maxStartsPerHour, 1, 30)) return ConfigError::STARTS_PER_HOUR;

  if (c.savedMode != ControlMode::AUTO && c.savedMode != ControlMode::MANUAL) return ConfigError::MODE;

  const size_t tzLen = strnlen(c.timezone, sizeof(c.timezone));
  if (tzLen < 3 || tzLen >= sizeof(c.timezone)) return ConfigError::TIMEZONE;

  if (!inRange(c.telemetryIntervalS, 5, 3600)) return ConfigError::TELEMETRY_INTERVAL;

  return validateSchedules(c.schedules, kMaxSchedules);
}

uint8_t probePercent(const DeviceConfig& c, uint8_t index) {
  if (index >= c.probeCount || c.tankHeightMm == 0) return 0;
  uint32_t p = (static_cast<uint32_t>(c.probeHeightMm[index]) * 100U + c.tankHeightMm / 2) / c.tankHeightMm;
  if (p > 99) p = 99;
  if (p < 1) p = 1;
  return static_cast<uint8_t>(p);
}

ProbeLayout makeProbeLayout(const DeviceConfig& c) {
  ProbeLayout l;
  l.probeCount = c.probeCount;
  for (uint8_t i = 0; i < kMaxProbes; ++i) l.positionPercent[i] = probePercent(c, i);
  return l;
}

void applyHardwareDefaults(DeviceConfig& c) {
#if defined(WINTEK_DEVBOARD_DEFAULTS)
  // Bare ESP32-C3 dev board on the bench: no contactor auxiliary contact,
  // no overload relay and no I2C front panel unless explicitly enabled.
  c.contactorFeedbackInstalled = false;
  c.overloadInputInstalled = false;
  c.localPanelInstalled = false;
#else
  (void)c;
#endif
}

}  // namespace wintek
