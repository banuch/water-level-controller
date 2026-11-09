#include "services/OtaManager.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <string.h>

#include "app/SharedState.h"
#include "app_config.h"
#include "diagnostics/Logger.h"
#include "ota_pubkey.h"
#include "storage/ConfigStore.h"

// Arduino-ESP32 hook: keep a freshly installed image in PENDING_VERIFY so
// that OtaManager::confirmIfHealthy() decides between valid and rollback.
extern "C" bool verifyRollbackLater() { return true; }

namespace wintek {

namespace {
constexpr const char* TAG = "ota";
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
OtaStatus g_status;
OtaManifest g_manifest;
volatile bool g_busy = false;
bool g_pendingVerify = false;

void setStatus(OtaPhase p, uint8_t progress, const char* err = nullptr) {
  portENTER_CRITICAL(&g_mux);
  g_status.phase = p;
  g_status.progressPercent = progress;
  if (err != nullptr) {
    strncpy(g_status.error, err, sizeof(g_status.error) - 1);
    g_status.error[sizeof(g_status.error) - 1] = '\0';
  } else if (p != OtaPhase::FAILED) {
    g_status.error[0] = '\0';
  }
  ++g_status.seq;
  portEXIT_CRITICAL(&g_mux);
}

bool hexToBytes(const char* hex, uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    unsigned v = 0;
    if (sscanf(hex + 2 * i, "%2x", &v) != 1) return false;
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

bool verifySignature(const uint8_t digest[32], const char* sigB64) {
  const char* pem = WINTEK_OTA_PUBKEY_PEM;
  if (pem[0] == '\0') return false;
  uint8_t sig[128];
  size_t sigLen = 0;
  if (mbedtls_base64_decode(sig, sizeof(sig), &sigLen, reinterpret_cast<const unsigned char*>(sigB64),
                            strlen(sigB64)) != 0) {
    return false;
  }
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  bool ok = mbedtls_pk_parse_public_key(&pk, reinterpret_cast<const unsigned char*>(pem), strlen(pem) + 1) == 0 &&
            mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECDSA) &&
            mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, 32, sig, sigLen) == 0;
  mbedtls_pk_free(&pk);
  return ok;
}

// Steps 1-3 of the OTA sequence: stop motor, confirm feedback OFF, enter maintenance.
bool enterMaintenance() {
  Command c;
  c.action = CommandAction::ENTER_OTA;
  c.source = CommandSource::SYSTEM;
  for (int attempt = 0; attempt < 6; ++attempt) {
    const CommandOutcome o = SharedState::submit(c, 2000);
    if (o.status == CommandStatus::ACCEPTED) return true;
    if (o.reason != RejectReason::MOTOR_NOT_STOPPED && o.reason != RejectReason::BOOTING) {
      LOGW(TAG, "maintenance refused: %s", toString(o.reason));
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(o.retryAfterMs > 0 ? o.retryAfterMs : 1000));
  }
  return false;
}

void exitMaintenance() {
  Command c;
  c.action = CommandAction::EXIT_OTA;
  c.source = CommandSource::SYSTEM;
  SharedState::submit(c, 2000);
}

void fail(const char* why) {
  LOGE(TAG, "update failed: %s", why);
  setStatus(OtaPhase::FAILED, 0, why);
}

void otaTask(void*) {
  esp_task_wdt_add(nullptr);
  const OtaManifest m = g_manifest;
  bool inMaintenance = false;
  WiFiClientSecure* tls = nullptr;
  HTTPClient http;
  char* ca = nullptr;

  do {
    setStatus(OtaPhase::STOPPING_MOTOR, 0);
    if (!enterMaintenance()) {
      fail("MOTOR_NOT_STOPPED");
      break;
    }
    inMaintenance = true;

    setStatus(OtaPhase::DOWNLOADING, 0);
    ca = static_cast<char*>(malloc(4096));
    if (ca == nullptr || (ConfigStore::loadPem("otaCa", ca, 4096) == 0 && ConfigStore::loadPem("mqCa", ca, 4096) == 0)) {
      fail("OTA_CA_NOT_PROVISIONED");
      break;
    }
    tls = new WiFiClientSecure();
    tls->setCACert(ca);
    tls->setTimeout(15);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(*tls, m.url)) {
      fail("HTTP_BEGIN");
      break;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
      fail("HTTP_STATUS");
      LOGE(TAG, "HTTP %d", code);
      break;
    }
    const int len = http.getSize();
    if (len <= 0 || static_cast<uint32_t>(len) != m.size) {
      fail("SIZE_MISMATCH");
      break;
    }
    if (!Update.begin(m.size, U_FLASH)) {
      fail("PARTITION");
      break;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    uint32_t written = 0;
    uint32_t lastData = millis();
    bool aborted = false;
    while (written < m.size) {
      esp_task_wdt_reset();
      // Abort if the control task left maintenance (e.g. a safety fault).
      if (SharedState::snapshot().state != MotorState::OTA_MAINTENANCE) {
        aborted = true;
        fail("MAINTENANCE_LOST");
        break;
      }
      const size_t avail = stream->available();
      if (avail == 0) {
        if (!http.connected() || millis() - lastData > 20000) {
          aborted = true;
          fail("DOWNLOAD_STALLED");
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      const int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (n <= 0) continue;
      lastData = millis();
      mbedtls_sha256_update_ret(&sha, buf, static_cast<size_t>(n));
      if (Update.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
        aborted = true;
        fail("FLASH_WRITE");
        break;
      }
      written += static_cast<uint32_t>(n);
      const uint8_t pct = static_cast<uint8_t>((static_cast<uint64_t>(written) * 100U) / m.size);
      if (pct != g_status.progressPercent) setStatus(OtaPhase::DOWNLOADING, pct);
    }
    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (aborted) {
      Update.abort();
      break;
    }

    setStatus(OtaPhase::VERIFYING, 100);
    uint8_t expected[32];
    if (!hexToBytes(m.sha256Hex, expected, 32) || memcmp(expected, digest, 32) != 0) {
      Update.abort();
      fail("HASH_MISMATCH");
      break;
    }
    if (!verifySignature(digest, m.signatureB64)) {
      Update.abort();
      fail(WINTEK_OTA_PUBKEY_PEM[0] == '\0' ? "OTA_KEY_NOT_PROVISIONED" : "BAD_SIGNATURE");
      break;
    }
    // Update.end() validates the image header/checksum and sets the boot partition.
    if (!Update.end(true)) {
      fail("IMAGE_INVALID");
      break;
    }

    OtaBookkeeping b;
    b.pendingVerify = true;
    b.attempts = 0;
    const esp_partition_t* running = esp_ota_get_running_partition();
    strncpy(b.previousLabel, running ? running->label : "", sizeof(b.previousLabel) - 1);
    strncpy(b.targetVersion, m.version, sizeof(b.targetVersion) - 1);
    ConfigStore::saveOta(b);

    setStatus(OtaPhase::REBOOTING, 100);
    LOGI(TAG, "update %s installed, rebooting", m.version);
    http.end();
    delete tls;
    free(ca);
    vTaskDelay(pdMS_TO_TICKS(1500));  // let the status reach MQTT / the app
    ESP.restart();
  } while (false);

  http.end();
  delete tls;
  free(ca);
  if (inMaintenance) exitMaintenance();  // back through self test before AUTO resumes
  g_busy = false;
  esp_task_wdt_delete(nullptr);
  vTaskDelete(nullptr);
}
}  // namespace

const char* toString(OtaPhase p) {
  switch (p) {
    case OtaPhase::IDLE: return "IDLE";
    case OtaPhase::VALIDATING: return "VALIDATING";
    case OtaPhase::STOPPING_MOTOR: return "STOPPING_MOTOR";
    case OtaPhase::DOWNLOADING: return "DOWNLOADING";
    case OtaPhase::VERIFYING: return "VERIFYING";
    case OtaPhase::REBOOTING: return "REBOOTING";
    case OtaPhase::FAILED: return "FAILED";
    case OtaPhase::PENDING_SELF_TEST: return "PENDING_SELF_TEST";
    case OtaPhase::CONFIRMED: return "CONFIRMED";
    case OtaPhase::ROLLED_BACK: return "ROLLED_BACK";
  }
  return "UNKNOWN";
}

void OtaManager::bootCheck() {
  OtaBookkeeping b;
  ConfigStore::loadOta(b);
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_get_state_partition(running, &st);
  if (st == ESP_OTA_IMG_PENDING_VERIFY) {
    g_pendingVerify = true;
    strncpy(g_status.targetVersion, b.targetVersion, sizeof(g_status.targetVersion) - 1);
    setStatus(OtaPhase::PENDING_SELF_TEST, 100);
    LOGI(TAG, "new image %s pending self test", b.targetVersion);
  } else if (b.pendingVerify && running != nullptr && strcmp(running->label, b.previousLabel) == 0) {
    // We expected the new image but are running the old one: bootloader rolled back.
    strncpy(g_status.targetVersion, b.targetVersion, sizeof(g_status.targetVersion) - 1);
    setStatus(OtaPhase::ROLLED_BACK, 0, "NEW_IMAGE_FAILED_SELF_TEST");
    LOGE(TAG, "update to %s was rolled back", b.targetVersion);
    ConfigStore::clearOta();
  }
}

void OtaManager::confirmIfHealthy(bool healthy) {
  if (!g_pendingVerify || !healthy) return;
  g_pendingVerify = false;
  esp_ota_mark_app_valid_cancel_rollback();
  ConfigStore::clearOta();
  setStatus(OtaPhase::CONFIRMED, 100);
  LOGI(TAG, "image confirmed valid");
}

bool OtaManager::start(const OtaManifest& m) {
  if (g_busy) return false;
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (next == nullptr) {
    setStatus(OtaPhase::FAILED, 0, "NO_OTA_PARTITION");
    return false;
  }
  setStatus(OtaPhase::VALIDATING, 0);
  const OtaManifestError e = validateManifest(m, app::kFirmwareVersion, app::kHardwareRevision, next->size, false);
  if (e != OtaManifestError::OK) {
    setStatus(OtaPhase::FAILED, 0, toString(e));
    return false;
  }
  if (WINTEK_OTA_PUBKEY_PEM[0] == '\0') {
    setStatus(OtaPhase::FAILED, 0, "OTA_KEY_NOT_PROVISIONED");
    return false;
  }
  g_manifest = m;
  portENTER_CRITICAL(&g_mux);
  strncpy(g_status.targetVersion, m.version, sizeof(g_status.targetVersion) - 1);
  portEXIT_CRITICAL(&g_mux);
  g_busy = true;
  if (xTaskCreate(otaTask, "ota", app::kOtaTaskStack, nullptr, app::kOtaTaskPriority, nullptr) != pdPASS) {
    g_busy = false;
    setStatus(OtaPhase::FAILED, 0, "TASK_CREATE");
    return false;
  }
  return true;
}

OtaStatus OtaManager::status() {
  portENTER_CRITICAL(&g_mux);
  OtaStatus s = g_status;
  portEXIT_CRITICAL(&g_mux);
  return s;
}

bool OtaManager::busy() { return g_busy; }

}  // namespace wintek
