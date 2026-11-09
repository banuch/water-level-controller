// OTA signing public key (ECDSA P-256, PEM).
//
// Generate a release key pair OFFLINE with tools/ota_keygen.sh, keep the
// private key in a hardware token / offline vault, and build with:
//   -DWINTEK_OTA_PUBKEY_FILE=\"ota_pubkey.pem.h\"
// where that generated header defines WINTEK_OTA_PUBKEY_PEM.
//
// If no key is compiled in, the firmware REFUSES every OTA update
// (OTA_KEY_NOT_PROVISIONED); it never falls back to unsigned updates.
#pragma once

#if defined(WINTEK_OTA_PUBKEY_FILE)
#include WINTEK_OTA_PUBKEY_FILE
#endif

#ifndef WINTEK_OTA_PUBKEY_PEM
#define WINTEK_OTA_PUBKEY_PEM ""
#endif
