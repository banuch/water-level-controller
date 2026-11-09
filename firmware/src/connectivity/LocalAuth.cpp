#include "connectivity/LocalAuth.h"

#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <string.h>

#include "core/Types.h"
#include "diagnostics/Logger.h"

namespace wintek {

namespace {
constexpr const char* TAG = "auth";
constexpr uint8_t kMaxPairFailures = 5;
constexpr uint32_t kPairLockoutMs = 5UL * 60UL * 1000UL;
constexpr uint8_t kMaxAuthFailures = 20;
constexpr uint32_t kAuthLockoutMs = 60UL * 1000UL;
}  // namespace

void LocalAuth::hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t out[32]) {
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, keyLen, msg, msgLen, out);
}

void LocalAuth::sha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
  mbedtls_sha256_ret(msg, len, out, 0);
}

void LocalAuth::toHex(const uint8_t* in, size_t n, char* out) {
  static const char* kHex = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    out[2 * i] = kHex[in[i] >> 4];
    out[2 * i + 1] = kHex[in[i] & 0x0F];
  }
  out[2 * n] = '\0';
}

bool LocalAuth::fromHex(const char* in, uint8_t* out, size_t n) {
  if (in == nullptr || strlen(in) != 2 * n) return false;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < n; ++i) {
    const int h = nib(in[2 * i]), l = nib(in[2 * i + 1]);
    if (h < 0 || l < 0) return false;
    out[i] = static_cast<uint8_t>((h << 4) | l);
  }
  return true;
}

bool LocalAuth::constantTimeEq(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; ++i) d |= static_cast<uint8_t>(a[i] ^ b[i]);
  return d == 0;
}

void LocalAuth::begin(const char* setupCode) {
  strncpy(setupCode_, setupCode, sizeof(setupCode_) - 1);
  ConfigStore::loadClients(clients_, app::kMaxPairedClients);
}

bool LocalAuth::issueNonce(char out[33], uint32_t now) {
  NonceSlot* slot = nullptr;
  for (auto& n : nonces_) {
    if (!n.used || hasElapsed(now, n.issuedMs, app::kNonceTtlMs)) {
      slot = &n;
      break;
    }
  }
  if (slot == nullptr) {
    // All in use: recycle the oldest so a client cannot exhaust the table.
    slot = &nonces_[0];
    for (auto& n : nonces_) {
      if (static_cast<int32_t>(n.issuedMs - slot->issuedMs) < 0) slot = &n;
    }
  }
  uint8_t raw[16];
  esp_fill_random(raw, sizeof(raw));
  toHex(raw, sizeof(raw), slot->value);
  slot->used = true;
  slot->issuedMs = now;
  memcpy(out, slot->value, 33);
  return true;
}

bool LocalAuth::consumeNonce(const char* nonce, uint32_t now) {
  if (nonce == nullptr || strlen(nonce) != 32) return false;
  for (auto& n : nonces_) {
    if (n.used && strcmp(n.value, nonce) == 0) {
      n.used = false;  // single use
      return !hasElapsed(now, n.issuedMs, app::kNonceTtlMs);
    }
  }
  return false;
}

int LocalAuth::findClient(const char* tokenId) const {
  if (tokenId == nullptr) return -1;
  for (uint8_t i = 0; i < app::kMaxPairedClients; ++i) {
    if (clients_[i].used && strncmp(clients_[i].tokenId, tokenId, 8) == 0 && strlen(tokenId) == 8) return i;
  }
  return -1;
}

uint8_t LocalAuth::pairedCount() const {
  uint8_t n = 0;
  for (const auto& c : clients_) n += c.used ? 1 : 0;
  return n;
}

