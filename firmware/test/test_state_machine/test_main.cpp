// Motor state machine behaviour tests (run on host: `pio test -e native`).
#include <unity.h>

#include "../helpers/TestHelpers.h"

using namespace wintek;
using namespace wintek::test;

void setUp() {}
void tearDown() {}

static void test_boot_starts_with_motor_off_and_waits_restart_delay() {
  Rig r;
  r.begin();
  r.in.level = level(0);  // tank empty: AUTO would want to start
  TEST_ASSERT_EQUAL(MotorState::BOOT_SELF_TEST, r.m.state());
  TEST_ASSERT_FALSE(r.m.motorOutput());
  r.run(r.cfg.restartDelayMs - 500);
  TEST_ASSERT_EQUAL(MotorState::BOOT_SELF_TEST, r.m.state());
  TEST_ASSERT_FALSE(r.m.motorOutput());
  r.run(1000);
  TEST_ASSERT_TRUE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(MotorState::AUTO_FILLING, r.m.state());
}

static void test_manual_mode_restored_but_motor_not_restarted() {
  Rig r;
  r.begin(ControlMode::MANUAL);
  r.in.level = level(1);
  r.passBoot();
  TEST_ASSERT_EQUAL(MotorState::MANUAL_IDLE, r.m.state());
  TEST_ASSERT_FALSE(r.m.motorOutput());
}

static void test_level_below_25_starts_auto() {
  Rig r;
  r.begin();
  r.in.level = level(0);  // below 25 %
  r.passBoot();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(StartReason::AUTO_LOW_LEVEL, r.m.lastStartReason());
}

static void test_level_at_25_does_not_start() {
  // 25 % probe wet means water is at/above 25 %; start happens when it goes dry.
  Rig r;
  r.begin();
  r.in.level = level(1);
  r.passBoot();
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(MotorState::AUTO_IDLE, r.m.state());
}

static void test_between_thresholds_keeps_state_hysteresis() {
  Rig r;
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  r.in.level = level(2);  // 50 %: still filling
  r.run(5000);
  TEST_ASSERT_TRUE(r.m.motorOutput());
  r.in.level = level(4);  // 95 %: stop
  r.tick();
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(StopReason::LEVEL_REACHED, r.m.lastStopReason());
  // Falling back to 50 % must not restart (hysteresis down to 25 %).
  r.in.level = level(2);
  r.run(r.cfg.minOffMs + 1000);
  TEST_ASSERT_FALSE(r.m.motorOutput());
  r.in.level = level(0);
  r.tick();
  TEST_ASSERT_TRUE(r.m.motorOutput());
}

static void test_level_at_95_stops_and_emits_tank_full() {
  Rig r;
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  r.drainEvents();
  r.in.level = level(4);
  r.tick();
  bool sawFull = false, sawStop = false;
  MachineEvent e;
  while (r.m.popEvent(e)) {
    if (e.type == MachineEventType::TANK_FULL) sawFull = true;
    if (e.type == MachineEventType::MOTOR_STOPPED) sawStop = true;
  }
  TEST_ASSERT_TRUE(sawFull);
  TEST_ASSERT_TRUE(sawStop);
}

static void test_min_off_time_prevents_rapid_restart() {
  Rig r;
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  CommandOutcome o = r.send(CommandAction::MOTOR_OFF);
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, o.status);
  TEST_ASSERT_FALSE(r.m.motorOutput());
  // Switch to MANUAL and try to start immediately.
  r.send(CommandAction::SET_MODE, CommandSource::CLOUD, true, ControlMode::MANUAL);
  o = r.send(CommandAction::MOTOR_ON);
  TEST_ASSERT_EQUAL(CommandStatus::REJECTED, o.status);
  TEST_ASSERT_EQUAL(RejectReason::MIN_OFF_TIME, o.reason);
  TEST_ASSERT_TRUE(o.retryAfterMs > 0 && o.retryAfterMs <= r.cfg.minOffMs);
  r.run(r.cfg.minOffMs);
  o = r.send(CommandAction::MOTOR_ON);
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, o.status);
  TEST_ASSERT_TRUE(r.m.motorOutput());
}

