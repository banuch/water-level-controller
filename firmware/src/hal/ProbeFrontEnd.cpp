#include "hal/ProbeFrontEnd.h"

#include <Arduino.h>

#include "board_config.h"

namespace wintek {

void ProbeFrontEnd::begin() {
  digitalWrite(board::kPinExcCommon, LOW);
  digitalWrite(board::kPinExcBias, LOW);
  pinMode(board::kPinExcCommon, OUTPUT);
  pinMode(board::kPinExcBias, OUTPUT);
  analogReadResolution(12);
  for (uint8_t pin : board::kProbePins) {
    pinMode(pin, INPUT);  // no internal pulls on the analog nodes
    analogSetPinAttenuation(pin, ADC_11db);  // ESP32-C3: ~0..2500 mV calibrated range
  }
}

void ProbeFrontEnd::acquire(uint16_t settleUs, float vA[kMaxProbes], float vB[kMaxProbes]) {
  // Phase A: common electrode positive.
  digitalWrite(board::kPinExcBias, LOW);
  digitalWrite(board::kPinExcCommon, HIGH);
  delayMicroseconds(settleUs);
  for (uint8_t i = 0; i < kMaxProbes; ++i) vA[i] = static_cast<float>(analogReadMilliVolts(board::kProbePins[i]));

  // Phase B: polarity reversed for the same duration (net DC ~ 0).
  digitalWrite(board::kPinExcCommon, LOW);
  digitalWrite(board::kPinExcBias, HIGH);
  delayMicroseconds(settleUs);
  for (uint8_t i = 0; i < kMaxProbes; ++i) vB[i] = static_cast<float>(analogReadMilliVolts(board::kProbePins[i]));

  // Rest: no excitation between cycles.
  digitalWrite(board::kPinExcBias, LOW);
  digitalWrite(board::kPinExcCommon, LOW);
}

}  // namespace wintek
