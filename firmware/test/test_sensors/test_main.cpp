// Probe front-end math, debounce and level mapping tests.
#include <unity.h>

#include "sensors/LevelEstimator.h"
#include "sensors/ProbeSensor.h"

using namespace wintek;

void setUp() {}
void tearDown() {}

static const ProbeNetwork kNet;

static void test_expected_voltages_stay_inside_adc_range() {
  // ESP32-C3 ADC at 11 dB attenuation is specified up to ~2500 mV.
  TEST_ASSERT_TRUE(expectedPhaseA(kNet, 0) < 2400.0f);
  TEST_ASSERT_TRUE(expectedPhaseB(kNet, -1) < 2400.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, expectedPhaseA(kNet, -1));
}

static void test_wet_probe_classified_and_resistance_estimated() {
  const float kRw[] = {2.0f, 10.0f, 50.0f, 150.0f};
  for (float rw : kRw) {
    const float a = expectedPhaseA(kNet, rw);
    const float b = expectedPhaseB(kNet, rw);
    ProbeSample s = classifyProbe(kNet, a, b, 250, 500, ProbeState::DRY);
    TEST_ASSERT_EQUAL(ProbeState::WET, s.state);
    TEST_ASSERT_FLOAT_WITHIN(rw * 0.05f + 1.0f, rw, s.waterKohm);
  }
}

static void test_dry_probe_classified() {
  ProbeSample s = classifyProbe(kNet, 0.0f, expectedPhaseB(kNet, -1), 250, 500, ProbeState::WET);
  TEST_ASSERT_EQUAL(ProbeState::DRY, s.state);
  // Very high resistance film (condensation) is dry too.
  s = classifyProbe(kNet, expectedPhaseA(kNet, 2000), expectedPhaseB(kNet, 2000), 250, 500, ProbeState::WET);
  TEST_ASSERT_EQUAL(ProbeState::DRY, s.state);
}

static void test_hysteresis_band_keeps_previous_state() {
  const float a = expectedPhaseA(kNet, 350), b = expectedPhaseB(kNet, 350);
  TEST_ASSERT_EQUAL(ProbeState::WET, classifyProbe(kNet, a, b, 250, 500, ProbeState::WET).state);
  TEST_ASSERT_EQUAL(ProbeState::DRY, classifyProbe(kNet, a, b, 250, 500, ProbeState::DRY).state);
}

static void test_open_bias_resistor_detected_even_when_dry() {
  // Bias network open: phase B reads ~0 instead of the divider voltage.
  ProbeSample s = classifyProbe(kNet, 0.0f, 0.0f, 250, 500, ProbeState::DRY);
  TEST_ASSERT_EQUAL(ProbeState::FAULT, s.state);
}

static void test_short_to_supply_detected() {
  ProbeSample s = classifyProbe(kNet, 3300.0f, 3300.0f, 250, 500, ProbeState::DRY);
  TEST_ASSERT_EQUAL(ProbeState::FAULT, s.state);
}

static void test_leakage_to_ground_detected() {
  // Phase A looks wet-ish but phase B is far too low for that resistance.
  ProbeSample s = classifyProbe(kNet, expectedPhaseA(kNet, 100), 50.0f, 250, 500, ProbeState::DRY);
  TEST_ASSERT_EQUAL(ProbeState::FAULT, s.state);
}

static void test_debounce_rejects_splash() {
  ProbeFilterConfig cfg;
  cfg.debounceMs = 3000;
  ProbeChannelFilter f;
  uint32_t t = 0;
  f.update(ProbeState::DRY, t, cfg);
  // 1 s splash
  for (int i = 0; i < 4; ++i) f.update(ProbeState::WET, t += 250, cfg);
  TEST_ASSERT_EQUAL(ProbeState::DRY, f.committed());
  f.update(ProbeState::DRY, t += 250, cfg);
  TEST_ASSERT_EQUAL(ProbeState::DRY, f.committed());
  // Stable wet for > 3 s commits.
  for (int i = 0; i < 14; ++i) f.update(ProbeState::WET, t += 250, cfg);
  TEST_ASSERT_EQUAL(ProbeState::WET, f.committed());
}

static void test_fault_requires_confirmation() {
  ProbeFilterConfig cfg;
  cfg.faultConfirmMs = 5000;
  ProbeChannelFilter f;
  uint32_t t = 0;
  f.update(ProbeState::WET, t, cfg);
  for (int i = 0; i < 16; ++i) f.update(ProbeState::FAULT, t += 250, cfg);
  TEST_ASSERT_EQUAL(ProbeState::WET, f.committed());
  for (int i = 0; i < 8; ++i) f.update(ProbeState::FAULT, t += 250, cfg);
  TEST_ASSERT_EQUAL(ProbeState::FAULT, f.committed());
}