static void test_user_off_in_auto_inhibits_until_resume_timeout() {
  Rig r;
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  r.send(CommandAction::MOTOR_OFF);
  r.run(r.cfg.minOffMs + 5000);
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_TRUE(r.m.userInhibit());
  r.run(r.cfg.autoResumeAfterUserOffMs, 1000);
  TEST_ASSERT_TRUE(r.m.motorOutput());
}

static void test_manual_on_rejected_when_tank_full() {
  Rig r;
  r.begin(ControlMode::MANUAL);
  r.in.level = level(4);
  r.passBoot();
  CommandOutcome o = r.send(CommandAction::MOTOR_ON);
  TEST_ASSERT_EQUAL(RejectReason::TANK_FULL, o.reason);
  TEST_ASSERT_FALSE(r.m.motorOutput());
}

static void test_manual_run_stops_at_off_threshold() {
  Rig r;
  r.begin(ControlMode::MANUAL);
  r.in.level = level(2);
  r.passBoot();
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::MOTOR_ON).status);
  r.tick();
  TEST_ASSERT_EQUAL(MotorState::MANUAL_RUNNING, r.m.state());
  r.in.level = level(4);
  r.tick();
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(MotorState::MANUAL_IDLE, r.m.state());
}

static void test_manual_on_during_overflow_rejected() {
  Rig r;
  r.begin(ControlMode::MANUAL);
  r.in.level = level(2);
  r.passBoot();
  r.in.hhTripped = true;
  r.tick();
  TEST_ASSERT_EQUAL(MotorState::OVERFLOW_LOCKOUT, r.m.state());
  CommandOutcome o = r.send(CommandAction::MOTOR_ON);
  TEST_ASSERT_EQUAL(CommandStatus::REJECTED, o.status);
  TEST_ASSERT_EQUAL(RejectReason::OVERFLOW_ACTIVE, o.reason);
  // Reset refused while HH still wet.
  TEST_ASSERT_EQUAL(RejectReason::CONDITION_NOT_CLEARED, r.send(CommandAction::RESET_FAULT).reason);
  r.in.hhTripped = false;
  r.tick();
  TEST_ASSERT_EQUAL(MotorState::OVERFLOW_LOCKOUT, r.m.state());  // latched
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::RESET_FAULT).status);
  TEST_ASSERT_EQUAL(MotorState::MANUAL_IDLE, r.m.state());
}

static void test_repeated_on_off_commands_are_idempotent() {
  Rig r;
  r.begin(ControlMode::MANUAL);
  r.in.level = level(1);
  r.passBoot();
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::MOTOR_ON).status);
  CommandOutcome o = r.send(CommandAction::MOTOR_ON);
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, o.status);
  TEST_ASSERT_EQUAL(RejectReason::ALREADY_IN_STATE, o.reason);
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::MOTOR_OFF).status);
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::MOTOR_OFF).status);
  TEST_ASSERT_FALSE(r.m.motorOutput());
  // Hammering ON keeps getting MIN_OFF_TIME, never short cycles.
  for (int i = 0; i < 20; ++i) {
    TEST_ASSERT_EQUAL(RejectReason::MIN_OFF_TIME, r.send(CommandAction::MOTOR_ON).reason);
    r.tick(1000);
  }
}

static void test_local_switch_off_stops_and_blocks_remote() {
  Rig r;
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  r.in.localSwitch = LocalSwitch::OFF;
  r.tick();
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(ControlMode::OFF, r.m.effectiveMode());
  r.run(r.cfg.minOffMs + 1000);
  TEST_ASSERT_EQUAL(RejectReason::LOCAL_SWITCH_OFF, r.send(CommandAction::MOTOR_ON).reason);
  TEST_ASSERT_EQUAL(RejectReason::LOCAL_SWITCH_OFF,
                    r.send(CommandAction::SET_MODE, CommandSource::CLOUD, true, ControlMode::MANUAL).reason);
}

