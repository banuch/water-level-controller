#include "services/ScheduleManager.h"

namespace wintek {

void ScheduleManager::configure(const DeviceConfig& cfg) {
  restrict_ = cfg.scheduleRestrictEnabled;
  failOpen_ = cfg.scheduleFailOpen;
  for (uint8_t i = 0; i < kMaxSchedules; ++i) {
    // Reset the one-shot memory only for entries that actually changed.
    const ScheduleEntry& n = cfg.schedules[i];
    const ScheduleEntry& o = entries_[i];
    if (n.enabled != o.enabled || n.type != o.type || n.daysMask != o.daysMask ||
        n.startMinute != o.startMinute || n.endMinute != o.endMinute) {
      lastFiredKey_[i] = 0;
    }
    entries_[i] = n;
  }
}

bool ScheduleManager::windowCovers(const ScheduleEntry& e, const LocalTime& t) {
  if (!e.enabled || e.type != ScheduleType::WINDOW) return false;
  const uint8_t today = static_cast<uint8_t>(1U << (t.weekday % 7));
  const uint8_t yesterday = static_cast<uint8_t>(1U << ((t.weekday + 6) % 7));
  const uint16_t m = t.minuteOfDay;
  if (e.startMinute < e.endMinute) {
    return (e.daysMask & today) && m >= e.startMinute && m < e.endMinute;
  }
  // Window wraps past midnight: e.g. 22:00-06:00 belongs to the start day.
  if ((e.daysMask & today) && m >= e.startMinute) return true;
  if ((e.daysMask & yesterday) && m < e.endMinute) return true;
  return false;
}

ScheduleResult ScheduleManager::evaluate(const LocalTime& t) {
  ScheduleResult r;
  if (!t.valid) {
    r.windowOpen = !restrict_ || failOpen_;
    return r;
  }

  if (restrict_) {
    r.windowOpen = false;
    for (uint8_t i = 0; i < kMaxSchedules; ++i) {
      if (windowCovers(entries_[i], t)) {
        r.windowOpen = true;
        r.entryIndex = static_cast<int8_t>(i);
        break;
      }
    }
  }

  const uint8_t today = static_cast<uint8_t>(1U << (t.weekday % 7));
  for (uint8_t i = 0; i < kMaxSchedules; ++i) {
    const ScheduleEntry& e = entries_[i];
    if (!e.enabled || e.type != ScheduleType::FILL || !(e.daysMask & today)) continue;
    // Tolerate up to 2 minutes of lateness (e.g. a time step after SNTP sync).
    if (t.minuteOfDay < e.startMinute || t.minuteOfDay >= e.startMinute + 2) continue;
    const uint32_t key = t.dayNumber + 1;  // +1 so that 0 means "never fired"
    if (lastFiredKey_[i] == key) continue;
    lastFiredKey_[i] = key;
    r.fillDue = true;
    r.entryIndex = static_cast<int8_t>(i);
    break;
  }
  return r;
}

}  // namespace wintek
