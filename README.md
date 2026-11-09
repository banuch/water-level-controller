# Wintek Smart Water Tank Controller

**Wintek Solutions.** An ESP32-C3 controller for a 230 V single-phase domestic pump that fills an
overhead tank, using conductive stainless-steel probes, an independent hardware overflow cutoff, and
local plus cloud control.

> **Status: prototype (Phases 1–3 of 8).** Not production-ready. The design has not yet been through
> electrical safety testing, EMC testing, reliability testing or hardware verification. Mains wiring
> must be reviewed by a qualified electrical engineer before it is connected to a live pump.

| Path | Content |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Requirements, assumptions, block diagram, probe front-end, GPIO map, safety layers, state machine, timing defaults, OTA, security, milestones |
| [docs/MQTT_PROTOCOL.md](docs/MQTT_PROTOCOL.md) | Topics, QoS/retain, LWT, ACL, payload schemas, command expiry, idempotency |
| [docs/LOCAL_API.md](docs/LOCAL_API.md) | LAN REST/WebSocket API, pairing, HMAC request signing, choosing the LAN or cloud path |
| [docs/TEST_PLAN.md](docs/TEST_PLAN.md) | Firmware, hardware and app tests with acceptance criteria |
| [firmware/](firmware/) | PlatformIO project (C++17, FreeRTOS tasks) and unit tests |

## What works today

The firmware builds for the ESP32-C3. Its hardware-independent core (sensing, state machine, safety,
schedules, command handling, configuration) is covered by **78 passing host unit tests**.

It includes:

* Wi-Fi provisioning
* MQTT over TLS
* A signed local LAN API with WebSocket live state
* Signed OTA updates with rollback
* NVS persistence
* Alerts
* A factory console

## Next phases

4. Flutter app
5. Node.js/PostgreSQL/Mosquitto backend with FCM
7. KiCad schematic, BOM and layout
8. HIL rig and pilot
