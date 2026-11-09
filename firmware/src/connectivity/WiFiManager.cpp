#include "connectivity/WiFiManager.h"

#include <WiFi.h>
#include <esp_random.h>
#include <string.h>

#include "app_config.h"
#include "diagnostics/Logger.h"

namespace wintek {

namespace {
constexpr const char* TAG = "wifi";
constexpr uint32_t kAttemptTimeoutMs = 20000;
}  // namespace

void WiFiManager::begin(const char* deviceId, const char* setupCode) {
  // Hostname "wintek-xxxxxxxx" (lowercase id suffix) for mDNS / DHCP.
  const char* suffix = strchr(deviceId, '-');
  snprintf(hostname_, sizeof(hostname_), "wintek%s", suffix ? suffix : "-device");
  for (char* p = hostname_; *p; ++p) {
    if (*p >= 'A' && *p <= 'Z') *p = static_cast<char>(*p - 'A' + 'a');
  }
  const size_t n = strlen(deviceId);
  snprintf(apSsid_, sizeof(apSsid_), "Wintek-%s", n > 4 ? deviceId + n - 4 : deviceId);
  strncpy(apPass_, setupCode, sizeof(apPass_) - 1);
  if (strlen(apPass_) < 8) strncat(apPass_, "00000000", 8 - strlen(apPass_));  // WPA2 minimum length

  WiFi.persistent(false);  // credentials live in our own NVS namespace
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname_);
  WiFi.setSleep(true);  // modem sleep; DTIM wakeups keep MQTT/HTTP responsive

  ConfigStore::loadWifi(creds_);
  backoffMs_ = app::kWifiBackoffMinMs;
  if (!creds_.valid()) {
    LOGW(TAG, "no Wi-Fi credentials: starting provisioning AP");
    startProvisioning(millis());
  } else {
    nextAttemptMs_ = millis();
  }
}

void WiFiManager::connectNow(uint32_t now) {
  if (!creds_.valid()) return;
  LOGI(TAG, "connecting to '%s'", creds_.ssid);
  WiFi.disconnect(false, false);
  WiFi.begin(creds_.ssid, creds_.pass);
  attemptActive_ = true;
  attemptStartMs_ = now;
}

void WiFiManager::loop(uint32_t now) {
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up && !wasConnected_) {
    LOGI(TAG, "connected, ip=%s rssi=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    backoffMs_ = app::kWifiBackoffMinMs;
    attemptActive_ = false;
  } else if (!up && wasConnected_) {
    LOGW(TAG, "connection lost");
    nextAttemptMs_ = now + backoffMs_;
  }
  wasConnected_ = up;

  if (provisioning_ && hasElapsed(now, provisioningSinceMs_, app::kProvisioningWindowMs) && creds_.valid()) {
    stopProvisioning();
  }

  if (up || !creds_.valid()) return;

  if (attemptActive_ && hasElapsed(now, attemptStartMs_, kAttemptTimeoutMs)) {
    attemptActive_ = false;
    // Exponential backoff with +-20 % jitter avoids synchronized reconnect
    // storms when a whole neighbourhood loses power and comes back.
    const uint32_t jitter = backoffMs_ / 5;
    const uint32_t delay = backoffMs_ - jitter + (esp_random() % (2 * jitter + 1));
    nextAttemptMs_ = now + delay;
    LOGW(TAG, "attempt failed, retry in %lus", static_cast<unsigned long>(delay / 1000));
    backoffMs_ = backoffMs_ * 2 > app::kWifiBackoffMaxMs ? app::kWifiBackoffMaxMs : backoffMs_ * 2;
  }
  if (!attemptActive_ && static_cast<int32_t>(now - nextAttemptMs_) >= 0) connectNow(now);
}

void WiFiManager::startProvisioning(uint32_t now) {
  provisioning_ = true;
  provisioningSinceMs_ = now;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid_, apPass_, 6, 0, 2);
  LOGI(TAG, "provisioning AP '%s' at %s", apSsid_, WiFi.softAPIP().toString().c_str());
}

void WiFiManager::stopProvisioning() {
  if (!provisioning_) return;
  provisioning_ = false;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  LOGI(TAG, "provisioning AP stopped");
}

bool WiFiManager::setCredentials(const char* ssid, const char* pass) {
  if (ssid == nullptr || pass == nullptr) return false;
  const size_t sl = strlen(ssid), pl = strlen(pass);
  if (sl == 0 || sl > 32 || pl > 64 || (pl > 0 && pl < 8)) return false;
  WifiCredentials c;
  memcpy(c.ssid, ssid, sl + 1);
  memcpy(c.pass, pass, pl + 1);
  if (!ConfigStore::saveWifi(c)) return false;
  creds_ = c;
  backoffMs_ = app::kWifiBackoffMinMs;
  attemptActive_ = false;
  nextAttemptMs_ = millis() + 1500;  // let the HTTP response reach the app first
  return true;
}

void WiFiManager::forgetCredentials() {
  ConfigStore::clearWifi();
  creds_ = WifiCredentials();
  WiFi.disconnect(false, true);
}

bool WiFiManager::connected() const { return WiFi.status() == WL_CONNECTED; }
int WiFiManager::rssi() const { return connected() ? WiFi.RSSI() : 0; }

}  // namespace wintek
