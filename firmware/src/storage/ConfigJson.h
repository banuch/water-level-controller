// JSON <-> DeviceConfig mapping shared by the local API and MQTT config
// topic. Hardware independent (ArduinoJson).
#pragma once

#include <ArduinoJson.h>

#include "storage/Config.h"

namespace wintek {

void configToJson(const DeviceConfig& c, JsonObject out);

// Applies a partial update onto a copy of `inout`. On success `inout` holds
// the merged, fully validated configuration. `errorField` receives the
// offending JSON key or validation code.
ConfigError applyConfigPatch(JsonObjectConst patch, DeviceConfig& inout, const char*& errorField);

}  // namespace wintek
