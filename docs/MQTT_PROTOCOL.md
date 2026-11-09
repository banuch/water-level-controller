# Wintek MQTT Protocol v1

MQTT 3.1.1 over TLS 1.2 (port 8883). Namespace: `wintek/v1/{deviceId}/…`.
The firmware implementation is in `firmware/src/app/NetworkTask.cpp`, `connectivity/MqttManager.cpp`
and `app/StateJson.cpp`.

## 1. Topics

| Topic | Direction | QoS | Retained | Content |
|---|---|---|---|---|
| `…/availability` | device → | 1 | **yes** | `online` on connect; `offline` from the LWT |
| `…/state` | device → | 1 | **yes** | Full state on every change (at most 1/s) and on reconnect |
| `…/telemetry` | device → | 0 | no | Every `telemetryIntervalS` (30 s), or every 10 s while the motor runs |
| `…/alerts` | device → | 1 | no | One message per alert. Buffered (32) while offline |
| `…/command` | → device | 1 | **never** | Motor and OTA commands |
| `…/command/ack` | device → | 1 | no | Result for each `commandId` |
| `…/config` | → device | 1 | **never** | Partial configuration update |
| `…/config/ack` | device → | 1 | no | Result plus the full effective configuration (also sent as `requestId:"sync"` on connect) |
| `…/ota/status` | device → | 1 | **yes** | OTA phase, progress, error |

The device **ignores any retained message** received on `command` or `config`. It also rejects any
command older than its expiry, so stale motor commands are never executed after a reconnect. The
session is a clean session, so the broker does not queue QoS 1 commands while the device is offline.

## 2. Authentication and ACL

* **Device identity.** Each device has its own credentials, never shared:
  username `deviceId` plus a random 32-byte password, **or** (preferred) an X.509 client certificate
  whose CN is `deviceId`, issued by the Wintek device CA. These are provisioned at end-of-line via the
  USB console (`factory set mqUri|mqUser|mqPass`, `factory pem mqCa|mqCert|mqKey`, then `factory lock`).
* **Server validation.** The broker CA is pinned (`mqCa`). The firmware refuses `mqtt://` URIs and
  refuses to connect without a CA. The hostname is verified (no `skip_cert_common_name_check`).
* **Mobile apps never connect to the broker with device or admin credentials.** They talk to the
  backend API, and the backend publishes commands using its own service identity.

Mosquitto ACL (pattern form):

```
# devices: only their own subtree, and only in the right direction
pattern write wintek/v1/%u/availability
pattern write wintek/v1/%u/state
pattern write wintek/v1/%u/telemetry
pattern write wintek/v1/%u/alerts
pattern write wintek/v1/%u/command/ack
pattern write wintek/v1/%u/config/ack
pattern write wintek/v1/%u/ota/status
pattern read  wintek/v1/%u/command
pattern read  wintek/v1/%u/config

# backend service account
user wintek-backend
topic readwrite wintek/v1/#
```

With mTLS, set `use_identity_as_username true` so `%u` is the certificate CN.

## 3. Payloads

Every device payload carries `schemaVersion`, `deviceId`, `timestamp` (ISO-8601 UTC, or `null` until
SNTP has synced) and `uptimeMs`.

### 3.1 Telemetry

```json
{
  "schemaVersion": 1,
  "deviceId": "WT-TANK-0001",
  "timestamp": "2026-11-09T08:30:00Z",
  "uptimeMs": 5234123,
  "levelPercent": 50,
  "levelRangeLow": 50,
  "levelRangeHigh": 75,
  "levelType": "discrete",
  "motorState": "OFF",
  "controlMode": "AUTO",
  "state": "AUTO_IDLE",
  "wifiRssi": -62,
  "sensorHealthy": true,
  "faultCode": null,
  "runMs": 0
}
```

* `levelPercent`, `levelRangeLow` and `levelRangeHigh` are `null` when the sensors are invalid.
* `motorState` is one of `ON`, `OFF`, `STARTING` or `STOPPING`. With feedback installed, ON means both
  commanded and confirmed by the auxiliary contact.

