// Wi-Fi station management with exponential backoff, plus a WPA2 SoftAP
// for provisioning ("Wintek-XXXX", passphrase = device setup code).
// Motor control never depends on Wi-Fi.
#pragma once

#include <stdint.h>

#include "storage/ConfigStore.h"

namespace wintek {

class WiFiManager {
 public:
  void begin(const char* deviceId, const char* setupCode);
  void loop(uint32_t nowMs);

  void startProvisioning(uint32_t nowMs);
  void stopProvisioning();
  bool provisioning() const { return provisioning_; }

  // Stores new credentials and reconnects. Returns false on invalid input.
  bool setCredentials(const char* ssid, const char* pass);
  void forgetCredentials();

  bool connected() const;
  int rssi() const;
  const char* hostname() const { return hostname_; }
  const char* apSsid() const { return apSsid_; }

 private:
  void connectNow(uint32_t nowMs);

  WifiCredentials creds_;
  char hostname_[32] = {};
  char apSsid_[32] = {};
  char apPass_[32] = {};
  bool provisioning_ = false;
  uint32_t provisioningSinceMs_ = 0;
  bool wasConnected_ = false;
  bool attemptActive_ = false;
  uint32_t attemptStartMs_ = 0;
  uint32_t nextAttemptMs_ = 0;
  uint32_t backoffMs_ = 0;
};

}  // namespace wintek
