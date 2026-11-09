// Safety supervision tests: hardware trips, contactor feedback, runtime limits.
#include <unity.h>

#include "../helpers/TestHelpers.h"

using namespace wintek;
using namespace wintek::test;

void setUp() {}
void tearDown() {}

static Rig runningAuto() {
  Rig r;
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  return r;
}

static void test_overload_trip_latches_fault() {
  Rig r = runningAuto();
  r.in.overloadTripped = true;
  r.tick();
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(MotorState::FAULT_LOCKOUT, r.m.state());
  TEST_ASSERT_EQUAL(FaultCode::MOTOR_OVERLOAD, r.m.activeFault());
  TEST_ASSERT_TRUE(r.m.takeLatchDirty());
  // Still tripped: reset refused.
  TEST_ASSERT_EQUAL(RejectReason::CONDITION_NOT_CLEARED, r.send(CommandAction::RESET_FAULT).reason);
  r.in.overloadTripped = false;
  r.run(10000);
  TEST_ASSERT_EQUAL(MotorState::FAULT_LOCKOUT, r.m.state());  // no auto-reset
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::RESET_FAULT).status);
  TEST_ASSERT_EQUAL(FaultCode::NONE, r.m.latchedFault());
}

static void test_estop_latches_and_blocks_on() {
  Rig r = runningAuto();
  r.in.estopActive = true;
  r.tick();
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(FaultCode::EMERGENCY_STOP, r.m.activeFault());
  TEST_ASSERT_EQUAL(RejectReason::FAULT_LOCKOUT_ACTIVE, r.send(CommandAction::MOTOR_ON).reason);
}

static void test_overflow_hh_trip() {
  Rig r = runningAuto();
  r.in.hhTripped = true;
  r.tick();
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(MotorState::OVERFLOW_LOCKOUT, r.m.state());
  TEST_ASSERT_EQUAL(StopReason::OVERFLOW, r.m.lastStopReason());
}

static void test_contactor_no_pull_in() {
  Rig r;
  r.begin();
  r.in.level = level(0);
  r.feedbackFollows = false;
  r.in.contactorClosed = false;
  r.passBoot();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  r.run(r.cfg.contactorFeedbackTimeoutMs + 200);
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(FaultCode::CONTACTOR_NO_PULL_IN, r.m.activeFault());
}

static void test_contactor_welded_detected_when_off() {
  Rig r = runningAuto();
  r.feedbackFollows = false;
  r.in.contactorClosed = true;
  r.in.level = level(4);
  r.tick();  // stops at 95 %
  TEST_ASSERT_FALSE(r.m.motorOutput());
  r.run(r.cfg.contactorFeedbackTimeoutMs + 200);
  TEST_ASSERT_EQUAL(MotorState::FAULT_LOCKOUT, r.m.state());
  TEST_ASSERT_EQUAL(FaultCode::CONTACTOR_WELDED, r.m.activeFault());
  // Reset refused while feedback still closed.
  TEST_ASSERT_EQUAL(RejectReason::CONDITION_NOT_CLEARED, r.send(CommandAction::RESET_FAULT).reason);
}

static void test_contactor_welded_at_boot() {
  Rig r;
  r.begin();
  r.feedbackFollows = false;
  r.in.contactorClosed = true;
  r.in.level = level(2);
  r.passBoot();
  TEST_ASSERT_EQUAL(FaultCode::CONTACTOR_WELDED, r.m.activeFault());
}

static void test_contactor_dropout_while_running() {
  Rig r = runningAuto();
  r.feedbackFollows = false;
  r.in.contactorClosed = true;
  r.tick();
  r.in.contactorClosed = false;  // opened without HH/OL/E-stop indication
  r.run(r.cfg.contactorFeedbackTimeoutMs + 200);
  TEST_ASSERT_EQUAL(FaultCode::CONTACTOR_DROPOUT, r.m.activeFault());
}

static void test_max_runtime_exceeded() {
  Rig r;
  r.cfg.noRiseTimeoutMs = 0;  // isolate the runtime limit
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  r.in.level = level(1);
  r.run(r.cfg.maxRuntimeMs - 2000, 1000);
  TEST_ASSERT_TRUE(r.m.motorOutput());
  r.run(3000, 1000);
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(FaultCode::MAX_RUNTIME, r.m.activeFault());
  // MAX_RUNTIME can be reset immediately.
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::RESET_FAULT).status);
}

static void test_runtime_warning_event_at_80_percent() {
  Rig r;
  r.cfg.noRiseTimeoutMs = 0;
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  r.drainEvents();
  r.run((r.cfg.maxRuntimeMs / 5) * 4 + 1000, 1000);
  bool warned = false;
  MachineEvent e;
  while (r.m.popEvent(e)) warned |= (e.type == MachineEventType::RUNTIME_WARNING);
  TEST_ASSERT_TRUE(warned);
}

static void test_no_level_rise_detects_dry_run() {
  Rig r = runningAuto();
  r.run(r.cfg.noRiseTimeoutMs - 5000, 1000);
  TEST_ASSERT_TRUE(r.m.motorOutput());
  r.run(6000, 1000);
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(FaultCode::NO_LEVEL_RISE, r.m.activeFault());
}

static void test_level_rise_resets_no_rise_timer() {
  Rig r = runningAuto();
  r.run(r.cfg.noRiseTimeoutMs - 5000, 1000);
  r.in.level = level(1);
  r.run(r.cfg.noRiseTimeoutMs - 5000, 1000);
  TEST_ASSERT_TRUE(r.m.motorOutput());
}

static void test_sensor_fault_stops_and_recovers() {
  Rig r = runningAuto();
  r.in.level = invalidLevel();
  r.tick();
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(MotorState::SENSOR_FAULT, r.m.state());
  TEST_ASSERT_EQUAL(RejectReason::SENSOR_INVALID, r.send(CommandAction::MOTOR_ON).reason);
  r.in.level = level(0);
  r.run(r.cfg.sensorRecoveryMs - 1000);
  TEST_ASSERT_EQUAL(MotorState::SENSOR_FAULT, r.m.state());
  r.run(r.cfg.minOffMs);
  TEST_ASSERT_TRUE(r.m.motorOutput());  // recovered and AUTO resumed
}

static void test_sensor_disconnected_at_boot() {
  Rig r;
  r.begin();
  r.in.level = invalidLevel();
  r.passBoot();
  TEST_ASSERT_EQUAL(MotorState::SENSOR_FAULT, r.m.state());
  TEST_ASSERT_FALSE(r.m.motorOutput());
}

static void test_off_always_accepted_in_lockout() {
  Rig r = runningAuto();
  r.in.overloadTripped = true;
  r.tick();
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::MOTOR_OFF).status);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_overload_trip_latches_fault);
  RUN_TEST(test_estop_latches_and_blocks_on);
  RUN_TEST(test_overflow_hh_trip);
  RUN_TEST(test_contactor_no_pull_in);
  RUN_TEST(test_contactor_welded_detected_when_off);
  RUN_TEST(test_contactor_welded_at_boot);
  RUN_TEST(test_contactor_dropout_while_running);
  RUN_TEST(test_max_runtime_exceeded);
  RUN_TEST(test_runtime_warning_event_at_80_percent);
  RUN_TEST(test_no_level_rise_detects_dry_run);
  RUN_TEST(test_level_rise_resets_no_rise_timer);
  RUN_TEST(test_sensor_fault_stops_and_recovers);
  RUN_TEST(test_sensor_disconnected_at_boot);
  RUN_TEST(test_off_always_accepted_in_lockout);
  return UNITY_END();
}
