#include "app/StateJson.h"

#include <Arduino.h>
#include <esp_system.h>

#include "app_config.h"
#include "services/CommandProcessor.h"
#include "services/TimeService.h"

namespace wintek {

void writeTimestamp(JsonObject o, const char* key) {
  const int64_t t = TimeService::nowEpochS();
  if (t > 0) {
    char buf[24];
    formatIso8601(t, buf, sizeof(buf));
    o[key] = buf;
  } else {
    o[key] = nullptr;  // clock not yet synchronised; consumers use uptimeMs
  }
  o["uptimeMs"] = millis();
}

namespace {
void writeHeader(JsonObject o, const DeviceIdentityView& id) {
  o["schemaVersion"] = app::kSchemaVersion;
  o["deviceId"] = id.deviceId;
  writeTimestamp(o, "timestamp");
}

void writeLevel(JsonObject o, const DeviceSnapshot& s) {
  if (s.level.valid) {
    o["levelPercent"] = s.level.levelPercent;
    o["levelRangeLow"] = s.level.rangeLowPercent;
    o["levelRangeHigh"] = s.level.rangeHighPercent;
  } else {
    o["levelPercent"] = nullptr;
    o["levelRangeLow"] = nullptr;
    o["levelRangeHigh"] = nullptr;
  }
  o["levelType"] = "discrete";
}

const char* motorStateString(const DeviceSnapshot& s, bool feedbackInstalled) {
  if (!feedbackInstalled) return s.motorCommanded ? "ON" : "OFF";
  if (s.motorCommanded && s.contactorClosed) return "ON";
  if (!s.motorCommanded && !s.contactorClosed) return "OFF";
  return s.motorCommanded ? "STARTING" : "STOPPING";
}
}  // namespace

void buildTelemetry(JsonObject o, const DeviceSnapshot& s, const DeviceIdentityView& id) {
  writeHeader(o, id);
  writeLevel(o, s);
  const DeviceConfig cfg = SharedState::config();
  o["motorState"] = motorStateString(s, cfg.contactorFeedbackInstalled);
  o["controlMode"] = toString(s.mode);
  o["state"] = toString(s.state);
  o["wifiRssi"] = id.wifiRssi;
  o["sensorHealthy"] = s.level.valid;
  if (s.activeFault == FaultCode::NONE) {
    o["faultCode"] = nullptr;
  } else {
    o["faultCode"] = toString(s.activeFault);
  }
  o["runMs"] = s.currentRunMs;
}

void buildState(JsonObject o, const DeviceSnapshot& s, const DeviceIdentityView& id, const DeviceConfig& cfg) {
  buildTelemetry(o, s, id);
  o["seq"] = s.seq;
  o["bootId"] = id.bootId;
  o["firmwareVersion"] = app::kFirmwareVersion;
  o["softwareMode"] = toString(s.softwareMode);
  o["localSwitch"] = toString(s.localSwitch);
  o["motorCommanded"] = s.motorCommanded;
  if (cfg.contactorFeedbackInstalled) {
    o["contactorClosed"] = s.contactorClosed;
  } else {
    o["contactorClosed"] = nullptr;  // no auxiliary contact installed
  }
  o["overloadTripped"] = s.overloadTripped;
  o["overflowTripped"] = s.hhTripped;
  o["emergencyStop"] = s.estopActive;
  o["latchedFault"] = s.latchedFault == FaultCode::NONE ? nullptr : toString(s.latchedFault);
  o["lastStartReason"] = toString(s.lastStart);
  o["lastStopReason"] = toString(s.lastStop);
  o["startBlockedMs"] = s.startBlockedMs;
  o["fillDemand"] = s.fillDemand;
  o["autoInhibitedByUser"] = s.userInhibit;
  o["scheduleWindowOpen"] = s.scheduleWindowOpen;
  o["timeSynced"] = TimeService::valid();
  o["cloudConnected"] = id.cloudConnected;
  o["onThresholdPercent"] = probePercent(cfg, cfg.onProbeIndex);
  o["offThresholdPercent"] = probePercent(cfg, cfg.offProbeIndex);
  o["totalRuntimeS"] = s.totalRuntimeS;
  o["totalStarts"] = s.totalStarts;
  JsonArray probes = o["probes"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.probeCount; ++i) {
    JsonObject p = probes.add<JsonObject>();
    p["percent"] = probePercent(cfg, i);
    p["state"] = toString(s.probeState[i]);
  }
}

void buildDiagnostics(JsonObject o, const DeviceSnapshot& s, const DeviceIdentityView& id) {
  writeHeader(o, id);
  o["firmwareVersion"] = app::kFirmwareVersion;
  o["hardwareRevision"] = app::kHardwareRevision;
  o["bootId"] = id.bootId;
  o["resetReason"] = static_cast<int>(esp_reset_reason());
  o["freeHeap"] = ESP.getFreeHeap();
  o["minFreeHeap"] = ESP.getMinFreeHeap();
  o["wifiRssi"] = id.wifiRssi;
  o["panelOk"] = s.panelOk;
  o["unusedProbeWet"] = s.unusedProbeWet;
  JsonArray ch = o["probeChannels"].to<JsonArray>();
  for (uint8_t i = 0; i < kMaxProbes; ++i) {
    JsonObject c = ch.add<JsonObject>();
    c["index"] = i;
    c["instant"] = toString(s.probeSample[i].state);
    c["committed"] = toString(s.probeState[i]);
    c["vAmV"] = static_cast<int>(s.probeSample[i].vAMv);
    c["vBmV"] = static_cast<int>(s.probeSample[i].vBMv);
    if (s.probeSample[i].waterKohm >= 0) {
      c["waterKohm"] = static_cast<int>(s.probeSample[i].waterKohm);
    } else {
      c["waterKohm"] = nullptr;  // open circuit
    }
  }
}

void buildAlert(JsonObject o, const Alert& a, const DeviceIdentityView& id) {
  o["schemaVersion"] = app::kSchemaVersion;
  o["deviceId"] = id.deviceId;
  o["bootId"] = id.bootId;
  o["alertSeq"] = a.seq;
  o["type"] = toString(a.type);
  o["severity"] = toString(a.severity);
  o["eventUptimeMs"] = a.atMs;
  // Convert the event's uptime into wall-clock time when available.
  const int64_t now = TimeService::nowEpochS();
  if (now > 0) {
    char buf[24];
    formatIso8601(now - static_cast<int64_t>((millis() - a.atMs) / 1000U), buf, sizeof(buf));
    o["timestamp"] = buf;
  } else {
    o["timestamp"] = nullptr;
  }
  o["faultCode"] = a.fault == FaultCode::NONE ? nullptr : toString(a.fault);
  if (a.startReason != StartReason::NONE) o["startReason"] = toString(a.startReason);
  if (a.stopReason != StopReason::NONE) o["stopReason"] = toString(a.stopReason);
  o["levelPercent"] = a.levelPercent;
  if (a.runtimeMs > 0) o["runtimeMs"] = a.runtimeMs;
  if (a.suppressedRepeats > 0) o["suppressedRepeats"] = a.suppressedRepeats;
}

void buildAck(JsonObject o, const char* commandId, const CommandOutcome& out, bool duplicate, const DeviceSnapshot& s,
              const DeviceIdentityView& id) {
  writeHeader(o, id);
  o["commandId"] = commandId;
  o["status"] = duplicate ? "DUPLICATE" : toString(out.status);
  o["result"] = toString(out.status);
  o["reason"] = out.reason == RejectReason::NONE ? nullptr : toString(out.reason);
  if (out.retryAfterMs > 0) o["retryAfterMs"] = out.retryAfterMs;
  JsonObject st = o["observed"].to<JsonObject>();
  st["state"] = toString(s.state);
  st["controlMode"] = toString(s.mode);
  st["motorCommanded"] = s.motorCommanded;
  st["contactorClosed"] = s.contactorClosed;
  st["seq"] = s.seq;
}

}  // namespace wintek
