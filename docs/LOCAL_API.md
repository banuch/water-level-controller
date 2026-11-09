# Wintek Local API v1 (LAN)

* **Discovery:** mDNS service `_wintek._tcp` with TXT records `id`, `fw`, `api=1` and `ws=81`. The
  hostname is `wintek-<id>.local`.
* **Transports:** REST on HTTP port 80 for commands and configuration, and a WebSocket on port 81 for
  live state and alerts.
* **Provisioning AP:** SSID `Wintek-XXXX`, WPA2, with the passphrase set to the setup code printed on
  the device label. The device is at `192.168.4.1`.

**Why REST plus WebSocket?** REST gives one signed request with one response per command, which maps
cleanly to the ack and idempotency model. The WebSocket pushes state within 250 ms of a change
without polling. Commands are **not** accepted over the WebSocket, so every command is individually
signed.

## 1. Pairing

The device only allows pairing while it is in the provisioning AP, or for 2 minutes after the
**service button** is pressed for 1–3 s. Both require physical access.

```
GET  /api/v1/nonce            -> {"nonce":"<32 hex>","ttlMs":30000}
POST /api/v1/pair
     {"nonce":"…","clientNonce":"<≥16 hex>","clientName":"Pixel 8",
      "proof": hex(HMAC_SHA256(setupCode, "PAIR\n"+nonce+"\n"+clientNonce))}
  <- {"tokenId":"a1b2c3d4","wrappedToken":"<64 hex>","deviceId":"WT-TANK-0001"}

token = wrappedToken XOR HMAC_SHA256(setupCode, "WRAP\n"+nonce+"\n"+clientNonce)
```

* The app stores `tokenId` and `token` in Android Keystore-backed secure storage.
* Up to 4 clients can be paired. After 5 wrong proofs, pairing is locked for 5 minutes.
* **Limitation.** A passive sniffer who records a pairing exchange can brute-force a weak setup code
  offline. Production setup codes must have at least 60 bits of entropy (12 or more random Base32
  characters).

## 2. Request signing

Every endpoint except `info`, `nonce` and `pair` requires these headers:

```
X-Wintek-Token-Id:  a1b2c3d4
X-Wintek-Nonce:     <fresh nonce from GET /api/v1/nonce, single use, 30 s>
X-Wintek-Signature: hex(HMAC_SHA256(token,
                       METHOD + "\n" + PATH + "\n" + nonce + "\n" + hex(SHA256(body))))
```

`PATH` excludes the query string. For a GET the body is empty, so its hash is the SHA-256 of an
empty string. Failures return `401` with one of these codes: `AUTH_MISSING`, `AUTH_BAD_NONCE`,
`AUTH_UNKNOWN_CLIENT` or `AUTH_BAD_SIGNATURE`. After 20 bad signatures the API returns `429`
(`AUTH_LOCKED_OUT`) for 60 s.

## 3. Endpoints

| Method | Path | Auth | Description |
|---|---|---|---|
| GET | `/api/v1/info` | — | `deviceId`, `model`, `firmwareVersion`, `hardwareRevision`, `wifiConnected`, `provisioning`, `cloudConnected`, `paired`, `pairingOpen` |
| GET | `/api/v1/nonce` | — | Single-use nonce |
| POST | `/api/v1/pair` | setup-code proof | See §1 |
| POST | `/api/v1/unpair` | ✓ | Removes the calling client |
| GET | `/api/v1/state` | ✓ | Same document as MQTT `state`, plus `ota` |
| POST | `/api/v1/command` | ✓ | Same body as an MQTT command. `timestamp`/`expiresAt` are optional on the LAN (the nonce provides freshness); if present they are checked. Response is the ack document |
| GET | `/api/v1/config` | ✓ | Full configuration |
| PUT | `/api/v1/config` | ✓ | Partial update. `422` returns `{error, field}` |
| GET | `/api/v1/diagnostics` | ✓ | Probe vA/vB/Rw per channel, heap, reset reason, RSSI |
| GET | `/api/v1/logs` | ✓ | Recent log lines (text/plain) |
| POST | `/api/v1/wifi` | ✓ | `{"ssid","password"}`. The device reconnects after the response |
| POST | `/api/v1/factory-reset` | ✓ | `{"confirm":"FACTORY-RESET"}`. Returns `409` if the motor is running |

## 4. WebSocket (`ws://<host>:81/`)

```
<- {"type":"challenge","nonce":"<32 hex>"}
-> {"type":"auth","tokenId":"a1b2c3d4","signature":hex(HMAC_SHA256(token,"WS\n"+nonce))}
<- {"type":"auth","ok":true}
<- {"type":"state","data":{…state…}}     on change (≤ 4/s) and every 5 s
<- {"type":"alert","data":{…alert…}}
```

The device closes any socket that has not authenticated within 5 s. At most 4 clients can connect.

## 5. Choosing between the LAN and cloud paths in the app

1. When the app is in the foreground, resolve `_wintek._tcp` and match the TXT `id`. Probe
   `/api/v1/info` with a 1.5 s timeout.
2. If that works, open the WebSocket and use the LAN as the primary path for state and commands.
   Keep the cloud subscription (through the backend) active so alerts still arrive.
3. Each user action gets one `commandId` (a UUID). Send it on the primary path. On a timeout or
   network error, resend **the same** command (same `commandId`, fresh nonce and expiry) on the other
   path. The device returns `DUPLICATE` with the original result if the first attempt did arrive.
4. Never infer the motor state from the HTTP status. Use the `observed` block in the ack and the
   following state messages.
