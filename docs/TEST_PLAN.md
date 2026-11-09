# Wintek Test Plan (firmware focus: Phases 2–3)

Legend: **AUTO** means an automated host unit test exists and passes (`firmware/tools/run_host_tests.sh`).
**HIL** means a hardware-in-the-loop rig test (not yet run). **FIELD** means an installation test
(not yet run).

## 1. Firmware: required scenarios

| # | Scenario | Type | Test / procedure | Acceptance criteria |
|---|---|---|---|---|
| F1 | Level below 25 % | AUTO | `test_level_below_25_starts_auto` | Motor ON after the restart delay; reason AUTO_LOW_LEVEL |
| F2 | Level at 25 % (probe wet) | AUTO | `test_level_at_25_does_not_start` | Motor stays OFF in AUTO_IDLE |
| F3 | Between thresholds | AUTO | `test_between_thresholds_keeps_state_hysteresis` | Keeps filling while rising; no restart while falling until P25 is dry |
| F4 | Level at 95 % | AUTO | `test_level_at_95_stops_and_emits_tank_full` | OFF on the same tick; MOTOR_STOPPED and TANK_FULL events |
| F5 | Tank full, manual ON | AUTO | `test_manual_on_rejected_when_tank_full` | Rejected with TANK_FULL |
| F6 | Sensor disconnected | AUTO + HIL | `test_sensor_disconnected_at_boot`, `test_open_bias_resistor_detected_even_when_dry`; HIL: pull each probe and bias resistor | SENSOR_FAULT within 5 s (channel) or 3 s (sequence); motor OFF |
| F7 | Invalid probe sequence | AUTO | `test_invalid_probe_sequence` | `valid=false`, SENSOR_SEQUENCE |
| F8 | Wi-Fi failure | HIL | Power off the AP for 10 min while filling | Fill completes and stops at 95 %; reconnect attempts back off 2→300 s with jitter |
| F9 | MQTT failure | HIL | Stop the broker while filling | Same as F8; alerts buffered and delivered in order after reconnect; LWT `offline` within 90 s |
| F10 | Power loss | HIL | Cut supply during AUTO_FILLING and MANUAL_RUNNING | Contactor drops instantly; after restore: BOOT_SELF_TEST, 30 s delay, MANUAL not restarted, AUTO re-evaluated; POWER_RESTORED alert |
| F11 | Reboot with latched fault | AUTO + HIL | `test_restored_latch_after_reboot` | Lockout re-entered after boot; reset still required |
| F12 | Manual ON during overflow | AUTO | `test_manual_on_during_overflow_rejected` | Rejected OVERFLOW_ACTIVE; reset refused until HH clears |
| F13 | Repeated ON/OFF | AUTO | `test_repeated_on_off_commands_are_idempotent` | Idempotent acks; never two starts within min-OFF |
| F14 | Expired MQTT command | AUTO | `test_expiry_rules` | EXPIRED; not executed; unsynced clock rejects cloud commands |
| F15 | Duplicate command | AUTO + HIL | `test_dedup`; HIL: same commandId via LAN then cloud | Second ack DUPLICATE with the original result; one execution |
| F16 | Watchdog reset | HIL | Debug build with a `while(1)` injected in the network task | Reset within 15 s; contactor OFF during reset; reset reason TASK_WATCHDOG published |
| F17 | OTA interruption | HIL | Cut power at 30 %, 70 %, and during verify; corrupt hash; wrong signature; wrong hwRev | Old image boots every time; FAILED with the correct error; motor never ON during OTA_MAINTENANCE |
| F18 | OTA new image crashes | HIL | Image that panics at 20 s | Bootloader rollback; `ota/status` ROLLED_BACK |
| F19 | Overload trip | AUTO + HIL | `test_overload_trip_latches_fault` | OFF; FAULT_LOCKOUT; no auto-reset; reset only after the OL is reset |
| F20 | Contactor welded / not pulling in / dropout | AUTO + HIL | `test_contactor_*` | Detected within 2 s ±40 ms; CRITICAL alert |
| F21 | Dry run / no level rise | AUTO | `test_no_level_rise_detects_dry_run` | FAULT_LOCKOUT NO_LEVEL_RISE at the configured timeout |
| F22 | Max runtime | AUTO | `test_max_runtime_exceeded`, `test_runtime_warning_event_at_80_percent` | Warning at 80 %, trip at 100 % |
| F23 | Schedules | AUTO | `test_schedule_*`, `test_schedule_window_wait_and_resume`, `test_scheduled_fill_tops_up` | Windows (including across midnight), once-per-day fills, fail-open when time is unknown |
| F24 | Local selector OFF / unreadable panel | AUTO + HIL | `test_local_switch_off_*`, `test_unreadable_panel_fails_safe_to_off`; HIL: unplug the I²C | Motor OFF; remote ON rejected; LOCAL_PANEL_FAULT alert |
| F25 | millis() wrap | AUTO | `test_millis_wraparound` | Correct behaviour across the 2³² ms rollover |
| F26 | Config validation | AUTO | `test_thresholds/*`, `test_config_json/*` | Invalid values rejected with a named error; config unchanged |
| F27 | Splash rejection | AUTO + HIL | `test_debounce_rejects_splash`; HIL: spray P95 for 2 s | No state change for bursts under the debounce time |

## 2. Hardware tests (Phase 7 rig, before any live pump)

| Test | Method | Acceptance |
|---|---|---|
| Relay/contactor operation | 10,000 cycles at the rated coil load (with snubber) | No contact welding; drop-out < 50 ms |
| Contactor feedback | Simulate aux open/closed | Correct state within 60 ms (3-sample debounce) |
| Probe corrosion | 4 probes in tap water, 90 days, normal duty cycle, against a DC-excited control set | Rw drift < 30 %; visibly less deposit than the DC control |
| Cable interference | 30 m cable bundled 1 m alongside a 230 V motor cable during switching | No false wet/dry transitions; no channel faults |
| Supply variation | 180–265 V AC, 47–53 Hz | Normal operation |
| Brown-out | Ramp 230→0→230 V slowly, and 50 ms dips | Clean reset; no contactor chatter; restart delay honoured |
| ESD | IEC 61000-4-2: ±4 kV contact / ±8 kV air on probe terminals and enclosure | No damage; self-recovery (criterion B) |
| Surge/EFT | IEC 61000-4-5 (±1 kV L-N), IEC 61000-4-4 (±1 kV) | Criterion B; the contactor never energises spuriously |
| Hardware overflow trip | Wet the HH pair with the MCU held in reset, and with firmware forced ON | The coil opens within 1 s regardless of the MCU; latch holds until local reset |
| Overload trip | Trip the OL relay manually | The coil opens (hardware) and the firmware reports MOTOR_OVERLOAD |
| Output fail-safe | Hold the MCU in reset, erase the flash, inject a GPIO fault | Contactor never energises |
| Insulation | 3 kV AC hipot (reinforced) between mains and SELV for 60 s; creepage measured | No breakdown; creepage ≥ 6.4 mm reinforced (pollution degree 2, 250 V) |

## 3. Flutter tests (Phase 4)

The plan covers: widget tests for each screen state (loading, error, disconnected, empty), provider
tests with a mocked repository, an ack-gated motor control test (the UI never shows ON before the
observed state confirms it), offline display, reconnection, pairing (HMAC vectors shared with the
firmware), settings persistence and navigation.

## 4. Running

```bash
cd firmware
tools/run_host_tests.sh          # g++ + ASan/UBSan, no PlatformIO needed
pio test -e native               # same tests through PlatformIO
pio run -e esp32c3_devkit        # build firmware
```
