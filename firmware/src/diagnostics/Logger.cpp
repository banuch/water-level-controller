#include "diagnostics/Logger.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

namespace wintek {

namespace {
SemaphoreHandle_t g_mutex = nullptr;
StaticSemaphore_t g_mutexBuf;
LogLevel g_min = LogLevel::INFO;
char g_ring[Logger::kLines][Logger::kLineLen];
size_t g_head = 0;
size_t g_count = 0;

const char* levelChar(LogLevel l) {
  switch (l) {
    case LogLevel::DEBUG: return "D";
    case LogLevel::INFO: return "I";
    case LogLevel::WARN: return "W";
    case LogLevel::ERROR: return "E";
  }
  return "?";
}
}  // namespace

void Logger::begin(LogLevel minLevel) {
  g_min = minLevel;
  if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutexStatic(&g_mutexBuf);
}

void Logger::log(LogLevel lvl, const char* tag, const char* fmt, ...) {
  if (static_cast<uint8_t>(lvl) < static_cast<uint8_t>(g_min)) return;
  char line[kLineLen];
  const int n = snprintf(line, sizeof(line), "%lu %s/%s: ", static_cast<unsigned long>(millis()), levelChar(lvl), tag);
  if (n > 0 && static_cast<size_t>(n) < sizeof(line)) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - static_cast<size_t>(n), fmt, ap);
    va_end(ap);
  }
  if (g_mutex != nullptr) xSemaphoreTake(g_mutex, portMAX_DELAY);
  Serial.println(line);
  memcpy(g_ring[g_head], line, sizeof(line));
  g_head = (g_head + 1) % kLines;
  if (g_count < kLines) ++g_count;
  if (g_mutex != nullptr) xSemaphoreGive(g_mutex);
}

size_t Logger::dump(char* out, size_t cap) {
  if (cap == 0) return 0;
  size_t w = 0;
  if (g_mutex != nullptr) xSemaphoreTake(g_mutex, portMAX_DELAY);
  const size_t start = (g_head + kLines - g_count) % kLines;
  for (size_t i = 0; i < g_count; ++i) {
    const char* l = g_ring[(start + i) % kLines];
    const size_t len = strnlen(l, kLineLen);
    if (w + len + 2 > cap) break;
    memcpy(out + w, l, len);
    w += len;
    out[w++] = '\n';
  }
  out[w] = '\0';
  if (g_mutex != nullptr) xSemaphoreGive(g_mutex);
  return w;
}

}  // namespace wintek
