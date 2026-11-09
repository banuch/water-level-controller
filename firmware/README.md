# Wintek Smart Water Tank Controller: ESP32-C3 firmware

| Item | Value |
|---|---|
| Board | `esp32-c3-devkitm-1` |
| Platform | `espressif32 @ 6.9.0` (Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7) |
| Libraries | ArduinoJson 7.2.1, WebSockets 2.6.1 |
| MQTT | The esp-mqtt client bundled with the core |

Design and safety rationale: [../docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md).

## Build, flash, test

```bash
pio run -e esp32c3_devkit -t upload      # bench dev board (USB-C, native USB)
pio device monitor                       # USB CDC console, 115200
pio run -e esp32c3_pcb_reva              # product PCB rev A
tools/run_host_tests.sh                  # 78 unit tests on the host (g++, ASan/UBSan)
pio test -e native                       # same tests via PlatformIO
```

Verified on the reference toolchain (GCC 8.4 riscv32):

| Build | Flash | RAM | Warnings in project sources |
|---|---|---|---|
| `esp32c3_devkit` | 1,083,408 B, 55 % of a 1.875 MB OTA slot | 48 KB static | None |
| `esp32c3_pcb_reva` | 1,080,060 B | 48 KB static | None |

## Bench bring-up on a DevKitM-1

1. Flash `esp32c3_devkit`. This build disables the feedback, overload and panel inputs by default.
2. Simulate probes without a tank by building the front-end in ARCHITECTURE §4.4 on a breadboard.
   Use 10 kΩ resistors from COM to a probe node to simulate wet water and leave the node open for
   dry. Without the front-end the ADC pins float and the device correctly reports SENSOR_FAULT.
3. Provisioning: the device starts AP `Wintek-XXXX`. The passphrase is the setup code, logged at
   boot as `WT<MAC>` on dev units. Pair as described in [LOCAL_API.md](../docs/LOCAL_API.md), then
   `POST /api/v1/wifi`. Alternatively use the console: `wifi <ssid> <password>`.
4. Cloud (optional), from the console:
   ```
   factory set mqUri mqtts://broker.example.com:8883
   factory set mqPass <device-password>
   factory pem mqCa          (paste the CA PEM)
   reboot
   ```
5. Console commands: `status` (probe vA/vB/Rw), `config`, `logs`, `help`.

## Production checklist (firmware)

- [ ] Generate the OTA key offline (`tools/ota_keygen.sh`). Build with `-DWINTEK_OTA_PUBKEY_FILE=\"ota_pubkey.pem.h\"`. Without a key, every OTA is refused.
- [ ] Provision per-device `devId`, a random `setup` code (12+ characters), and MQTT credentials or an mTLS certificate. Then run `factory lock`.
- [ ] Enable flash encryption, NVS encryption and Secure Boot v2. This requires an ESP-IDF build; the `nvs_keys` partition is already reserved.
- [ ] Set `maxRuntimeMs` and `noRiseTimeoutMs` from the measured fill time at installation.
