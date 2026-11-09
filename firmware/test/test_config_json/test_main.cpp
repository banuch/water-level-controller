// Configuration JSON round trip and patch validation.
#include <ArduinoJson.h>
#include <unity.h>

#include "storage/ConfigJson.h"

using namespace wintek;

void setUp() {}
void tearDown() {}

static ConfigError patch(const char* json, DeviceConfig& c, const char*& field) {
  JsonDocument d;
  deserializeJson(d, json);
  return applyConfigPatch(d.as<JsonObjectConst>(), c, field);
}

static void test_roundtrip_exposes_probe_percentages() {
  DeviceConfig c;
  JsonDocument d;
  configToJson(c, d.to<JsonObject>());
  TEST_ASSERT_EQUAL_UINT8(25, d["thresholds"]["onPercent"].as<uint8_t>());
  TEST_ASSERT_EQUAL_UINT8(95, d["thresholds"]["offPercent"].as<uint8_t>());
  TEST_ASSERT_EQUAL(4, d["tank"]["probes"].size());
  TEST_ASSERT_EQUAL(kMaxSchedules, d["schedule"]["entries"].size());
}

static void test_threshold_by_percent_must_match_probe() {
  DeviceConfig c;
  const char* f = nullptr;
  TEST_ASSERT_EQUAL(ConfigError::OK, patch(R"({"thresholds":{"onPercent":50,"offPercent":95}})", c, f));
  TEST_ASSERT_EQUAL_UINT8(1, c.onProbeIndex);
  // 60 % is not an installed probe position: no interpolated thresholds.
  TEST_ASSERT_EQUAL(ConfigError::BAD_FIELD, patch(R"({"thresholds":{"onPercent":60}})", c, f));
  TEST_ASSERT_EQUAL_STRING("thresholds.onPercent", f);
  TEST_ASSERT_EQUAL_UINT8(1, c.onProbeIndex);  // unchanged on failure
}

static void test_invalid_patch_leaves_config_untouched() {
  DeviceConfig c;
  const char* f = nullptr;
  TEST_ASSERT_EQUAL(ConfigError::MIN_OFF, patch(R"({"motor":{"minOffMs":1000}})", c, f));
  TEST_ASSERT_EQUAL_UINT32(120000, c.minOffMs);
  TEST_ASSERT_EQUAL(ConfigError::BAD_FIELD, patch(R"({"motor":{"minOffMs":"fast"}})", c, f));
  TEST_ASSERT_EQUAL(ConfigError::BAD_FIELD, patch(R"({"motor":{"maxStartsPerHour":300}})", c, f));
}

static void test_schedule_patch() {
  DeviceConfig c;
  const char* f = nullptr;
  const char* j =
      R"({"schedule":{"restrictEnabled":true,"timezone":"IST-5:30","entries":[)"
      R"({"enabled":true,"type":"WINDOW","daysMask":127,"startMinute":300,"endMinute":540},)"
      R"({"enabled":true,"type":"FILL","daysMask":62,"startMinute":1080}]}})";
  TEST_ASSERT_EQUAL(ConfigError::OK, patch(j, c, f));
  TEST_ASSERT_TRUE(c.scheduleRestrictEnabled);
  TEST_ASSERT_EQUAL(ScheduleType::FILL, c.schedules[1].type);
  TEST_ASSERT_FALSE(c.schedules[2].enabled);
  TEST_ASSERT_EQUAL(ConfigError::BAD_FIELD,
                    patch(R"({"schedule":{"entries":[{"enabled":true,"type":"ALWAYS"}]}})", c, f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip_exposes_probe_percentages);
  RUN_TEST(test_threshold_by_percent_must_match_probe);
  RUN_TEST(test_invalid_patch_leaves_config_untouched);
  RUN_TEST(test_schedule_patch);
  return UNITY_END();
}