### 3.2 State (retained)

This is the telemetry payload plus the following fields: `seq`, `bootId`, `firmwareVersion`,
`softwareMode`, `localSwitch`, `motorCommanded`, `contactorClosed`, `overloadTripped`,
`overflowTripped`, `emergencyStop`, `latchedFault`, `lastStartReason`, `lastStopReason`,
`startBlockedMs`, `fillDemand`, `autoInhibitedByUser`, `scheduleWindowOpen`, `timeSynced`,
`cloudConnected`, `onThresholdPercent`, `offThresholdPercent`, `totalRuntimeS`, `totalStarts`,
`probes[] {percent,state}`, and `ota {phase,progress}`.

`seq` increases whenever a reportable field changes. Within one `bootId`, consumers drop any state
whose `seq` is lower than one they have already seen.

### 3.3 Command

```json
{
  "commandId": "cmd-7f3c2a10",
  "action": "MOTOR_ON",
  "mode": "MANUAL",
  "timestamp": "2026-11-09T08:30:00Z",
  "expiresAt": "2026-11-09T08:30:30Z"
}
```

| action | Fields | Notes |
|---|---|---|
| `MOTOR_ON` | `mode` optional (`AUTO` or `MANUAL`) | In AUTO this means "fill now" to the OFF probe. With `"mode":"MANUAL"` it switches to manual and starts |
| `MOTOR_OFF` | — | Always accepted |
| `SET_MODE` | `mode` required | Rejected unless the selector is in AUTO |
| `RESET_FAULT` | — | Only when the cause has cleared |
| `OTA_UPDATE` | `ota {version,url,sha256,signature,size,hwRev,stopMotorIfRunning}` | See ARCHITECTURE §10 |

Rules applied by the device (`CommandProcessor`):

* `commandId` has 4–48 characters from `[A-Za-z0-9-_.:]`. Use a UUID.
* Cloud commands **must** carry `timestamp` and `expiresAt`, with `expiresAt − timestamp ≤ 120 s`.
  A timestamp more than 30 s in the future is rejected as `INVALID`. A command already past expiry is
  rejected as `EXPIRED`.
* If the device clock is not synced, cloud commands are rejected as `EXPIRED`, because the device
  cannot prove they are fresh. Local commands still work.
* The last 24 `commandId`s are remembered. A repeat returns the original result with
  `"status":"DUPLICATE"` and is **not executed again**. This covers the MQTT QoS 1 redelivery and the
  app retrying across the LAN and cloud paths.

### 3.4 Command acknowledgement

```json
{
  "schemaVersion": 1,
  "deviceId": "WT-TANK-0001",
  "timestamp": "2026-11-09T08:30:01Z",
  "uptimeMs": 5235123,
  "commandId": "cmd-7f3c2a10",
  "status": "REJECTED",
  "result": "REJECTED",
  "reason": "MIN_OFF_TIME",
  "retryAfterMs": 85000,
  "observed": {
    "state": "MANUAL_IDLE",
    "controlMode": "MANUAL",
    "motorCommanded": false,
    "contactorClosed": false,
    "seq": 412
  }
}
```

* `status` is one of `ACCEPTED`, `REJECTED`, `DUPLICATE`, `EXPIRED`, `INVALID` or `TIMEOUT`.
  `result` is the original outcome, which differs from `status` only for duplicates.
* `reason` is one of: `TANK_FULL`, `OVERFLOW_ACTIVE`, `OVERLOAD_ACTIVE`, `EMERGENCY_STOP_ACTIVE`,
  `SENSOR_INVALID`, `FAULT_LOCKOUT_ACTIVE`, `CONDITION_NOT_CLEARED`, `MIN_OFF_TIME`,
  `START_RATE_LIMIT`, `RESTART_DELAY`, `LOCAL_SWITCH_OFF`, `LOCAL_SWITCH_MANUAL`, `OTA_IN_PROGRESS`,
  `NOT_IN_LOCKOUT`, `ALREADY_IN_STATE`, `BOOTING`, `MOTOR_NOT_STOPPED` or `UNSUPPORTED`.
