// Network task: Wi-Fi, MQTT, local API, alerts delivery, OTA orchestration
// and the factory serial console. Runs at lower priority than control.
#pragma once

#include "storage/ConfigStore.h"

namespace wintek {

class NetworkTask {
 public:
  static void start(const FactoryIdentity& id, const char* bootId);
};

}  // namespace wintek
