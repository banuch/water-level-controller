#include "storage/ConfigJson.h"

#include <string.h>

namespace wintek {

namespace {
const char* typeName(ScheduleType t) { return t == ScheduleType::FILL ? "FILL" : "WINDOW"; }

template <typename T>
bool readUint(JsonObjectConst o, const char* key, T& dst, bool& bad, const char*& field) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return false;
  if (!v.is<uint32_t>()) {
    bad = true;
    field = key;
    return false;
  }
  const uint32_t x = v.as<uint32_t>();
  if (x > static_cast<uint32_t>(static_cast<T>(~static_cast<T>(0)))) {
    bad = true;
    field = key;
    return false;
  }
  dst = static_cast<T>(x);
  return true;
}

bool readBool(JsonObjectConst o, const char* key, bool& dst, bool& bad, const char*& field) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return false;
  if (!v.is<bool>()) {
    bad = true;
    field = key;
    return false;
  }
  dst = v.as<bool>();
  return true;
}

bool readStr(JsonObjectConst o, const char* key, char* dst, size_t cap, bool& bad, const char*& field) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return false;
  const char* s = v.as<const char*>();
  if (s == nullptr || strlen(s) >= cap) {
    bad = true;
    field = key;
    return false;
  }
  strcpy(dst, s);
  return true;
}
}  // namespace

void configToJson(const DeviceConfig& c, JsonObject o) {
  o["schemaVersion"] = c.schemaVersion;
  o["deviceName"] = c.deviceName;
  JsonObject tank = o["tank"].to<JsonObject>();
  tank["heightMm"] = c.tankHeightMm;
  tank["capacityLitres"] = c.tankCapacityLitres;
  tank["probeCount"] = c.probeCount;
  JsonArray probes = tank["probes"].to<JsonArray>();
  for (uint8_t i = 0; i < c.probeCount; ++i) {
    JsonObject p = probes.add<JsonObject>();
    p["index"] = i;
    p["heightMm"] = c.probeHeightMm[i];
    p["percent"] = probePercent(c, i);
  }
  JsonObject th = o["thresholds"].to<JsonObject>();
  th["onProbeIndex"] = c.onProbeIndex;
  th["offProbeIndex"] = c.offProbeIndex;
  th["onPercent"] = probePercent(c, c.onProbeIndex);
  th["offPercent"] = probePercent(c, c.offProbeIndex);

  JsonObject s = o["sensing"].to<JsonObject>();
  s["wetBelowKohm"] = c.wetBelowKohm;
  s["dryAboveKohm"] = c.dryAboveKohm;
  s["probeSettleUs"] = c.probeSettleUs;
  s["debounceMs"] = c.sensorDebounceMs;
  s["faultConfirmMs"] = c.sensorFaultConfirmMs;
  s["recoveryMs"] = c.sensorRecoveryMs;

  JsonObject m = o["motor"].to<JsonObject>();
  m["minOnMs"] = c.minOnMs;
  m["minOffMs"] = c.minOffMs;
  m["maxRuntimeMs"] = c.maxRuntimeMs;
  m["noRiseTimeoutMs"] = c.noRiseTimeoutMs;
  m["contactorFeedbackTimeoutMs"] = c.contactorFeedbackTimeoutMs;
  m["restartDelayMs"] = c.restartDelayMs;
  m["autoResumeAfterUserOffMs"] = c.autoResumeAfterUserOffMs;
  m["maxStartsPerHour"] = c.maxStartsPerHour;

  JsonObject hw = o["hardware"].to<JsonObject>();
  hw["contactorFeedbackInstalled"] = c.contactorFeedbackInstalled;
  hw["overloadInputInstalled"] = c.overloadInputInstalled;
  hw["localPanelInstalled"] = c.localPanelInstalled;

  JsonObject sc = o["schedule"].to<JsonObject>();
  sc["restrictEnabled"] = c.scheduleRestrictEnabled;
  sc["stopAtWindowEnd"] = c.scheduleStopAtWindowEnd;
  sc["failOpen"] = c.scheduleFailOpen;
  sc["timezone"] = c.timezone;
  JsonArray entries = sc["entries"].to<JsonArray>();
  for (uint8_t i = 0; i < kMaxSchedules; ++i) {
    const ScheduleEntry& e = c.schedules[i];
    JsonObject j = entries.add<JsonObject>();
    j["enabled"] = e.enabled;
    j["type"] = typeName(e.type);
    j["daysMask"] = e.daysMask;
    j["startMinute"] = e.startMinute;
    j["endMinute"] = e.endMinute;
  }
  o["telemetryIntervalS"] = c.telemetryIntervalS;
}