static void test_unreadable_panel_fails_safe_to_off() {
  Rig r;
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  r.in.localSwitch = LocalSwitch::UNKNOWN;
  r.tick();
  TEST_ASSERT_FALSE(r.m.motorOutput());
}

static void test_local_manual_switch_blocks_remote_on_allows_panel() {
  Rig r;
  r.begin();
  r.in.level = level(2);
  r.in.localSwitch = LocalSwitch::MANUAL;
  r.passBoot();
  TEST_ASSERT_EQUAL(ControlMode::MANUAL, r.m.effectiveMode());
  TEST_ASSERT_EQUAL(RejectReason::LOCAL_SWITCH_MANUAL, r.send(CommandAction::MOTOR_ON).reason);
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::MOTOR_ON, CommandSource::LOCAL_PANEL).status);
  TEST_ASSERT_TRUE(r.m.motorOutput());
  // Remote OFF is always accepted.
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::MOTOR_OFF).status);
  TEST_ASSERT_FALSE(r.m.motorOutput());
}

static void test_manual_to_auto_handover_continues_fill() {
  Rig r;
  r.begin(ControlMode::MANUAL);
  r.in.level = level(2);
  r.passBoot();
  r.send(CommandAction::MOTOR_ON);
  r.drainEvents();
  r.send(CommandAction::SET_MODE, CommandSource::CLOUD, true, ControlMode::AUTO);
  r.tick();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(MotorState::AUTO_FILLING, r.m.state());
  MachineEvent e;
  while (r.m.popEvent(e)) TEST_ASSERT_NOT_EQUAL(MachineEventType::MOTOR_STOPPED, e.type);
}

static void test_auto_to_manual_stops_motor() {
  Rig r;
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  r.send(CommandAction::SET_MODE, CommandSource::CLOUD, true, ControlMode::MANUAL);
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(MotorState::MANUAL_IDLE, r.m.state());
  TEST_ASSERT_EQUAL(StopReason::MODE_CHANGE, r.m.lastStopReason());
}

static void test_schedule_window_wait_and_resume() {
  Rig r;
  r.cfg.scheduleRestrictEnabled = true;
  r.begin();
  r.in.level = level(0);
  r.in.scheduleWindowOpen = false;
  r.passBoot();
  TEST_ASSERT_EQUAL(MotorState::SCHEDULE_WAIT, r.m.state());
  TEST_ASSERT_FALSE(r.m.motorOutput());
  r.in.scheduleWindowOpen = true;
  r.tick();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  // Window closes after min ON: stop and wait, fill demand retained.
  r.run(r.cfg.minOnMs + 100);
  r.in.level = level(2);
  r.in.scheduleWindowOpen = false;
  r.tick();
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(MotorState::SCHEDULE_WAIT, r.m.state());
  r.run(r.cfg.minOffMs + 100);
  r.in.scheduleWindowOpen = true;
  r.tick();
  TEST_ASSERT_TRUE(r.m.motorOutput());  // resumes until 95 %
}

static void test_scheduled_fill_tops_up() {
  Rig r;
  r.begin();
  r.in.level = level(2);
  r.passBoot();
  TEST_ASSERT_FALSE(r.m.motorOutput());
  r.in.scheduleFillDue = true;
  r.tick();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(StartReason::AUTO_SCHEDULED_FILL, r.m.lastStartReason());
}