int LocalAuth::pair(const char* nonce, const char* clientNonce, const char* clientName, const char* proofHex,
                    uint32_t now, char tokenIdOut[9], char wrappedHexOut[65]) {
  if (lockoutUntilMs_ != 0 && static_cast<int32_t>(now - lockoutUntilMs_) < 0) return -1;
  if (!consumeNonce(nonce, now)) return -1;
  if (clientNonce == nullptr || strlen(clientNonce) < 16 || strlen(clientNonce) > 64) return -1;

  char msg[160];
  snprintf(msg, sizeof(msg), "PAIR\n%s\n%s", nonce, clientNonce);
  uint8_t expect[32], proof[32];
  hmacSha256(reinterpret_cast<const uint8_t*>(setupCode_), strlen(setupCode_), reinterpret_cast<const uint8_t*>(msg),
             strlen(msg), expect);
  if (!fromHex(proofHex, proof, 32) || !constantTimeEq(expect, proof, 32)) {
    if (++pairFailures_ >= kMaxPairFailures) {
      lockoutUntilMs_ = now + kPairLockoutMs;
      pairFailures_ = 0;
      LOGW(TAG, "pairing locked for 5 min after repeated failures");
    }
    return -1;
  }
  pairFailures_ = 0;

  // Choose a free slot, else replace slot 0 (oldest policy is app-managed).
  int slot = -1;
  for (uint8_t i = 0; i < app::kMaxPairedClients; ++i) {
    if (!clients_[i].used) {
      slot = i;
      break;
    }
  }
  if (slot < 0) slot = 0;

  PairedClient c;
  c.used = true;
  uint8_t idRaw[4];
  esp_fill_random(idRaw, sizeof(idRaw));
  toHex(idRaw, sizeof(idRaw), c.tokenId);
  esp_fill_random(c.token, sizeof(c.token));
  strncpy(c.name, clientName ? clientName : "app", sizeof(c.name) - 1);

  snprintf(msg, sizeof(msg), "WRAP\n%s\n%s", nonce, clientNonce);
  uint8_t wrapKey[32], wrapped[32];
  hmacSha256(reinterpret_cast<const uint8_t*>(setupCode_), strlen(setupCode_), reinterpret_cast<const uint8_t*>(msg),
             strlen(msg), wrapKey);
  for (int i = 0; i < 32; ++i) wrapped[i] = c.token[i] ^ wrapKey[i];

  if (!ConfigStore::saveClient(static_cast<uint8_t>(slot), c)) return -1;
  clients_[slot] = c;
  memcpy(tokenIdOut, c.tokenId, 9);
  toHex(wrapped, 32, wrappedHexOut);
  LOGI(TAG, "paired client '%s' id=%s slot=%d", c.name, c.tokenId, slot);
  return slot;
}

bool LocalAuth::unpair(const char* tokenId) {
  const int i = findClient(tokenId);
  if (i < 0) return false;
  clients_[i] = PairedClient();
  ConfigStore::clearClient(static_cast<uint8_t>(i));
  return true;
}

void LocalAuth::unpairAll() {
  for (uint8_t i = 0; i < app::kMaxPairedClients; ++i) {
    clients_[i] = PairedClient();
    ConfigStore::clearClient(i);
  }
}

AuthResult LocalAuth::verify(const char* method, const char* path, const char* tokenId, const char* nonce,
                             const char* sigHex, const uint8_t* body, size_t bodyLen, uint32_t now) {
  if (authLockoutUntilMs_ != 0 && static_cast<int32_t>(now - authLockoutUntilMs_) < 0) return AuthResult::LOCKED_OUT;
  if (tokenId == nullptr || nonce == nullptr || sigHex == nullptr || !*tokenId || !*nonce || !*sigHex) {
    return AuthResult::MISSING;
  }
  const int ci = findClient(tokenId);
  if (ci < 0) return AuthResult::UNKNOWN_TOKEN;
  if (!consumeNonce(nonce, now)) return AuthResult::BAD_NONCE;

  uint8_t bodyHash[32];
  sha256(body, bodyLen, bodyHash);
  char bodyHex[65];
  toHex(bodyHash, 32, bodyHex);
  char canon[320];
  const int n = snprintf(canon, sizeof(canon), "%s\n%s\n%s\n%s", method, path, nonce, bodyHex);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(canon)) return AuthResult::BAD_SIGNATURE;
  uint8_t expect[32], got[32];
  hmacSha256(clients_[ci].token, 32, reinterpret_cast<const uint8_t*>(canon), static_cast<size_t>(n), expect);
  if (!fromHex(sigHex, got, 32) || !constantTimeEq(expect, got, 32)) {
    if (++authFailures_ >= kMaxAuthFailures) {
      authLockoutUntilMs_ = now + kAuthLockoutMs;
      authFailures_ = 0;
    }
    return AuthResult::BAD_SIGNATURE;
  }
  authFailures_ = 0;
  return AuthResult::OK;
}

AuthResult LocalAuth::verifyWs(const char* tokenId, const char* nonce, const char* sigHex, uint32_t now) {
  const int ci = findClient(tokenId);
  if (ci < 0) return AuthResult::UNKNOWN_TOKEN;
  if (!consumeNonce(nonce, now)) return AuthResult::BAD_NONCE;
  char msg[48];
  snprintf(msg, sizeof(msg), "WS\n%s", nonce);
  uint8_t expect[32], got[32];
  hmacSha256(clients_[ci].token, 32, reinterpret_cast<const uint8_t*>(msg), strlen(msg), expect);
  if (!fromHex(sigHex, got, 32) || !constantTimeEq(expect, got, 32)) return AuthResult::BAD_SIGNATURE;
  return AuthResult::OK;
}

}  // namespace wintek