ConfigError applyConfigPatch(JsonObjectConst p, DeviceConfig& inout, const char*& field) {
  DeviceConfig c = inout;
  bool bad = false;
  field = nullptr;

  readStr(p, "deviceName", c.deviceName, sizeof(c.deviceName), bad, field);

  JsonObjectConst tank = p["tank"].as<JsonObjectConst>();
  if (!tank.isNull()) {
    readUint(tank, "heightMm", c.tankHeightMm, bad, field);
    readUint(tank, "capacityLitres", c.tankCapacityLitres, bad, field);
    readUint(tank, "probeCount", c.probeCount, bad, field);
    JsonArrayConst probes = tank["probes"].as<JsonArrayConst>();
    if (!probes.isNull()) {
      if (probes.size() > kMaxProbes) {
        bad = true;
        field = "tank.probes";
      }
      uint8_t i = 0;
      for (JsonVariantConst v : probes) {
        if (i >= kMaxProbes) break;
        if (!v["heightMm"].is<uint16_t>()) {
          bad = true;
          field = "tank.probes.heightMm";
          break;
        }
        c.probeHeightMm[i++] = v["heightMm"].as<uint16_t>();
      }
    }
  }

  JsonObjectConst th = p["thresholds"].as<JsonObjectConst>();
  if (!th.isNull()) {
    readUint(th, "onProbeIndex", c.onProbeIndex, bad, field);
    readUint(th, "offProbeIndex", c.offProbeIndex, bad, field);
    // Percent form: must match an installed probe exactly (no interpolation).
    auto byPercent = [&c](uint8_t pct, uint8_t& idx) {
      for (uint8_t i = 0; i < c.probeCount; ++i) {
        if (probePercent(c, i) == pct) {
          idx = i;
          return true;
        }
      }
      return false;
    };
    uint8_t pct = 0;
    if (readUint(th, "onPercent", pct, bad, field) && !byPercent(pct, c.onProbeIndex)) {
      bad = true;
      field = "thresholds.onPercent";
    }
    if (readUint(th, "offPercent", pct, bad, field) && !byPercent(pct, c.offProbeIndex)) {
      bad = true;
      field = "thresholds.offPercent";
    }
  }

  JsonObjectConst s = p["sensing"].as<JsonObjectConst>();
  if (!s.isNull()) {
    readUint(s, "wetBelowKohm", c.wetBelowKohm, bad, field);
    readUint(s, "dryAboveKohm", c.dryAboveKohm, bad, field);
    readUint(s, "probeSettleUs", c.probeSettleUs, bad, field);
    readUint(s, "debounceMs", c.sensorDebounceMs, bad, field);
    readUint(s, "faultConfirmMs", c.sensorFaultConfirmMs, bad, field);
    readUint(s, "recoveryMs", c.sensorRecoveryMs, bad, field);
  }

  JsonObjectConst m = p["motor"].as<JsonObjectConst>();
  if (!m.isNull()) {
    readUint(m, "minOnMs", c.minOnMs, bad, field);
    readUint(m, "minOffMs", c.minOffMs, bad, field);
    readUint(m, "maxRuntimeMs", c.maxRuntimeMs, bad, field);
    readUint(m, "noRiseTimeoutMs", c.noRiseTimeoutMs, bad, field);
    readUint(m, "contactorFeedbackTimeoutMs", c.contactorFeedbackTimeoutMs, bad, field);
    readUint(m, "restartDelayMs", c.restartDelayMs, bad, field);
    readUint(m, "autoResumeAfterUserOffMs", c.autoResumeAfterUserOffMs, bad, field);
    readUint(m, "maxStartsPerHour", c.maxStartsPerHour, bad, field);
  }

  JsonObjectConst hw = p["hardware"].as<JsonObjectConst>();
  if (!hw.isNull()) {
    readBool(hw, "contactorFeedbackInstalled", c.contactorFeedbackInstalled, bad, field);
    readBool(hw, "overloadInputInstalled", c.overloadInputInstalled, bad, field);
    readBool(hw, "localPanelInstalled", c.localPanelInstalled, bad, field);
  }

  JsonObjectConst sc = p["schedule"].as<JsonObjectConst>();
  if (!sc.isNull()) {
    readBool(sc, "restrictEnabled", c.scheduleRestrictEnabled, bad, field);
    readBool(sc, "stopAtWindowEnd", c.scheduleStopAtWindowEnd, bad, field);
    readBool(sc, "failOpen", c.scheduleFailOpen, bad, field);
    readStr(sc, "timezone", c.timezone, sizeof(c.timezone), bad, field);
    JsonArrayConst entries = sc["entries"].as<JsonArrayConst>();
    if (!entries.isNull()) {
      if (entries.size() > kMaxSchedules) {
        bad = true;
        field = "schedule.entries";
      }
      for (uint8_t i = 0; i < kMaxSchedules; ++i) c.schedules[i] = ScheduleEntry();
      uint8_t i = 0;
      for (JsonVariantConst v : entries) {
        if (i >= kMaxSchedules) break;
        JsonObjectConst e = v.as<JsonObjectConst>();
        ScheduleEntry& d = c.schedules[i++];
        readBool(e, "enabled", d.enabled, bad, field);
        const char* t = e["type"].as<const char*>();
        if (t != nullptr && strcmp(t, "FILL") == 0) {
          d.type = ScheduleType::FILL;
        } else if (t == nullptr || strcmp(t, "WINDOW") == 0) {
          d.type = ScheduleType::WINDOW;
        } else {
          bad = true;
          field = "schedule.entries.type";
        }
        readUint(e, "daysMask", d.daysMask, bad, field);
        readUint(e, "startMinute", d.startMinute, bad, field);
        readUint(e, "endMinute", d.endMinute, bad, field);
      }
    }
  }
  readUint(p, "telemetryIntervalS", c.telemetryIntervalS, bad, field);

  if (bad) {
    if (field == nullptr) field = "payload";
    return ConfigError::BAD_FIELD;
  }
  const ConfigError e = validateConfig(c);
  if (e != ConfigError::OK) {
    field = toString(e);
    return e;
  }
  inout = c;
  return ConfigError::OK;
}

}  // namespace wintek
