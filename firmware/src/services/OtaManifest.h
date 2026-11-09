// OTA manifest model and validation (hardware independent).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace wintek {

struct OtaManifest {
  char version[24] = {};        // semantic version "MAJOR.MINOR.PATCH"
  char url[256] = {};           // must be https://
  char sha256Hex[65] = {};      // lowercase hex SHA-256 of the image
  char signatureB64[160] = {};  // base64 DER ECDSA-P256 signature over the SHA-256 digest
  uint32_t size = 0;
  char hwRev[16] = {};          // hardware revision the image is built for
  bool stopMotorIfRunning = true;
};

enum class OtaManifestError : uint8_t {
  OK = 0,
  BAD_VERSION,
  NOT_NEWER,
  BAD_URL,
  BAD_HASH,
  BAD_SIGNATURE_FIELD,
  BAD_SIZE,
  HW_MISMATCH,
};

const char* toString(OtaManifestError e);

struct SemVer {
  uint16_t major = 0, minor = 0, patch = 0;
};

bool parseSemVer(const char* s, SemVer& out);
int compareSemVer(const SemVer& a, const SemVer& b);

// maxImageSize: OTA partition size.
OtaManifestError validateManifest(const OtaManifest& m, const char* currentVersion, const char* hwRev,
                                  uint32_t maxImageSize, bool allowDowngrade);

}  // namespace wintek
