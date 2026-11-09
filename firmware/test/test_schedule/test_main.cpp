// Schedule evaluation tests.
#include <unity.h>

#include "services/ScheduleManager.h"

using namespace wintek;

void setUp() {}
void tearDown() {}

static LocalTime at(uint8_t weekday, uint16_t hh, uint16_t mm, uint32_t day = 100) {
  LocalTime t;
  t.valid = true;
  t.weekday = weekday;
  t.minuteOfDay = static_cast<uint16_t>(hh * 60 + mm);
  t.dayNumber = day;
  return t;
}

static void test_no_restriction_always_open() {
  DeviceConfig c;
  ScheduleManager s;
  s.configure(c);
  TEST_ASSERT_TRUE(s.evaluate(at(1, 3, 0)).windowOpen);
}

static void test_window_restriction() {
  DeviceConfig c;
  c.scheduleRestrictEnabled = true;
  c.schedules[0].enabled = true;
  c.schedules[0].type = ScheduleType::WINDOW;
  c.schedules[0].daysMask = 0x3E;  // Mon-Fri
  c.schedules[0].startMinute = 6 * 60;
  c.schedules[0].endMinute = 9 * 60;
  ScheduleManager s;
  s.configure(c);
  TEST_ASSERT_TRUE(s.evaluate(at(1, 6, 0)).windowOpen);
  TEST_ASSERT_TRUE(s.evaluate(at(5, 8, 59)).windowOpen);
  TEST_ASSERT_FALSE(s.evaluate(at(1, 9, 0)).windowOpen);
  TEST_ASSERT_FALSE(s.evaluate(at(0, 7, 0)).windowOpen);  // Sunday
}

static void test_window_wrapping_midnight() {
  DeviceConfig c;
  c.scheduleRestrictEnabled = true;
  c.schedules[0].enabled = true;
  c.schedules[0].daysMask = 0x01;  // Sunday night 22:00 -> Monday 05:00
  c.schedules[0].startMinute = 22 * 60;
  c.schedules[0].endMinute = 5 * 60;
  ScheduleManager s;
  s.configure(c);
  TEST_ASSERT_TRUE(s.evaluate(at(0, 23, 0)).windowOpen);
  TEST_ASSERT_TRUE(s.evaluate(at(1, 4, 59)).windowOpen);
  TEST_ASSERT_FALSE(s.evaluate(at(1, 23, 0)).windowOpen);
  TEST_ASSERT_FALSE(s.evaluate(at(0, 4, 0)).windowOpen);  // Sunday early = Saturday's night
}

static void test_unknown_time_fail_open_and_closed() {
  DeviceConfig c;
  c.scheduleRestrictEnabled = true;
  ScheduleManager s;
  s.configure(c);
  LocalTime unknown;
  TEST_ASSERT_TRUE(s.evaluate(unknown).windowOpen);
  c.scheduleFailOpen = false;
  s.configure(c);
  TEST_ASSERT_FALSE(s.evaluate(unknown).windowOpen);
  TEST_ASSERT_FALSE(s.evaluate(unknown).fillDue);
}

static void test_fill_fires_once_per_day() {
  DeviceConfig c;
  c.schedules[0].enabled = true;
  c.schedules[0].type = ScheduleType::FILL;
  c.schedules[0].startMinute = 5 * 60 + 30;
  ScheduleManager s;
  s.configure(c);
  TEST_ASSERT_FALSE(s.evaluate(at(2, 5, 29)).fillDue);
  TEST_ASSERT_TRUE(s.evaluate(at(2, 5, 30)).fillDue);
  TEST_ASSERT_FALSE(s.evaluate(at(2, 5, 30)).fillDue);
  TEST_ASSERT_FALSE(s.evaluate(at(2, 5, 31)).fillDue);
  TEST_ASSERT_TRUE(s.evaluate(at(3, 5, 31, 101)).fillDue);  // next day, 1 min late
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_restriction_always_open);
  RUN_TEST(test_window_restriction);
  RUN_TEST(test_window_wrapping_midnight);
  RUN_TEST(test_unknown_time_fail_open_and_closed);
  RUN_TEST(test_fill_fires_once_per_day);
  return UNITY_END();
}
