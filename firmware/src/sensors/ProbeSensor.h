// Conductive probe channel decoding and debounce (hardware independent).
//
// Front-end per probe channel (see docs/ARCHITECTURE.md §4):
//
//   EXC_COM (GPIO) --100R--> COMMON electrode (tank bottom)
//   PROBE_n electrode --R_series(82k)--+--> ADC pin (1nF to GND, ESD diode)
//                                      +--R_bias(220k)--> EXC_BIAS (GPIO, shared)
//                                      +--R_gnd(470k)---> GND
//
// Every acquisition cycle is two short, equal-length phases of opposite
// polarity, so the average DC current through the water is ~0:
//   Phase A: EXC_COM = V, EXC_BIAS = 0  -> vA rises when the probe is wet
//   Phase B: EXC_COM = 0, EXC_BIAS = V  -> vB falls when the probe is wet
// Between cycles both lines are held low (no current).
//
// The pair (vA, vB) over-determines the single unknown (water path
// resistance Rw), which gives an electrical self-test of every channel even
// when the probe is dry (vB must equal the bias divider voltage).
#pragma once

#include "core/Types.h"

namespace wintek {

struct ProbeNetwork {
  float excitationMv = 3300.0f;  // GPIO high level (VDD 3.3 V)
  float rSeriesKohm = 82.0f;
  float rBiasKohm = 220.0f;
  float rGndKohm = 470.0f;
  float consistencyToleranceMv = 350.0f;  // allowed |vB - expected(vB)|
  float noiseFloorMv = 15.0f;             // vA below this => open circuit (dry)
};

struct ProbeSample {
  ProbeState state = ProbeState::FAULT;  // instantaneous classification
  float waterKohm = -1.0f;               // estimated Rw; <0 means open (no water path)
  float vAMv = 0;
  float vBMv = 0;
};

// Pure math: classify one channel from its two phase voltages.
// wetBelowKohm / dryAboveKohm form a hysteresis band; `previous` is the
// channel's last instantaneous state and is kept while Rw lies in the band.
ProbeSample classifyProbe(const ProbeNetwork& net, float vAMv, float vBMv, float wetBelowKohm,
                          float dryAboveKohm, ProbeState previous);

// Expected phase voltages for a given water resistance (Rw < 0 == open).
float expectedPhaseA(const ProbeNetwork& net, float waterKohm);
float expectedPhaseB(const ProbeNetwork& net, float waterKohm);

struct ProbeFilterConfig {
  uint32_t debounceMs = 3000;      // wet<->dry must be stable this long
  uint32_t faultConfirmMs = 5000;  // channel fault must persist this long
};

// Per-channel debounce. Splashes and ripples produce short wet/dry flicker
// that is rejected unless it persists for debounceMs.
class ProbeChannelFilter {
 public:
  void reset(ProbeState initial, uint32_t nowMs);
  // Returns the committed (debounced) state.
  ProbeState update(ProbeState instantaneous, uint32_t nowMs, const ProbeFilterConfig& cfg);
  ProbeState committed() const { return committed_; }
  bool initialized() const { return initialized_; }

 private:
  bool initialized_ = false;
  ProbeState committed_ = ProbeState::DRY;
  ProbeState candidate_ = ProbeState::DRY;
  uint32_t candidateSinceMs_ = 0;
};

// Set of channels + diagnostics. Owned by the control task.
class ProbeArray {
 public:
  void configure(uint8_t probeCount, const ProbeNetwork& net, float wetBelowKohm,
                 float dryAboveKohm, const ProbeFilterConfig& filter);
  // Feed one acquisition cycle (millivolts per channel, both phases).
  void update(const float vAMv[kMaxProbes], const float vBMv[kMaxProbes], uint32_t nowMs);

  uint8_t probeCount() const { return probeCount_; }
  ProbeState committed(uint8_t i) const { return filters_[i].committed(); }
  const ProbeSample& lastSample(uint8_t i) const { return samples_[i]; }
  uint32_t lastUpdateMs() const { return lastUpdateMs_; }
  bool hasData() const { return hasData_; }
  // Unused channels (index >= probeCount) must read dry; a wet reading
  // there indicates wiring/contamination and is reported as a warning.
  bool unusedChannelWet() const { return unusedChannelWet_; }

 private:
  uint8_t probeCount_ = kMaxProbes;
  ProbeNetwork net_;
  float wetBelowKohm_ = 250.0f;
  float dryAboveKohm_ = 500.0f;
  ProbeFilterConfig filter_;
  ProbeChannelFilter filters_[kMaxProbes];
  ProbeSample samples_[kMaxProbes];
  bool hasData_ = false;
  bool unusedChannelWet_ = false;
  uint32_t lastUpdateMs_ = 0;
};

}  // namespace wintek
