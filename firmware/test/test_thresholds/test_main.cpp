// Configuration / threshold validation tests.
#include <string.h>
#include <unity.h>

#include "storage/Config.h"

using namespace wintek;

void setUp() {}
void tearDown() {}

static void test_defaults_are_valid() {
  DeviceConfig c;
  TEST_ASSERT_EQUAL(ConfigError::OK, validateConfig(c));
  TEST_ASSERT_EQUAL_UINT8(25, probePercent(c, 0));
  TEST_ASSERT_EQUAL_UINT8(50, probePercent(c, 1));
  TEST_ASSERT_EQUAL_UINT8(75, probePercent(c, 2));
  TEST_ASSERT_EQUAL_UINT8(95, probePercent(c, 3));
}

static void test_on_must_be_below_off() {
  DeviceConfig c;
  c.onProbeIndex = 3;
  c.offProbeIndex = 3;
  TEST_ASSERT_EQUAL(ConfigError::THRESHOLD_ORDER, validateConfig(c));
  c.onProbeIndex = 2;
  c.offProbeIndex = 1;
  TEST_ASSERT_EQUAL(ConfigError::THRESHOLD_ORDER, validateConfig(c));
  c.onProbeIndex = 1;
  c.offProbeIndex = 2;
  TEST_ASSERT_EQUAL(ConfigError::OK, validateConfig(c));
}

static void test_threshold_must_reference_installed_probe() {
  DeviceConfig c;
  c.probeCount = 3;
  c.offProbeIndex = 3;
  TEST_ASSERT_EQUAL(ConfigError::THRESHOLD_INDEX, validateConfig(c));
}

static void test_probe_heights_ascending_and_spaced() {
  DeviceConfig c;
  c.probeHeightMm[2] = 550;  // below probe 1 (600)
  TEST_ASSERT_EQUAL(ConfigError::PROBE_ORDER, validateConfig(c));
  c.probeHeightMm[2] = 630;  // only 30 mm above
  TEST_ASSERT_EQUAL(ConfigError::PROBE_SPACING, validateConfig(c));
}

static void test_top_probe_clearance_for_hh() {
  DeviceConfig c;
  c.probeHeightMm[3] = 1190;
  TEST_ASSERT_EQUAL(ConfigError::PROBE_TOP_CLEARANCE, validateConfig(c));
}

static void test_timing_limits() {
  DeviceConfig c;
  c.minOffMs = 10000;  // below 30 s floor
  TEST_ASSERT_EQUAL(ConfigError::MIN_OFF, validateConfig(c));
  c = DeviceConfig();
  c.maxRuntimeMs = 60000;
  TEST_ASSERT_EQUAL(ConfigError::MAX_RUNTIME, validateConfig(c));
  c = DeviceConfig();
  c.noRiseTimeoutMs = c.maxRuntimeMs + 1;
  TEST_ASSERT_EQUAL(ConfigError::NO_RISE, validateConfig(c));
  c.noRiseTimeoutMs = 0;  // disabled is allowed
  TEST_ASSERT_EQUAL(ConfigError::OK, validateConfig(c));
}

static void test_sensitivity_needs_hysteresis() {
  DeviceConfig c;
  c.wetBelowKohm = 400;
  c.dryAboveKohm = 420;
  TEST_ASSERT_EQUAL(ConfigError::SENSITIVITY, validateConfig(c));
}

static void test_schedule_validation_and_conflict() {
  DeviceConfig c;
  c.schedules[0].enabled = true;
  c.schedules[0].type = ScheduleType::WINDOW;
  c.schedules[0].startMinute = 600;
  c.schedules[0].endMinute = 600;
  TEST_ASSERT_EQUAL(ConfigError::SCHEDULE, validateConfig(c));
  c.schedules[0].endMinute = 120;  // wraps midnight: allowed
  TEST_ASSERT_EQUAL(ConfigError::OK, validateConfig(c));

  c.schedules[1].enabled = true;
  c.schedules[1].type = ScheduleType::FILL;
  c.schedules[1].startMinute = 360;
  c.schedules[2].enabled = true;
  c.schedules[2].type = ScheduleType::FILL;
  c.schedules[2].startMinute = 375;
  TEST_ASSERT_EQUAL(ConfigError::SCHEDULE_CONFLICT, validateConfig(c));
  c.schedules[2].daysMask = 0x01;
  c.schedules[1].daysMask = 0x02;  // different days: no conflict
  TEST_ASSERT_EQUAL(ConfigError::OK, validateConfig(c));
}

static void test_device_name_and_timezone() {
  DeviceConfig c;
  c.deviceName[0] = '\0';
  TEST_ASSERT_EQUAL(ConfigError::DEVICE_NAME, validateConfig(c));
  c = DeviceConfig();
  strcpy(c.timezone, "X");
  TEST_ASSERT_EQUAL(ConfigError::TIMEZONE, validateConfig(c));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_are_valid);
  RUN_TEST(test_on_must_be_below_off);
  RUN_TEST(test_threshold_must_reference_installed_probe);
  RUN_TEST(test_probe_heights_ascending_and_spaced);
  RUN_TEST(test_top_probe_clearance_for_hh);
  RUN_TEST(test_timing_limits);
  RUN_TEST(test_sensitivity_needs_hysteresis);
  RUN_TEST(test_schedule_validation_and_conflict);
  RUN_TEST(test_device_name_and_timezone);
  return UNITY_END();
}
