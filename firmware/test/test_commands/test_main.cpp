// Command envelope parsing, expiry, duplicates, OTA manifest and alerts.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "services/AlertManager.h"
#include "services/CommandProcessor.h"
#include "services/OtaManifest.h"

using namespace wintek;

void setUp() {}
void tearDown() {}

static const char* kOn =
    R"({"commandId":"cmd-1001","action":"MOTOR_ON","mode":"MANUAL",)"
    R"("timestamp":"2026-11-09T08:30:00Z","expiresAt":"2026-11-09T08:30:30Z"})";

static void test_iso8601_roundtrip() {
  int64_t t = 0;
  TEST_ASSERT_TRUE(parseIso8601("2026-11-09T08:30:00Z", t));
  char buf[32];
  formatIso8601(t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("2026-11-09T08:30:00Z", buf);
  int64_t t2 = 0;
  TEST_ASSERT_TRUE(parseIso8601("2026-11-09T14:00:00+05:30", t2));
  TEST_ASSERT_EQUAL_INT64(t, t2);
  TEST_ASSERT_TRUE(parseIso8601("1970-01-01T00:00:00.123Z", t));
  TEST_ASSERT_EQUAL_INT64(0, t);
  TEST_ASSERT_FALSE(parseIso8601("2026-11-09T08:30:00", t));  // no zone
  TEST_ASSERT_FALSE(parseIso8601("2026-13-09T08:30:00Z", t));
}

static void test_parse_motor_command() {
  ParsedCommand c;
  TEST_ASSERT_EQUAL(ParseError::OK, parseCommandJson(kOn, strlen(kOn), c));
  TEST_ASSERT_EQUAL_STRING("cmd-1001", c.commandId);
  TEST_ASSERT_EQUAL(CommandAction::MOTOR_ON, c.cmd.action);
  TEST_ASSERT_TRUE(c.cmd.modeSpecified);
  TEST_ASSERT_EQUAL(ControlMode::MANUAL, c.cmd.mode);
  TEST_ASSERT_EQUAL_INT64(30, c.expiresAtS - c.issuedAtS);
}

static void test_parse_rejects_bad_input() {
  ParsedCommand c;
  const char* bad1 = "{not json";
  TEST_ASSERT_EQUAL(ParseError::MALFORMED_JSON, parseCommandJson(bad1, strlen(bad1), c));
  const char* bad2 = R"({"action":"MOTOR_ON"})";
  TEST_ASSERT_EQUAL(ParseError::MISSING_COMMAND_ID, parseCommandJson(bad2, strlen(bad2), c));
  const char* bad3 = R"({"commandId":"abcd","action":"EXPLODE"})";
  TEST_ASSERT_EQUAL(ParseError::UNKNOWN_ACTION, parseCommandJson(bad3, strlen(bad3), c));
  const char* bad4 = R"({"commandId":"abcd","action":"SET_MODE","mode":"OFF"})";
  TEST_ASSERT_EQUAL(ParseError::BAD_MODE, parseCommandJson(bad4, strlen(bad4), c));
  const char* bad5 = R"({"commandId":"ab cd","action":"MOTOR_OFF"})";
  TEST_ASSERT_EQUAL(ParseError::BAD_COMMAND_ID, parseCommandJson(bad5, strlen(bad5), c));
}

static void test_expiry_rules() {
  ParsedCommand c;
  parseCommandJson(kOn, strlen(kOn), c);
  int64_t now;
  parseIso8601("2026-11-09T08:30:10Z", now);
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, checkFreshness(c, true, now, true));
  parseIso8601("2026-11-09T08:30:30Z", now);
  TEST_ASSERT_EQUAL(CommandStatus::EXPIRED, checkFreshness(c, true, now, true));
  // Device clock unknown: cloud command cannot be proven fresh.
  TEST_ASSERT_EQUAL(CommandStatus::EXPIRED, checkFreshness(c, false, 0, true));
  // Lifetime longer than 120 s is invalid.
  const char* longLived =
      R"({"commandId":"cmd-2","action":"MOTOR_OFF","timestamp":"2026-11-09T08:30:00Z","expiresAt":"2026-11-09T09:30:00Z"})";
  parseCommandJson(longLived, strlen(longLived), c);
  TEST_ASSERT_EQUAL(CommandStatus::INVALID, checkFreshness(c, true, now, true));
  // Cloud commands must carry both fields.
  const char* bare = R"({"commandId":"cmd-3","action":"MOTOR_OFF"})";
  parseCommandJson(bare, strlen(bare), c);
  TEST_ASSERT_EQUAL(CommandStatus::INVALID, checkFreshness(c, true, now, true));
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, checkFreshness(c, true, now, false));  // local API
}

