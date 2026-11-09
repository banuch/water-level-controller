#include "app/SharedState.h"

#include <string.h>

namespace wintek {

namespace {
struct CommandRequest {
  Command cmd;
  TaskHandle_t replyTo;
};

SemaphoreHandle_t g_mutex = nullptr;
StaticSemaphore_t g_mutexBuf;
QueueHandle_t g_cmdQueue = nullptr;
QueueHandle_t g_alertQueue = nullptr;
DeviceSnapshot g_snapshot;
DeviceConfig g_config;
volatile uint32_t g_configVersion = 0;
volatile uint8_t g_sysReq = 0;
volatile bool g_wifiUp = false, g_cloudUp = false, g_prov = false, g_pairing = false;
volatile bool g_timeValid = false;

// Outcome packed into a 32-bit task notification value.
uint32_t pack(const CommandOutcome& o) {
  uint32_t retryS = (o.retryAfterMs + 999) / 1000;
  if (retryS > 0xFFFF) retryS = 0xFFFF;
  return (static_cast<uint32_t>(o.status) << 24) | (static_cast<uint32_t>(o.reason) << 16) | retryS;
}
CommandOutcome unpack(uint32_t v) {
  CommandOutcome o;
  o.status = static_cast<CommandStatus>((v >> 24) & 0xFF);
  o.reason = static_cast<RejectReason>((v >> 16) & 0xFF);
  o.retryAfterMs = (v & 0xFFFF) * 1000UL;
  return o;
}

struct Lock {
  Lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mutex); }
};
}  // namespace

void SharedState::begin() {
  if (g_mutex != nullptr) return;
  g_mutex = xSemaphoreCreateMutexStatic(&g_mutexBuf);
  g_cmdQueue = xQueueCreate(8, sizeof(CommandRequest));
  g_alertQueue = xQueueCreate(32, sizeof(Alert));
}

void SharedState::publish(const DeviceSnapshot& s) {
  Lock l;
  g_snapshot = s;
}

DeviceSnapshot SharedState::snapshot() {
  Lock l;
  return g_snapshot;
}

void SharedState::setConfig(const DeviceConfig& c) {
  Lock l;
  g_config = c;
  g_configVersion = g_configVersion + 1;
}

DeviceConfig SharedState::config() {
  Lock l;
  return g_config;
}

uint32_t SharedState::configVersion() { return g_configVersion; }

CommandOutcome SharedState::submit(const Command& cmd, uint32_t timeoutMs) {
  CommandRequest r{cmd, xTaskGetCurrentTaskHandle()};
  xTaskNotifyStateClear(nullptr);
  CommandOutcome timeout;
  timeout.status = CommandStatus::TIMEOUT;
  if (xQueueSend(g_cmdQueue, &r, pdMS_TO_TICKS(100)) != pdTRUE) return timeout;
  uint32_t v = 0;
  if (xTaskNotifyWait(0xFFFFFFFFUL, 0xFFFFFFFFUL, &v, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return timeout;
  return unpack(v);
}

bool SharedState::nextCommand(Command& cmd, TaskHandle_t& replyTo) {
  CommandRequest r;
  if (xQueueReceive(g_cmdQueue, &r, 0) != pdTRUE) return false;
  cmd = r.cmd;
  replyTo = r.replyTo;
  return true;
}

void SharedState::reply(TaskHandle_t replyTo, const CommandOutcome& o) {
  if (replyTo != nullptr) xTaskNotify(replyTo, pack(o), eSetValueWithOverwrite);
}

void SharedState::pushAlert(const Alert& a) {
  if (xQueueSend(g_alertQueue, &a, 0) != pdTRUE) {
    Alert dropped;
    xQueueReceive(g_alertQueue, &dropped, 0);  // keep the newest
    xQueueSend(g_alertQueue, &a, 0);
  }
}

bool SharedState::popAlert(Alert& a, TickType_t wait) { return xQueueReceive(g_alertQueue, &a, wait) == pdTRUE; }

void SharedState::raiseSystemRequest(SystemRequest r) { g_sysReq = static_cast<uint8_t>(r); }

SystemRequest SharedState::takeSystemRequest() {
  const uint8_t v = g_sysReq;
  g_sysReq = 0;
  return static_cast<SystemRequest>(v);
}

void SharedState::setConnectivity(bool wifiUp, bool cloudUp, bool provisioning, bool pairingOpen) {
  g_wifiUp = wifiUp;
  g_cloudUp = cloudUp;
  g_prov = provisioning;
  g_pairing = pairingOpen;
}

void SharedState::connectivity(bool& wifiUp, bool& cloudUp, bool& provisioning, bool& pairingOpen) {
  wifiUp = g_wifiUp;
  cloudUp = g_cloudUp;
  provisioning = g_prov;
  pairingOpen = g_pairing;
}

void SharedState::setTimeValid(bool v) { g_timeValid = v; }
bool SharedState::timeValid() { return g_timeValid; }

}  // namespace wintek
