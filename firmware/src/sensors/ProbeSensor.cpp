#include "sensors/ProbeSensor.h"

#include <math.h>

namespace wintek {

namespace {
inline float parallel(float a, float b) { return (a * b) / (a + b); }
}  // namespace

float expectedPhaseA(const ProbeNetwork& n, float waterKohm) {
  if (waterKohm < 0) return 0.0f;  // open: node pulled to 0 by bias(0 V) and gnd
  const float rLow = parallel(n.rBiasKohm, n.rGndKohm);
  const float rHigh = n.rSeriesKohm + waterKohm;
  return n.excitationMv * rLow / (rHigh + rLow);
}

float expectedPhaseB(const ProbeNetwork& n, float waterKohm) {
  // Node pulled up by R_bias to V, down by R_gnd and (R_series+Rw) to 0.
  const float rLow = (waterKohm < 0) ? n.rGndKohm
                                      : parallel(n.rGndKohm, n.rSeriesKohm + waterKohm);
  return n.excitationMv * rLow / (n.rBiasKohm + rLow);
}

ProbeSample classifyProbe(const ProbeNetwork& n, float vA, float vB, float wetBelowKohm,
                          float dryAboveKohm, ProbeState previous) {
  ProbeSample s;
  s.vAMv = vA;
  s.vBMv = vB;

  // 1) Range check against physically possible extremes (+tolerance).
  const float vAMax = expectedPhaseA(n, 0.0f) + n.consistencyToleranceMv;
  const float vBMax = expectedPhaseB(n, -1.0f) + n.consistencyToleranceMv;
  if (vA > vAMax || vB > vBMax || vA < -1.0f || vB < -1.0f) {
    s.state = ProbeState::FAULT;  // short to supply, ADC fault, bias network fault
    return s;
  }

  // 2) Estimate the water path from phase A.
  float rw = -1.0f;
  if (vA > n.noiseFloorMv) {
    const float rLow = parallel(n.rBiasKohm, n.rGndKohm);
    rw = rLow * (n.excitationMv - vA) / vA - n.rSeriesKohm;
    if (rw < 0) rw = 0;
  }
  s.waterKohm = rw;

  // 3) Cross-check with phase B. A dry probe must still produce the bias
  //    divider voltage, so this also proves the channel wiring is intact.
  const float vBExpected = expectedPhaseB(n, rw);
  if (fabsf(vB - vBExpected) > n.consistencyToleranceMv) {
    s.state = ProbeState::FAULT;  // leakage to ground, open bias resistor, DC offset
    return s;
  }

  // 4) Wet/dry with hysteresis.
  if (rw >= 0 && rw < wetBelowKohm) {
    s.state = ProbeState::WET;
  } else if (rw < 0 || rw > dryAboveKohm) {
    s.state = ProbeState::DRY;
  } else {
    s.state = (previous == ProbeState::FAULT) ? ProbeState::DRY : previous;
  }
  return s;
}

void ProbeChannelFilter::reset(ProbeState initial, uint32_t nowMs) {
  initialized_ = true;
  committed_ = initial;
  candidate_ = initial;
  candidateSinceMs_ = nowMs;
}

ProbeState ProbeChannelFilter::update(ProbeState inst, uint32_t nowMs, const ProbeFilterConfig& cfg) {
  if (!initialized_) {
    // First valid sample is not trusted blindly: start as FAULT-free DRY
    // candidate tracking so the level is only reported after debounce.
    initialized_ = true;
    committed_ = (inst == ProbeState::FAULT) ? ProbeState::DRY : inst;
    candidate_ = inst;
    candidateSinceMs_ = nowMs;
    return committed_;
  }
  if (inst != candidate_) {
    candidate_ = inst;
    candidateSinceMs_ = nowMs;
  }
  if (candidate_ != committed_) {
    const uint32_t hold = (candidate_ == ProbeState::FAULT) ? cfg.faultConfirmMs : cfg.debounceMs;
    if (hasElapsed(nowMs, candidateSinceMs_, hold)) committed_ = candidate_;
  }
  return committed_;
}

void ProbeArray::configure(uint8_t probeCount, const ProbeNetwork& net, float wetBelowKohm,
                           float dryAboveKohm, const ProbeFilterConfig& filter) {
  probeCount_ = (probeCount > kMaxProbes) ? kMaxProbes : probeCount;
  net_ = net;
  wetBelowKohm_ = wetBelowKohm;
  dryAboveKohm_ = dryAboveKohm;
  filter_ = filter;
}

void ProbeArray::update(const float vA[kMaxProbes], const float vB[kMaxProbes], uint32_t nowMs) {
  bool unusedWet = false;
  for (uint8_t i = 0; i < kMaxProbes; ++i) {
    const ProbeState prev = samples_[i].state;
    samples_[i] = classifyProbe(net_, vA[i], vB[i], wetBelowKohm_, dryAboveKohm_, prev);
    if (i < probeCount_) {
      filters_[i].update(samples_[i].state, nowMs, filter_);
    } else if (samples_[i].state == ProbeState::WET) {
      unusedWet = true;
    }
  }
  unusedChannelWet_ = unusedWet;
  lastUpdateMs_ = nowMs;
  hasData_ = true;
}

}  // namespace wintek
