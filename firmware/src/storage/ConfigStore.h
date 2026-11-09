// NVS persistence for configuration, factory identity, Wi-Fi credentials,
// paired local clients, latched faults and OTA bookkeeping.
//
// SECURITY NOTE: the Arduino-ESP32 prebuilt SDK does not enable flash
// encryption / NVS encryption. Production units must be built with flash
// encryption + NVS encryption enabled (ESP-IDF or Arduino-as-component) so
// Wi-Fi passwords, MQTT credentials and pairing tokens are not readable
// from a dumped flash. See docs/ARCHITECTURE.md §9.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/Types.h"
#include "storage/Config.h"

namespace wintek {

struct FactoryIdentity {
  char deviceId[24] = {};    // e.g. WT-TANK-0001 (falls back to WT-<MAC>)
  char setupCode[24] = {};   // printed on the device label; >= 12 chars in production
  char hwRev[16] = {};
  char mqttUri[96] = {};     // mqtts://broker.example.com:8883
  char mqttUser[48] = {};
  char mqttPass[96] = {};
};

struct WifiCredentials {
  char ssid[33] = {};
  char pass[65] = {};
  bool valid() const { return ssid[0] != '\0'; }
};

struct PairedClient {
  bool used = false;
  char tokenId[9] = {};  // 8 hex chars
  uint8_t token[32] = {};
  char name[24] = {};
};

struct PersistentState {
  FaultCode latchedFault = FaultCode::NONE;
  ControlMode mode = ControlMode::AUTO;
  uint32_t totalRuntimeS = 0;
  uint32_t totalStarts = 0;
  uint32_t bootCount = 0;
};

struct OtaBookkeeping {
  bool pendingVerify = false;
  uint8_t attempts = 0;
  char previousLabel[17] = {};
  char targetVersion[24] = {};
};

class ConfigStore {
 public:
  static bool begin();

  static bool loadConfig(DeviceConfig& out);  // false => defaults returned
  static bool saveConfig(const DeviceConfig& c);

  static void loadFactory(FactoryIdentity& out);
  static bool saveFactoryField(const char* key, const char* value);
  // Large PEM blobs (CA / client certificate / key). Returns length or 0.
  static size_t loadPem(const char* key, char* out, size_t cap);
  static bool savePem(const char* key, const char* pem);

  static bool loadWifi(WifiCredentials& out);
  static bool saveWifi(const WifiCredentials& c);
  static void clearWifi();

  static uint8_t loadClients(PairedClient out[], uint8_t max);
  static bool saveClient(uint8_t slot, const PairedClient& c);
  static void clearClient(uint8_t slot);

  static void loadState(PersistentState& out);
  static void saveLatch(FaultCode f);
  static void saveMode(ControlMode m);
  static void saveCounters(uint32_t runtimeS, uint32_t starts);
  static uint32_t incrementBootCount();

  static void loadOta(OtaBookkeeping& out);
  static void saveOta(const OtaBookkeeping& o);
  static void clearOta();

  // Erases user data (config, Wi-Fi, pairing, state, OTA). Factory identity kept.
  static void factoryReset();
};

}  // namespace wintek