static void test_level_mapping_discrete() {
  ProbeLayout l;
  ProbeState s[kMaxProbes] = {ProbeState::DRY, ProbeState::DRY, ProbeState::DRY, ProbeState::DRY};
  LevelReading r = estimateLevel(l, s);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_EQUAL_UINT8(0, r.levelPercent);
  TEST_ASSERT_EQUAL_UINT8(25, r.rangeHighPercent);
  s[0] = s[1] = ProbeState::WET;
  r = estimateLevel(l, s);
  TEST_ASSERT_EQUAL_UINT8(50, r.levelPercent);
  TEST_ASSERT_EQUAL_UINT8(50, r.rangeLowPercent);
  TEST_ASSERT_EQUAL_UINT8(75, r.rangeHighPercent);
  s[2] = s[3] = ProbeState::WET;
  r = estimateLevel(l, s);
  TEST_ASSERT_EQUAL_UINT8(95, r.levelPercent);
  TEST_ASSERT_EQUAL_UINT8(100, r.rangeHighPercent);
}

static void test_invalid_probe_sequence() {
  ProbeLayout l;
  ProbeState s[kMaxProbes] = {ProbeState::WET, ProbeState::DRY, ProbeState::WET, ProbeState::DRY};
  LevelReading r = estimateLevel(l, s);
  TEST_ASSERT_FALSE(r.valid);
  TEST_ASSERT_EQUAL(FaultCode::SENSOR_SEQUENCE, r.fault);
  // Broken 25 % probe wire while tank is half full looks like this too.
  ProbeState s2[kMaxProbes] = {ProbeState::DRY, ProbeState::WET, ProbeState::DRY, ProbeState::DRY};
  TEST_ASSERT_FALSE(estimateLevel(l, s2).valid);
}

static void test_channel_fault_invalidates_level() {
  ProbeLayout l;
  ProbeState s[kMaxProbes] = {ProbeState::WET, ProbeState::FAULT, ProbeState::DRY, ProbeState::DRY};
  LevelReading r = estimateLevel(l, s);
  TEST_ASSERT_FALSE(r.valid);
  TEST_ASSERT_EQUAL(FaultCode::SENSOR_CHAIN, r.fault);
}

static void test_three_probe_install() {
  ProbeLayout l;
  l.probeCount = 3;
  l.positionPercent[0] = 30;
  l.positionPercent[1] = 60;
  l.positionPercent[2] = 90;
  ProbeState s[kMaxProbes] = {ProbeState::WET, ProbeState::WET, ProbeState::WET, ProbeState::DRY};
  LevelReading r = estimateLevel(l, s);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_EQUAL_UINT8(90, r.levelPercent);
  TEST_ASSERT_TRUE(isAtOrAboveOffThreshold(r, 2));
}

static void test_probe_array_end_to_end() {
  ProbeArray a;
  ProbeFilterConfig fc;
  a.configure(4, kNet, 250, 500, fc);
  float vA[kMaxProbes], vB[kMaxProbes];
  for (uint8_t i = 0; i < kMaxProbes; ++i) {
    const float rw = (i < 2) ? 20.0f : -1.0f;
    vA[i] = expectedPhaseA(kNet, rw);
    vB[i] = expectedPhaseB(kNet, rw);
  }
  for (uint32_t t = 0; t <= 4000; t += 250) a.update(vA, vB, t);
  TEST_ASSERT_EQUAL(ProbeState::WET, a.committed(0));
  TEST_ASSERT_EQUAL(ProbeState::WET, a.committed(1));
  TEST_ASSERT_EQUAL(ProbeState::DRY, a.committed(2));
  TEST_ASSERT_FALSE(a.unusedChannelWet());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_expected_voltages_stay_inside_adc_range);
  RUN_TEST(test_wet_probe_classified_and_resistance_estimated);
  RUN_TEST(test_dry_probe_classified);
  RUN_TEST(test_hysteresis_band_keeps_previous_state);
  RUN_TEST(test_open_bias_resistor_detected_even_when_dry);
  RUN_TEST(test_short_to_supply_detected);
  RUN_TEST(test_leakage_to_ground_detected);
  RUN_TEST(test_debounce_rejects_splash);
  RUN_TEST(test_fault_requires_confirmation);
  RUN_TEST(test_level_mapping_discrete);
  RUN_TEST(test_invalid_probe_sequence);
  RUN_TEST(test_channel_fault_invalidates_level);
  RUN_TEST(test_three_probe_install);
  RUN_TEST(test_probe_array_end_to_end);
  return UNITY_END();
}
