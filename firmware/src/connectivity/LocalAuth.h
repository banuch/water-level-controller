// Local-network pairing and request authentication.
//
// Pairing (requires physical access: service button pairing window, or the
// provisioning AP whose WPA2 passphrase is the label setup code):
//   1. GET  /api/v1/nonce              -> {nonce}
//   2. POST /api/v1/pair {nonce, clientNonce, clientName,
//        proof = HMAC-SHA256(setupCode, "PAIR\n"+nonce+"\n"+clientNonce)}
//      <- {tokenId, wrappedToken = token XOR HMAC-SHA256(setupCode, "WRAP\n"+nonce+"\n"+clientNonce)}
// The 32-byte token never crosses the network in clear.
//
// Every later request carries a fresh single-use device nonce:
//   X-Wintek-Token-Id, X-Wintek-Nonce,
//   X-Wintek-Signature = hex(HMAC-SHA256(token, METHOD+"\n"+PATH+"\n"+nonce+"\n"+hex(SHA256(body))))
// so captured requests cannot be replayed and the token is never sent.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "storage/ConfigStore.h"

namespace wintek {

enum class AuthResult : uint8_t { OK = 0, MISSING, BAD_NONCE, UNKNOWN_TOKEN, BAD_SIGNATURE, LOCKED_OUT };

class LocalAuth {
 public:
  void begin(const char* setupCode);

  // Issues a single-use nonce (32 hex chars). Returns false if the table is full.
  bool issueNonce(char out[33], uint32_t nowMs);

  // Pairing. Returns the client slot or -1.
  int pair(const char* nonce, const char* clientNonce, const char* clientName, const char* proofHex,
           uint32_t nowMs, char tokenIdOut[9], char wrappedHexOut[65]);
  bool unpair(const char* tokenId);
  void unpairAll();
  uint8_t pairedCount() const;

  AuthResult verify(const char* method, const char* path, const char* tokenId, const char* nonce,
                    const char* signatureHex, const uint8_t* body, size_t bodyLen, uint32_t nowMs);
  // WebSocket session auth: signature over "WS\n"+nonce.
  AuthResult verifyWs(const char* tokenId, const char* nonce, const char* signatureHex, uint32_t nowMs);

  static void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t out[32]);
  static void sha256(const uint8_t* msg, size_t len, uint8_t out[32]);
  static void toHex(const uint8_t* in, size_t n, char* out);
  static bool fromHex(const char* in, uint8_t* out, size_t n);
  static bool constantTimeEq(const uint8_t* a, const uint8_t* b, size_t n);

 private:
  bool consumeNonce(const char* nonce, uint32_t nowMs);
  int findClient(const char* tokenId) const;

  struct NonceSlot {
    bool used = false;
    char value[33] = {};
    uint32_t issuedMs = 0;
  };
  NonceSlot nonces_[8];
  PairedClient clients_[app::kMaxPairedClients];
  char setupCode_[24] = {};
  uint8_t pairFailures_ = 0;
  uint32_t lockoutUntilMs_ = 0;
  uint8_t authFailures_ = 0;
  uint32_t authLockoutUntilMs_ = 0;
};

}  // namespace wintek
