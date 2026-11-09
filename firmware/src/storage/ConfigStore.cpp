#include "storage/ConfigStore.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <string.h>

#include "app_config.h"
#include "diagnostics/Logger.h"

namespace wintek {

namespace {
constexpr const char* kNsCfg = "cfg";
constexpr const char* kNsFactory = "factory";
constexpr const char* kNsWifi = "wifi";
constexpr const char* kNsPair = "pair";
constexpr const char* kNsState = "state";
constexpr const char* kNsOta = "ota";

// Blob header guards against layout changes between firmware versions.
struct ConfigBlobHeader {
  uint16_t schema;
  uint16_t size;
  uint32_t crc;
};

uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    c ^= d[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

void copyField(Preferences& p, const char* key, char* dst, size_t cap) {
  dst[0] = '\0';
  if (p.isKey(key)) p.getString(key, dst, cap);
}
}  // namespace

bool ConfigStore::begin() {
  // nvs_flash_init() is performed by the Arduino core; verify access.
  Preferences p;
  const bool ok = p.begin(kNsState, false);
  p.end();
  return ok;
}

bool ConfigStore::loadConfig(DeviceConfig& out) {
  out = DeviceConfig();
  applyHardwareDefaults(out);
  Preferences p;
  if (!p.begin(kNsCfg, true)) return false;
  const size_t len = p.getBytesLength("blob");
  bool ok = false;
  if (len == sizeof(ConfigBlobHeader) + sizeof(DeviceConfig)) {
    uint8_t* buf = new uint8_t[len];
    p.getBytes("blob", buf, len);
    ConfigBlobHeader h;
    memcpy(&h, buf, sizeof(h));
    if (h.schema == kConfigSchemaVersion && h.size == sizeof(DeviceConfig) &&
        h.crc == crc32(buf + sizeof(h), sizeof(DeviceConfig))) {
      DeviceConfig c;
      memcpy(&c, buf + sizeof(h), sizeof(DeviceConfig));
      if (validateConfig(c) == ConfigError::OK) {
        out = c;
        ok = true;
      }
    }
    delete[] buf;
  }
  p.end();
  if (!ok) LOGW("nvs", "config missing/invalid, using defaults");
  return ok;
}

bool ConfigStore::saveConfig(const DeviceConfig& c) {
  if (validateConfig(c) != ConfigError::OK) return false;
  uint8_t buf[sizeof(ConfigBlobHeader) + sizeof(DeviceConfig)];
  ConfigBlobHeader h{kConfigSchemaVersion, static_cast<uint16_t>(sizeof(DeviceConfig)),
                     crc32(reinterpret_cast<const uint8_t*>(&c), sizeof(DeviceConfig))};
  memcpy(buf, &h, sizeof(h));
  memcpy(buf + sizeof(h), &c, sizeof(DeviceConfig));
  Preferences p;
  if (!p.begin(kNsCfg, false)) return false;
  const bool ok = p.putBytes("blob", buf, sizeof(buf)) == sizeof(buf);
  p.end();
  return ok;
}

void ConfigStore::loadFactory(FactoryIdentity& out) {
  out = FactoryIdentity();
  Preferences p;
  if (p.begin(kNsFactory, true)) {
    copyField(p, "devId", out.deviceId, sizeof(out.deviceId));
    copyField(p, "setup", out.setupCode, sizeof(out.setupCode));
    copyField(p, "hwRev", out.hwRev, sizeof(out.hwRev));
    copyField(p, "mqUri", out.mqttUri, sizeof(out.mqttUri));
    copyField(p, "mqUser", out.mqttUser, sizeof(out.mqttUser));
    copyField(p, "mqPass", out.mqttPass, sizeof(out.mqttPass));
    p.end();
  }
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (out.deviceId[0] == '\0') {
    snprintf(out.deviceId, sizeof(out.deviceId), "WT-%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
  }
  if (out.hwRev[0] == '\0') strncpy(out.hwRev, app::kHardwareRevision, sizeof(out.hwRev) - 1);
  if (out.setupCode[0] == '\0') {
    // Development fallback only. Production units are provisioned with a
    // random code at end-of-line test (tools/factory_provision.py).
    snprintf(out.setupCode, sizeof(out.setupCode), "WT%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    LOGW("nvs", "no factory setup code: using MAC-derived development code");
  }
  if (out.mqttUser[0] == '\0') strncpy(out.mqttUser, out.deviceId, sizeof(out.mqttUser) - 1);
}

bool ConfigStore::saveFactoryField(const char* key, const char* value) {
  static const char* const kAllowed[] = {"devId", "setup", "hwRev", "mqUri", "mqUser", "mqPass"};
  bool allowed = false;
  for (const char* k : kAllowed) allowed |= strcmp(k, key) == 0;
  if (!allowed) return false;
  Preferences p;
  if (!p.begin(kNsFactory, false)) return false;
  const bool ok = p.putString(key, value) == strlen(value);
  p.end();
  return ok;
}

size_t ConfigStore::loadPem(const char* key, char* out, size_t cap) {
  Preferences p;
  if (!p.begin(kNsFactory, true)) return 0;
  size_t n = 0;
  if (p.isKey(key)) n = p.getString(key, out, cap);
  p.end();
  return (n > 0 && out[0] == '-') ? strlen(out) : 0;
}

bool ConfigStore::savePem(const char* key, const char* pem) {
  if (strcmp(key, "mqCa") != 0 && strcmp(key, "mqCert") != 0 && strcmp(key, "mqKey") != 0 &&
      strcmp(key, "otaCa") != 0) {
    return false;
  }
  if (strncmp(pem, "-----BEGIN ", 11) != 0) return false;
  Preferences p;
  if (!p.begin(kNsFactory, false)) return false;
  const bool ok = p.putString(key, pem) == strlen(pem);
  p.end();
  return ok;
}

bool ConfigStore::loadWifi(WifiCredentials& out) {
  out = WifiCredentials();
  Preferences p;
  if (!p.begin(kNsWifi, true)) return false;
  copyField(p, "ssid", out.ssid, sizeof(out.ssid));
  copyField(p, "pass", out.pass, sizeof(out.pass));
  p.end();
  return out.valid();
}

bool ConfigStore::saveWifi(const WifiCredentials& c) {
  Preferences p;
  if (!p.begin(kNsWifi, false)) return false;
  const bool ok = p.putString("ssid", c.ssid) == strlen(c.ssid) && p.putString("pass", c.pass) == strlen(c.pass);
  p.end();
  return ok;
}

void ConfigStore::clearWifi() {
  Preferences p;
  if (p.begin(kNsWifi, false)) {
    p.clear();
    p.end();
  }
}

uint8_t ConfigStore::loadClients(PairedClient out[], uint8_t max) {
  uint8_t n = 0;
  Preferences p;
  if (!p.begin(kNsPair, true)) return 0;
  for (uint8_t i = 0; i < max; ++i) {
    char key[4] = {'c', static_cast<char>('0' + i), 0, 0};
    out[i] = PairedClient();
    if (p.getBytesLength(key) == sizeof(PairedClient)) {
      p.getBytes(key, &out[i], sizeof(PairedClient));
      if (out[i].used) ++n;
    }
  }
  p.end();
  return n;
}

bool ConfigStore::saveClient(uint8_t slot, const PairedClient& c) {
  char key[4] = {'c', static_cast<char>('0' + slot), 0, 0};
  Preferences p;
  if (!p.begin(kNsPair, false)) return false;
  const bool ok = p.putBytes(key, &c, sizeof(c)) == sizeof(c);
  p.end();
  return ok;
}

void ConfigStore::clearClient(uint8_t slot) {
  char key[4] = {'c', static_cast<char>('0' + slot), 0, 0};
  Preferences p;
  if (p.begin(kNsPair, false)) {
    p.remove(key);
    p.end();
  }
}

void ConfigStore::loadState(PersistentState& out) {
  out = PersistentState();
  Preferences p;
  if (!p.begin(kNsState, true)) return;
  out.latchedFault = static_cast<FaultCode>(p.getUChar("latch", 0));
  if (!isLatchingFault(out.latchedFault)) out.latchedFault = FaultCode::NONE;
  out.mode = p.getUChar("mode", 0) == static_cast<uint8_t>(ControlMode::MANUAL) ? ControlMode::MANUAL
                                                                                  : ControlMode::AUTO;
  out.totalRuntimeS = p.getULong("rtS", 0);
  out.totalStarts = p.getULong("starts", 0);
  out.bootCount = p.getULong("boots", 0);
  p.end();
}

void ConfigStore::saveLatch(FaultCode f) {
  Preferences p;
  if (p.begin(kNsState, false)) {
    p.putUChar("latch", static_cast<uint8_t>(f));
    p.end();
  }
}

void ConfigStore::saveMode(ControlMode m) {
  Preferences p;
  if (p.begin(kNsState, false)) {
    p.putUChar("mode", static_cast<uint8_t>(m));
    p.end();
  }
}

void ConfigStore::saveCounters(uint32_t runtimeS, uint32_t starts) {
  Preferences p;
  if (p.begin(kNsState, false)) {
    p.putULong("rtS", runtimeS);
    p.putULong("starts", starts);
    p.end();
  }
}

uint32_t ConfigStore::incrementBootCount() {
  Preferences p;
  uint32_t n = 0;
  if (p.begin(kNsState, false)) {
    n = p.getULong("boots", 0) + 1;
    p.putULong("boots", n);
    p.end();
  }
  return n;
}

void ConfigStore::loadOta(OtaBookkeeping& out) {
  out = OtaBookkeeping();
  Preferences p;
  if (!p.begin(kNsOta, true)) return;
  out.pendingVerify = p.getBool("pending", false);
  out.attempts = p.getUChar("tries", 0);
  copyField(p, "prev", out.previousLabel, sizeof(out.previousLabel));
  copyField(p, "ver", out.targetVersion, sizeof(out.targetVersion));
  p.end();
}

void ConfigStore::saveOta(const OtaBookkeeping& o) {
  Preferences p;
  if (!p.begin(kNsOta, false)) return;
  p.putBool("pending", o.pendingVerify);
  p.putUChar("tries", o.attempts);
  p.putString("prev", o.previousLabel);
  p.putString("ver", o.targetVersion);
  p.end();
}

void ConfigStore::clearOta() {
  Preferences p;
  if (p.begin(kNsOta, false)) {
    p.clear();
    p.end();
  }
}

void ConfigStore::factoryReset() {
  for (const char* ns : {kNsCfg, kNsWifi, kNsPair, kNsState, kNsOta}) {
    Preferences p;
    if (p.begin(ns, false)) {
      p.clear();
      p.end();
    }
  }
}

}  // namespace wintek
