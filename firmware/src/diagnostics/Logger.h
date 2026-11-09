// Thread-safe logger: USB serial console + RAM ring buffer readable via the
// local API (/api/v1/logs) for field diagnostics.
#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

namespace wintek {

enum class LogLevel : uint8_t { DEBUG = 0, INFO, WARN, ERROR };

class Logger {
 public:
  static constexpr size_t kLines = 48;
  static constexpr size_t kLineLen = 112;

  static void begin(LogLevel minLevel);
  static void log(LogLevel lvl, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  // Copies lines (oldest first) into `out` separated by '\n'. Returns bytes written.
  static size_t dump(char* out, size_t cap);
};

}  // namespace wintek

#define LOGD(tag, ...) ::wintek::Logger::log(::wintek::LogLevel::DEBUG, tag, __VA_ARGS__)
#define LOGI(tag, ...) ::wintek::Logger::log(::wintek::LogLevel::INFO, tag, __VA_ARGS__)
#define LOGW(tag, ...) ::wintek::Logger::log(::wintek::LogLevel::WARN, tag, __VA_ARGS__)
#define LOGE(tag, ...) ::wintek::Logger::log(::wintek::LogLevel::ERROR, tag, __VA_ARGS__)
