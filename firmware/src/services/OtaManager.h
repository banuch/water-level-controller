// Secure OTA: HTTPS download (server CA verified) into the inactive OTA
// partition, SHA-256 integrity check, ECDSA-P256 signature verification
// against the compiled-in release key, then boot switch. The new image
// stays in PENDING_VERIFY until its self test passes; otherwise the
// bootloader rolls back (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y in the
// Arduino-ESP32 2.0.17 ESP32-C3 SDK build).
#pragma once

#include <stdint.h>

#include "services/OtaManifest.h"

namespace wintek {

enum class OtaPhase : uint8_t {
  IDLE = 0,
  VALIDATING,
  STOPPING_MOTOR,
  DOWNLOADING,
  VERIFYING,
  REBOOTING,
  FAILED,
  PENDING_SELF_TEST,  // running a freshly installed image
  CONFIRMED,
  ROLLED_BACK,
};

const char* toString(OtaPhase p);

struct OtaStatus {
  OtaPhase phase = OtaPhase::IDLE;
  uint8_t progressPercent = 0;
  char targetVersion[24] = {};
  char error[40] = {};
  uint32_t seq = 0;  // increments on every change
};

class OtaManager {
 public:
  // Called early in setup(): detects first boot of a new image / rollback.
  static void bootCheck();
  // Called periodically: confirms the running image once the system is healthy.
  static void confirmIfHealthy(bool healthy);

  // Starts the update task. Returns false (with status.error) if rejected.
  static bool start(const OtaManifest& m);
  static OtaStatus status();
  static bool busy();
};

}  // namespace wintek