static void test_dedup() {
  CommandDeduplicator d;
  CommandOutcome o, prev;
  o.status = CommandStatus::ACCEPTED;
  TEST_ASSERT_FALSE(d.lookup("cmd-1", prev));
  d.remember("cmd-1", o);
  TEST_ASSERT_TRUE(d.lookup("cmd-1", prev));
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, prev.status);
  char id[16];
  for (int i = 0; i < CommandDeduplicator::kSize; ++i) {
    snprintf(id, sizeof(id), "x-%d", i);
    d.remember(id, o);
  }
  TEST_ASSERT_FALSE(d.lookup("cmd-1", prev));  // evicted (bounded memory)
}

static void test_ota_manifest() {
  const char* j =
      R"({"commandId":"ota-1","action":"OTA_UPDATE","timestamp":"2026-11-09T08:30:00Z","expiresAt":"2026-11-09T08:31:00Z",)"
      R"("ota":{"version":"1.1.0","url":"https://fw.example.com/wt/1.1.0.bin",)"
      R"("sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
      R"("signature":"MEUCIQDexampleexampleexampleexampleexampleexampleexampleexampleAiB=","size":1200000,"hwRev":"A"}})";
  ParsedCommand c;
  TEST_ASSERT_EQUAL(ParseError::OK, parseCommandJson(j, strlen(j), c));
  TEST_ASSERT_EQUAL(CommandKind::OTA_UPDATE, c.kind);
  TEST_ASSERT_EQUAL(OtaManifestError::OK, validateManifest(c.ota, "1.0.0", "A", 0x1E0000, false));
  TEST_ASSERT_EQUAL(OtaManifestError::NOT_NEWER, validateManifest(c.ota, "1.1.0", "A", 0x1E0000, false));
  TEST_ASSERT_EQUAL(OtaManifestError::HW_MISMATCH, validateManifest(c.ota, "1.0.0", "B", 0x1E0000, false));
  TEST_ASSERT_EQUAL(OtaManifestError::BAD_SIZE, validateManifest(c.ota, "1.0.0", "A", 1000000, false));
  strcpy(c.ota.url, "http://fw.example.com/x.bin");
  TEST_ASSERT_EQUAL(OtaManifestError::BAD_URL, validateManifest(c.ota, "1.0.0", "A", 0x1E0000, false));
}

static void test_semver() {
  SemVer a, b;
  TEST_ASSERT_TRUE(parseSemVer("1.2.10", a));
  TEST_ASSERT_TRUE(parseSemVer("v1.2.9-rc1", b));
  TEST_ASSERT_EQUAL_INT(1, compareSemVer(a, b));
  TEST_ASSERT_FALSE(parseSemVer("1.2", a));
}

static void test_alert_repeat_suppression() {
  AlertManager m;
  Alert a;
  TEST_ASSERT_TRUE(m.raise(AlertType::SENSOR_FAULT, FaultCode::SENSOR_SEQUENCE, 1000, 50, a));
  TEST_ASSERT_EQUAL(AlertSeverity::WARNING, a.severity);
  TEST_ASSERT_FALSE(m.raise(AlertType::SENSOR_FAULT, FaultCode::SENSOR_SEQUENCE, 61000, 50, a));
  TEST_ASSERT_FALSE(m.raise(AlertType::SENSOR_FAULT, FaultCode::SENSOR_SEQUENCE, 121000, 50, a));
  TEST_ASSERT_TRUE(m.raise(AlertType::SENSOR_FAULT, FaultCode::SENSOR_SEQUENCE, 1000 + 600000, 50, a));
  TEST_ASSERT_EQUAL_UINT16(2, a.suppressedRepeats);
  // Different fault code is a different alert.
  TEST_ASSERT_TRUE(m.raise(AlertType::SENSOR_FAULT, FaultCode::SENSOR_CHAIN, 1000 + 600001, 50, a));
}

static void test_alert_from_events() {
  AlertManager m;
  MachineEvent e;
  Alert a;
  e.type = MachineEventType::FAULT_RAISED;
  e.fault = FaultCode::OVERFLOW_HH;
  TEST_ASSERT_TRUE(m.fromEvent(e, a));
  TEST_ASSERT_EQUAL(AlertType::OVERFLOW_CUTOFF, a.type);
  TEST_ASSERT_EQUAL(AlertSeverity::CRITICAL, a.severity);
  e.type = MachineEventType::MOTOR_STARTED;
  TEST_ASSERT_TRUE(m.fromEvent(e, a));
  TEST_ASSERT_TRUE(m.fromEvent(e, a));  // motor events never suppressed
  e.type = MachineEventType::STATE_CHANGED;
  TEST_ASSERT_FALSE(m.fromEvent(e, a));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_iso8601_roundtrip);
  RUN_TEST(test_parse_motor_command);
  RUN_TEST(test_parse_rejects_bad_input);
  RUN_TEST(test_expiry_rules);
  RUN_TEST(test_dedup);
  RUN_TEST(test_ota_manifest);
  RUN_TEST(test_semver);
  RUN_TEST(test_alert_repeat_suppression);
  RUN_TEST(test_alert_from_events);
  return UNITY_END();
}
