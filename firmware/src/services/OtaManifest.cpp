#include "services/OtaManifest.h"

#include <ctype.h>
#include <string.h>

namespace wintek {

const char* toString(OtaManifestError e) {
  switch (e) {
    case OtaManifestError::OK: return "OK";
    case OtaManifestError::BAD_VERSION: return "BAD_VERSION";
    case OtaManifestError::NOT_NEWER: return "NOT_NEWER";
    case OtaManifestError::BAD_URL: return "BAD_URL";
    case OtaManifestError::BAD_HASH: return "BAD_HASH";
    case OtaManifestError::BAD_SIGNATURE_FIELD: return "BAD_SIGNATURE_FIELD";
    case OtaManifestError::BAD_SIZE: return "BAD_SIZE";
    case OtaManifestError::HW_MISMATCH: return "HW_MISMATCH";
  }
  return "UNKNOWN";
}

namespace {
bool parseNum(const char*& p, uint16_t& out) {
  if (!isdigit(static_cast<unsigned char>(*p))) return false;
  uint32_t v = 0;
  while (isdigit(static_cast<unsigned char>(*p))) {
    v = v * 10 + static_cast<uint32_t>(*p - '0');
    if (v > 65535) return false;
    ++p;
  }
  out = static_cast<uint16_t>(v);
  return true;
}
}  // namespace

bool parseSemVer(const char* s, SemVer& out) {
  if (s == nullptr) return false;
  const char* p = s;
  if (*p == 'v') ++p;
  if (!parseNum(p, out.major) || *p++ != '.') return false;
  if (!parseNum(p, out.minor) || *p++ != '.') return false;
  if (!parseNum(p, out.patch)) return false;
  return *p == '\0' || *p == '-' || *p == '+';
}

int compareSemVer(const SemVer& a, const SemVer& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  return 0;
}

OtaManifestError validateManifest(const OtaManifest& m, const char* currentVersion, const char* hwRev,
                                  uint32_t maxImageSize, bool allowDowngrade) {
  SemVer next, cur;
  if (!parseSemVer(m.version, next)) return OtaManifestError::BAD_VERSION;
  if (!parseSemVer(currentVersion, cur)) return OtaManifestError::BAD_VERSION;
  if (!allowDowngrade && compareSemVer(next, cur) <= 0) return OtaManifestError::NOT_NEWER;

  if (strncmp(m.url, "https://", 8) != 0 || strlen(m.url) < 12) return OtaManifestError::BAD_URL;

  if (strlen(m.sha256Hex) != 64) return OtaManifestError::BAD_HASH;
  for (size_t i = 0; i < 64; ++i) {
    const char c = m.sha256Hex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return OtaManifestError::BAD_HASH;
  }

  const size_t sigLen = strlen(m.signatureB64);
  if (sigLen < 64 || sigLen >= sizeof(m.signatureB64)) return OtaManifestError::BAD_SIGNATURE_FIELD;

  if (m.size < 64 * 1024 || m.size > maxImageSize) return OtaManifestError::BAD_SIZE;

  if (hwRev != nullptr && strcmp(m.hwRev, hwRev) != 0) return OtaManifestError::HW_MISMATCH;
  return OtaManifestError::OK;
}

}  // namespace wintek
