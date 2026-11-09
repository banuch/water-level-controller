// Thread-safe exchange between the control task (motor authority) and the
// network/OTA tasks. Network code never touches GPIO or the state machine
// directly: it reads snapshots and submits commands through this bridge.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "core/Types.h"
#include "sensors/ProbeSensor.h"
#include "services/AlertManager.h"
#include "storage/Config.h"

namespace wintek {

struct DeviceSnapshot {
  uint32_t seq = 0;          // increments whenever a reportable field changes
  uint32_t uptimeMs = 0;
  MotorState state = MotorState::BOOT_SELF_TEST;
  ControlMode mode = ControlMode::AUTO;
  ControlMode softwareMode = ControlMode::AUTO;
  LocalSwitch localSwitch = LocalSwitch::UNKNOWN;
  bool motorCommanded = false;
  bool contactorClosed = false;
  bool overloadTripped = false;
  bool hhTripped = false;
  bool estopActive = false;
  bool panelOk = false;
  LevelReading level;
  ProbeState probeState[kMaxProbes] = {};
  ProbeSample probeSample[kMaxProbes];
  bool unusedProbeWet = false;
  FaultCode activeFault = FaultCode::NONE;
  FaultCode latchedFault = FaultCode::NONE;
  StartReason lastStart = StartReason::NONE;
  StopReason lastStop = StopReason::NONE;
  uint32_t currentRunMs = 0;
  uint32_t startBlockedMs = 0;
  bool fillDemand = false;
  bool userInhibit = false;
  bool scheduleWindowOpen = true;
  uint32_t totalRuntimeS = 0;
  uint32_t totalStarts = 0;
};

// Requests raised by the local service button (handled by the network task).
enum class SystemRequest : uint8_t { NONE = 0, OPEN_PAIRING, START_PROVISIONING, FACTORY_RESET };

class SharedState {
 public:
  static void begin();

  // --- snapshot (control -> others)
  static void publish(const DeviceSnapshot& s);
  static DeviceSnapshot snapshot();

  // --- configuration (single source of truth in RAM)
  static void setConfig(const DeviceConfig& c);  // bumps version
  static DeviceConfig config();
  static uint32_t configVersion();

  // --- commands (network/OTA -> control). Blocks the caller up to timeoutMs.
  static CommandOutcome submit(const Command& cmd, uint32_t timeoutMs = 2000);
  // Called by the control task: returns true when a command was dequeued.
  static bool nextCommand(Command& cmd, TaskHandle_t& replyTo);
  static void reply(TaskHandle_t replyTo, const CommandOutcome& o);

  // --- alerts (control -> network)
  static void pushAlert(const Alert& a);
  static bool popAlert(Alert& a, TickType_t wait = 0);

  // --- system requests (control -> network)
  static void raiseSystemRequest(SystemRequest r);
  static SystemRequest takeSystemRequest();

  // --- connectivity status (network -> control, for LEDs)
  static void setConnectivity(bool wifiUp, bool cloudUp, bool provisioning, bool pairingOpen);
  static void connectivity(bool& wifiUp, bool& cloudUp, bool& provisioning, bool& pairingOpen);

  // --- time validity for the scheduler (network -> control)
  static void setTimeValid(bool v);
  static bool timeValid();
};

}  // namespace wintek
