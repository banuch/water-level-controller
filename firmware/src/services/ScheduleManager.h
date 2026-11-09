// Schedule evaluation (hardware independent). Runs on the device so
// schedules keep working without Wi-Fi or cloud, as long as local time is
// known. ESP32-C3 has no battery-backed RTC, so after a power cut the time
// is unknown until SNTP succeeds; `scheduleFailOpen` decides what happens
// then (default: permit AUTO filling — water availability first, safety
// interlocks are unaffected by schedules).
#pragma once

#include "storage/Config.h"

namespace wintek {

struct LocalTime {
  bool valid = false;
  uint8_t weekday = 0;       // 0 = Sunday
  uint16_t minuteOfDay = 0;  // 0..1439
  uint32_t dayNumber = 0;    // monotonically increasing local day counter
};

struct ScheduleResult {
  bool windowOpen = true;  // AUTO filling permitted now
  bool fillDue = false;    // a FILL entry fired (one-shot)
  int8_t entryIndex = -1;  // entry responsible for fillDue / open window
};

class ScheduleManager {
 public:
  void configure(const DeviceConfig& cfg);
  ScheduleResult evaluate(const LocalTime& t);
  static bool windowCovers(const ScheduleEntry& e, const LocalTime& t);

 private:
  bool restrict_ = false;
  bool failOpen_ = true;
  ScheduleEntry entries_[kMaxSchedules];
  uint32_t lastFiredKey_[kMaxSchedules] = {};
};

}  // namespace wintek
