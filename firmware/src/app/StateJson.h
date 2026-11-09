// JSON payload builders shared by MQTT and the local API so both report
// identical schemas (docs/MQTT_PROTOCOL.md).
#pragma once

#include <ArduinoJson.h>

#include "app/SharedState.h"
#include "services/AlertManager.h"

namespace wintek {

struct DeviceIdentityView {
  const char* deviceId;
  const char* bootId;
  int wifiRssi;  // 0 when not connected
  bool cloudConnected;
};

void writeTimestamp(JsonObject o, const char* key);
void buildTelemetry(JsonObject o, const DeviceSnapshot& s, const DeviceIdentityView& id);
void buildState(JsonObject o, const DeviceSnapshot& s, const DeviceIdentityView& id, const DeviceConfig& cfg);
void buildDiagnostics(JsonObject o, const DeviceSnapshot& s, const DeviceIdentityView& id);
void buildAlert(JsonObject o, const Alert& a, const DeviceIdentityView& id);
void buildAck(JsonObject o, const char* commandId, const CommandOutcome& out, bool duplicate, const DeviceSnapshot& s,
              const DeviceIdentityView& id);

}  // namespace wintek