* **The app must show the motor as running only from `observed` or `state`, never from the button
  press.** `ACCEPTED` with `motorCommanded:true` but `contactorClosed:false` shows as "Starting…" until
  a state message confirms it.

### 3.5 Configuration

Request (`…/config`):

```json
{
  "requestId": "cfg-91aa",
  "timestamp": "2026-11-09T08:30:00Z",
  "expiresAt": "2026-11-09T08:31:00Z",
  "config": { "thresholds": { "onPercent": 25, "offPercent": 95 } }
}
```

The `config` object is a partial document matching `GET /api/v1/config` on the LAN API. The ack on
`…/config/ack` contains `{requestId, ok, error?, config}`. Validation errors are named after
`ConfigError` (for example `THRESHOLD_ORDER`, `MIN_OFF` or `BAD_FIELD`), and an invalid patch leaves
the configuration unchanged.

### 3.6 Alerts

```json
{
  "schemaVersion": 1,
  "deviceId": "WT-TANK-0001",
  "bootId": "9c1e22d0",
  "alertSeq": 17,
  "type": "OVERFLOW_CUTOFF",
  "severity": "CRITICAL",
  "eventUptimeMs": 81234,
  "timestamp": "2026-11-09T08:31:12Z",
  "faultCode": "OVERFLOW_HH",
  "levelPercent": 95
}
```

| Field | Values |
|---|---|
| `type` | `TANK_FULL`, `MOTOR_STARTED`, `MOTOR_STOPPED`, `SENSOR_FAULT`, `MOTOR_OVERLOAD`, `EXCESSIVE_RUNTIME`, `RUNTIME_WARNING`, `POWER_RESTORED`, `OVERFLOW_CUTOFF`, `CONTACTOR_FAULT`, `NO_LEVEL_RISE`, `EMERGENCY_STOP`, `LOCAL_PANEL_FAULT`, `FAULT_CLEARED` |
| Optional fields | `startReason`, `stopReason`, `runtimeMs`, and `suppressedRepeats` (number of identical alerts swallowed in the 10-minute repeat window) |

`DEVICE_OFFLINE` is generated by the **backend** when `availability` becomes `offline`, after a
2-minute grace period to absorb brief reconnects. The `(bootId, alertSeq)` pair identifies each alert
for backend de-duplication.

### 3.7 OTA status (retained)

```json
{"schemaVersion":1,"deviceId":"WT-TANK-0001","timestamp":"…","currentVersion":"0.3.0",
 "phase":"DOWNLOADING","progress":42,"targetVersion":"0.4.0"}
```

`phase` is one of `IDLE`, `VALIDATING`, `STOPPING_MOTOR`, `DOWNLOADING`, `VERIFYING`, `REBOOTING`,
`FAILED` (with `error`), `PENDING_SELF_TEST`, `CONFIRMED` or `ROLLED_BACK`.

## 4. Connection behaviour

* **Reconnect.** Exponential backoff from 2 s doubling to 300 s, with ±20 % jitter. The same applies
  to Wi-Fi.
* **On every connect** the device publishes `availability=online` (retained), subscribes to `command`
  and `config`, publishes the full `state` (retained), a `config/ack` with `requestId:"sync"`, and
  `ota/status`. It then flushes any buffered alerts.
* **Clock.** SNTP (pool.ntp.org, time.google.com, time.cloudflare.com). MQTT waits up to 30 s after
  Wi-Fi comes up for a valid clock before connecting. Schedules use the POSIX timezone in the config
  (default `IST-5:30`).
* **Keepalive** is 60 s, so the broker publishes the LWT `offline` within about 90 s of silent loss.
* **The device is the safety authority.** No message can bypass `MotorStateMachine::handleCommand`.
