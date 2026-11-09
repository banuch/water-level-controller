#include "services/TimeService.h"

#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

namespace wintek {

namespace {
constexpr time_t kMinValidEpoch = 1767225600;  // 2026-01-01T00:00:00Z
}

void TimeService::begin(const char* tz) {
  // configTzTime starts SNTP (lwIP) in the background; safe before Wi-Fi is up.
  configTzTime(tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void TimeService::setTimezone(const char* tz) {
  setenv("TZ", tz, 1);
  tzset();
}

bool TimeService::valid() { return time(nullptr) >= kMinValidEpoch; }

int64_t TimeService::nowEpochS() {
  const time_t t = time(nullptr);
  return t >= kMinValidEpoch ? static_cast<int64_t>(t) : 0;
}

LocalTime TimeService::localTime() {
  LocalTime lt;
  const time_t t = time(nullptr);
  if (t < kMinValidEpoch) return lt;
  struct tm tmv;
  localtime_r(&t, &tmv);
  lt.valid = true;
  lt.weekday = static_cast<uint8_t>(tmv.tm_wday);
  lt.minuteOfDay = static_cast<uint16_t>(tmv.tm_hour * 60 + tmv.tm_min);
  // Unique, increasing key per local calendar day (gaps are irrelevant).
  lt.dayNumber = static_cast<uint32_t>(tmv.tm_year) * 366U + static_cast<uint32_t>(tmv.tm_yday);
  return lt;
}

}  // namespace wintek
