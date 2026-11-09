// Wintek Smart Water Tank Controller — board pin map.
//
// Target: ESP32-C3 (QFN32, 4 MB flash) — ESP32-C3-DevKitM-1 for development,
// ESP32-C3-MINI-1(U)-N4 module on the product PCB (same GPIO numbering).
//
// ESP32-C3 GPIO budget (22 GPIOs, GPIO0..GPIO21):
//   GPIO11..17  SPI flash / VDD_SPI on modules           -> NOT AVAILABLE
//   GPIO18/19   USB D-/D+ (USB-Serial-JTAG console, OTA-free flashing) -> reserved
//   GPIO2/8/9   strapping pins (GPIO9 = BOOT; GPIO8 must be high for serial download;
//               GPIO2 should be high at reset)          -> only for pulled-up functions
//   GPIO21      U0TXD (ROM boot log)                    -> kept free
//   GPIO20      U0RXD                                   -> input only, used below
//   ADC1 = GPIO0..4 (works with Wi-Fi active); ADC2 (GPIO5) is unusable with Wi-Fi.
//
// Required functions (16) exceed the safe direct GPIO count (11), so the
// non time-critical human interface is on a TCA9555 16-bit I2C expander.
// Everything that switches or protects the motor stays on direct GPIO.
#pragma once

#include <stdint.h>

namespace board {

// ---- Conductive probe front-end (ADC1 channels, see docs/ARCHITECTURE.md §4)
constexpr uint8_t kPinProbe25 = 0;   // ADC1_CH0 (XTAL_32K_P, no 32 kHz crystal fitted)
constexpr uint8_t kPinProbe50 = 1;   // ADC1_CH1 (XTAL_32K_N)
constexpr uint8_t kPinProbe75 = 3;   // ADC1_CH3
constexpr uint8_t kPinProbe95 = 4;   // ADC1_CH4 (MTMS)
constexpr uint8_t kProbePins[4] = {kPinProbe25, kPinProbe50, kPinProbe75, kPinProbe95};

constexpr uint8_t kPinExcCommon = 5;  // drives the common (bottom) electrode via 100R
constexpr uint8_t kPinExcBias = 6;    // drives the shared 220k bias network

// ---- Contactor interface (direct GPIO, safety relevant)
// Drive: GPIO -> 1k -> optocoupler LED (needs >1 mA; immune to weak reset pulls)
//        + 10k pull-down at the pin. Optocoupler output drives the PCB relay
//        whose reinforced-isolation contact switches the contactor coil.
constexpr uint8_t kPinContactorDrive = 10;
constexpr bool kContactorDriveActiveHigh = true;
// Contactor auxiliary NO contact -> isolated input (opto), LOW = contactor closed.
constexpr uint8_t kPinContactorFeedback = 7;
constexpr bool kContactorFeedbackActiveLow = true;
// Thermal overload relay 97-98 (NO) -> isolated input, LOW = overload tripped.
constexpr uint8_t kPinOverload = 20;
constexpr bool kOverloadActiveLow = true;

// ---- I2C bus to the TCA9555 front-panel expander (both lines 4.7k to 3V3,
//      which also keeps the strapping pins high during reset).
constexpr uint8_t kPinI2cSda = 8;  // NOTE: DevKitM-1 RGB LED data also on GPIO8 (cosmetic glitches only)
constexpr uint8_t kPinI2cScl = 2;
constexpr uint32_t kI2cClockHz = 100000;
constexpr uint8_t kExpanderAddress = 0x20;  // TCA9555 A2..A0 = 000

// ---- Service button (GPIO9 = BOOT strap; pressing it during reset enters download mode)
constexpr uint8_t kPinServiceButton = 9;

// ---- TCA9555 port bits (port 0 = inputs, port 1 = outputs)
namespace exp {
constexpr uint8_t kInModeAuto = 0;      // P0.0 selector AUTO contact, active low
constexpr uint8_t kInModeManual = 1;    // P0.1 selector MANUAL contact, active low (OFF = neither)
constexpr uint8_t kInStart = 2;         // P0.2 START push button (NO), active low
constexpr uint8_t kInStopNc = 3;        // P0.3 STOP push button (NC to GND): HIGH = pressed or wire broken
constexpr uint8_t kInHighHigh = 4;      // P0.4 independent HH relay status (opto), active low = tripped
constexpr uint8_t kInEstop = 5;         // P0.5 E-stop status (opto across E-stop), active low = E-stop open
constexpr uint8_t kInHhCircuitOk = 6;   // P0.6 HH circuit healthy (opto, active low = healthy)
constexpr uint8_t kOutLedWifi = 0;      // P1.0
constexpr uint8_t kOutLedMotor = 1;     // P1.1
constexpr uint8_t kOutLedFault = 2;     // P1.2
constexpr uint8_t kOutBuzzer = 3;       // P1.3
}  // namespace exp

// ---- Probe front-end component values (must match the fitted resistors)
constexpr float kProbeExcitationMv = 3300.0f;
constexpr float kProbeSeriesKohm = 82.0f;
constexpr float kProbeBiasKohm = 220.0f;
constexpr float kProbeGndKohm = 470.0f;

}  // namespace board