static void test_ota_requires_motor_stop_then_maintenance() {
  Rig r;
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  r.feedbackFollows = false;
  r.in.contactorClosed = true;
  CommandOutcome o = r.send(CommandAction::ENTER_OTA);
  TEST_ASSERT_EQUAL(RejectReason::MOTOR_NOT_STOPPED, o.reason);
  TEST_ASSERT_FALSE(r.m.motorOutput());
  r.in.contactorClosed = false;  // contactor released
  r.tick();
  o = r.send(CommandAction::ENTER_OTA);
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, o.status);
  TEST_ASSERT_EQUAL(MotorState::OTA_MAINTENANCE, r.m.state());
  r.feedbackFollows = true;
  r.run(5000);
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(RejectReason::OTA_IN_PROGRESS, r.send(CommandAction::MOTOR_ON).reason);
  // Abort: back through self test before AUTO resumes.
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::EXIT_OTA).status);
  TEST_ASSERT_EQUAL(MotorState::BOOT_SELF_TEST, r.m.state());
  r.passBoot();
  r.run(r.cfg.minOffMs);  // the OTA stop still counts towards minimum OFF time
  TEST_ASSERT_TRUE(r.m.motorOutput());
}

static void test_restored_latch_after_reboot() {
  Rig r;
  r.begin(ControlMode::AUTO, FaultCode::MOTOR_OVERLOAD);
  r.in.level = level(0);
  r.passBoot();
  TEST_ASSERT_EQUAL(MotorState::FAULT_LOCKOUT, r.m.state());
  TEST_ASSERT_FALSE(r.m.motorOutput());
  TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::RESET_FAULT).status);
  r.tick();
  TEST_ASSERT_TRUE(r.m.motorOutput());
}

static void test_start_rate_limit() {
  Rig r;
  r.cfg.maxStartsPerHour = 2;
  r.cfg.minOffMs = 30000;
  r.begin(ControlMode::MANUAL);
  r.in.level = level(1);
  r.passBoot();
  for (int i = 0; i < 2; ++i) {
    TEST_ASSERT_EQUAL(CommandStatus::ACCEPTED, r.send(CommandAction::MOTOR_ON).status);
    r.tick();
    r.send(CommandAction::MOTOR_OFF);
    r.run(r.cfg.minOffMs + 100, 1000);
  }
  CommandOutcome o = r.send(CommandAction::MOTOR_ON);
  TEST_ASSERT_EQUAL(RejectReason::START_RATE_LIMIT, o.reason);
  TEST_ASSERT_TRUE(o.retryAfterMs > 0);
}

static void test_millis_wraparound() {
  Rig r;
  r.now = 0xFFFFFFFFUL - 10000;  // wraps during the boot delay
  r.begin();
  r.in.level = level(0);
  r.passBoot();
  TEST_ASSERT_TRUE(r.m.motorOutput());
  r.in.level = level(4);
  r.tick();
  TEST_ASSERT_FALSE(r.m.motorOutput());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_starts_with_motor_off_and_waits_restart_delay);
  RUN_TEST(test_manual_mode_restored_but_motor_not_restarted);
  RUN_TEST(test_level_below_25_starts_auto);
  RUN_TEST(test_level_at_25_does_not_start);
  RUN_TEST(test_between_thresholds_keeps_state_hysteresis);
  RUN_TEST(test_level_at_95_stops_and_emits_tank_full);
  RUN_TEST(test_min_off_time_prevents_rapid_restart);
  RUN_TEST(test_user_off_in_auto_inhibits_until_resume_timeout);
  RUN_TEST(test_manual_on_rejected_when_tank_full);
  RUN_TEST(test_manual_run_stops_at_off_threshold);
  RUN_TEST(test_manual_on_during_overflow_rejected);
  RUN_TEST(test_repeated_on_off_commands_are_idempotent);
  RUN_TEST(test_local_switch_off_stops_and_blocks_remote);
  RUN_TEST(test_unreadable_panel_fails_safe_to_off);
  RUN_TEST(test_local_manual_switch_blocks_remote_on_allows_panel);
  RUN_TEST(test_manual_to_auto_handover_continues_fill);
  RUN_TEST(test_auto_to_manual_stops_motor);
  RUN_TEST(test_schedule_window_wait_and_resume);
  RUN_TEST(test_scheduled_fill_tops_up);
  RUN_TEST(test_ota_requires_motor_stop_then_maintenance);
  RUN_TEST(test_restored_latch_after_reboot);
  RUN_TEST(test_start_rate_limit);
  RUN_TEST(test_millis_wraparound);
  return UNITY_END();
}
