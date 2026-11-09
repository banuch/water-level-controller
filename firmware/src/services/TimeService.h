// Wall-clock time via SNTP. ESP32-C3 has no battery-backed RTC: after a
// power cut the time is invalid until the first successful sync.
#pragma once

#include <stdint.h>

#include "services/ScheduleManager.h"

namespace wintek {

class TimeService {
 public:
  static void begin(const char* posixTz);
  static void setTimezone(const char* posixTz);
  static bool valid();          // clock set to a plausible date
  static int64_t nowEpochS();   // UTC seconds (0 if invalid)
  static LocalTime localTime(); // for the scheduler
};

}  // namespace wintek
