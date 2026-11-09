// Wintek Smart Water Tank Controller — ESP32-C3 firmware entry point.
//
// Task layout (single-core RISC-V, FreeRTOS):
//   control  (prio 5, 20 ms)  sensors -> safety -> state machine -> contactor
//   network  (prio 3)         Wi-Fi, MQTT/TLS, local API, alerts, OTA orchestration
//   mqtt     (prio 4)         esp-mqtt client internals
//   ota      (prio 2)         only while an update is running
// Motor control never waits for the network; all exchange is via SharedState.
#include <Arduino.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "app/ControlTask.h"
#include "app/NetworkTask.h"
#include "app/SharedState.h"
#include "app_config.h"
#include "diagnostics/Logger.h"
#include "hal/BoardIo.h"
#include "services/OtaManager.h"
#include "storage/ConfigStore.h"

using namespace wintek;

namespace {
const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "POWER_ON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}
}  // namespace

void setup() {
  // 1) Contactor output OFF before anything else can run.
  BoardIo::earlySafeInit();

  Serial.begin(115200);
  Logger::begin(LogLevel::INFO);

  // 2) Task watchdog: a hung control or network task resets the chip, and a
  //    reset always de-energises the contactor (pull-down on the drive line).
  esp_task_wdt_init(app::kWatchdogTimeoutS, true);

  const esp_reset_reason_t rr = esp_reset_reason();
  LOGI("boot", "Wintek %s fw %s hw %s, reset=%s", app::kModel, app::kFirmwareVersion, app::kHardwareRevision,
       resetReasonName(rr));

  // 3) Storage and OTA image state.
  ConfigStore::begin();
  OtaManager::bootCheck();

  DeviceConfig cfg;
  ConfigStore::loadConfig(cfg);
  SharedState::begin();
  SharedState::setConfig(cfg);

  PersistentState ps;
  ConfigStore::loadState(ps);
  const uint32_t boots = ConfigStore::incrementBootCount();

  FactoryIdentity id;
  ConfigStore::loadFactory(id);

  char bootId[12];
  snprintf(bootId, sizeof(bootId), "%08lx", static_cast<unsigned long>(esp_random()));
  LOGI("boot", "device=%s boot#%lu bootId=%s latched=%s mode=%s", id.deviceId, static_cast<unsigned long>(boots),
       bootId, toString(ps.latchedFault), toString(ps.mode));

  // 4) Start control first (motor authority), then networking.
  ControlBootInfo info;
  info.restoredLatch = ps.latchedFault;
  info.restoredMode = ps.mode;  // the motor itself is never restored to ON
  info.totalRuntimeS = ps.totalRuntimeS;
  info.totalStarts = ps.totalStarts;
  info.powerOnReset = (rr == ESP_RST_POWERON || rr == ESP_RST_BROWNOUT);
  ControlTask::start(info);
  NetworkTask::start(id, bootId);
}

void loop() {
  // All work happens in dedicated tasks.
  vTaskDelete(nullptr);
}
