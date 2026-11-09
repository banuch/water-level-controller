#include "app/ControlTask.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "app/SharedState.h"
#include "app_config.h"
#include "board_config.h"
#include "diagnostics/Logger.h"
#include "hal/BoardIo.h"
#include "hal/ProbeFrontEnd.h"
#include "motor/MotorController.h"
#include "motor/MotorStateMachine.h"
#include "sensors/LevelEstimator.h"
#include "sensors/ProbeSensor.h"
#include "services/AlertManager.h"
#include "services/ScheduleManager.h"
#include "services/TimeService.h"
#include "storage/ConfigStore.h"

namespace wintek {

namespace {

constexpr const char* TAG = "ctl";

volatile bool g_healthy = false;
ControlBootInfo g_boot;

struct ControlContext {
  BoardIo io;
  ProbeFrontEnd probesHw;
  ProbeArray probes;
  ProbeLayout layout;
  MotorStateMachine fsm;
  MotorController motor;
  ScheduleManager schedule;
  AlertManager alerts;
  DeviceConfig cfg;
  uint32_t cfgVersion = 0;

  uint32_t lastProbeMs = 0;
  uint32_t lastScheduleMs = 0;
  ScheduleResult sched;
  bool fillDuePending = false;

  PanelInputs panel;
  bool panelFaultReported = false;
  bool prevStart = false, prevStop = false;
  uint32_t stopDownSinceMs = 0;
  bool resetSent = false;

  bool svcPrev = false;
  uint32_t svcDownSinceMs = 0;
  bool factoryResetRaised = false;

  DeviceSnapshot last;
  uint32_t seq = 0;
  uint32_t startedMs = 0;
};

ControlContext* g_ctx = nullptr;

ProbeNetwork makeNetwork() {
  ProbeNetwork n;
  n.excitationMv = board::kProbeExcitationMv;
  n.rSeriesKohm = board::kProbeSeriesKohm;
  n.rBiasKohm = board::kProbeBiasKohm;
  n.rGndKohm = board::kProbeGndKohm;
  return n;
}

void applyConfig(ControlContext& c) {
  c.cfg = SharedState::config();
  c.cfgVersion = SharedState::configVersion();
  c.layout = makeProbeLayout(c.cfg);
  ProbeFilterConfig fc;
  fc.debounceMs = c.cfg.sensorDebounceMs;
  fc.faultConfirmMs = c.cfg.sensorFaultConfirmMs;
  c.probes.configure(c.cfg.probeCount, makeNetwork(), c.cfg.wetBelowKohm, c.cfg.dryAboveKohm, fc);
  c.fsm.configure(c.cfg);
  c.schedule.configure(c.cfg);
  TimeService::setTimezone(c.cfg.timezone);
}

void runLocalCommand(ControlContext& c, CommandAction a, const MachineInputs& in) {
  Command cmd;
  cmd.action = a;
  cmd.source = CommandSource::LOCAL_PANEL;
  const CommandOutcome o = c.fsm.handleCommand(cmd, in);
  LOGI(TAG, "panel %s -> %s/%s", toString(a), toString(o.status), toString(o.reason));
}

void handlePanelButtons(ControlContext& c, const MachineInputs& in, uint32_t now) {
  if (!c.panel.ok) return;
  if (c.panel.startPressed && !c.prevStart) runLocalCommand(c, CommandAction::MOTOR_ON, in);
  if (c.panel.stopPressed && !c.prevStop) {
    runLocalCommand(c, CommandAction::MOTOR_OFF, in);
    c.stopDownSinceMs = now;
    c.resetSent = false;
  }
  if (c.panel.stopPressed && !c.resetSent && hasElapsed(now, c.stopDownSinceMs, app::kResetHoldMs)) {
    c.resetSent = true;
    runLocalCommand(c, CommandAction::RESET_FAULT, in);
  }
  c.prevStart = c.panel.startPressed;
  c.prevStop = c.panel.stopPressed;
}

void handleServiceButton(ControlContext& c, uint32_t now) {
  const bool down = c.io.serviceButtonDown();
  if (down && !c.svcPrev) {
    c.svcDownSinceMs = now;
    c.factoryResetRaised = false;
  }
  if (down && !c.factoryResetRaised && hasElapsed(now, c.svcDownSinceMs, app::kServiceFactoryResetMs)) {
    c.factoryResetRaised = true;
    SharedState::raiseSystemRequest(SystemRequest::FACTORY_RESET);
  }
  if (!down && c.svcPrev && !c.factoryResetRaised) {
    const uint32_t held = elapsedMs(now, c.svcDownSinceMs);
    if (held >= app::kServiceProvisionMs && held < 2 * app::kServiceProvisionMs) {
      SharedState::raiseSystemRequest(SystemRequest::START_PROVISIONING);
    } else if (held >= app::kServicePairingMinMs && held < 3000) {
      SharedState::raiseSystemRequest(SystemRequest::OPEN_PAIRING);
    }
  }
  c.svcPrev = down;
}

void updateLeds(ControlContext& c, uint32_t now) {
  bool wifi, cloud, prov, pairing;
  SharedState::connectivity(wifi, cloud, prov, pairing);
  const bool slow = (now / 500) % 2;
  const bool fast = (now / 125) % 2;
  PanelOutputs o;
  o.ledWifi = pairing ? fast : prov ? slow : (wifi && cloud) ? true : wifi ? ((now / 1000) % 4 == 0) : false;
  o.ledMotor = c.cfg.contactorFeedbackInstalled ? c.io.contactorClosed() : c.fsm.motorOutput();
  switch (c.fsm.state()) {
    case MotorState::FAULT_LOCKOUT:
    case MotorState::OVERFLOW_LOCKOUT: o.ledFault = true; break;
    case MotorState::SENSOR_FAULT: o.ledFault = slow; break;
    case MotorState::OTA_MAINTENANCE: o.ledFault = fast; break;
    default: o.ledFault = c.cfg.localPanelInstalled && !c.panel.ok ? slow : false;
  }
  o.buzzer = c.fsm.state() == MotorState::OVERFLOW_LOCKOUT && ((now / 1000) % 30 == 0);
  c.io.writePanel(o);
}

void publishSnapshot(ControlContext& c, const MachineInputs& in, uint32_t now) {
  DeviceSnapshot s;
  s.uptimeMs = now;
  s.state = c.fsm.state();
  s.mode = c.fsm.effectiveMode();
  s.softwareMode = c.fsm.softwareMode();
  s.localSwitch = c.cfg.localPanelInstalled ? in.localSwitch : LocalSwitch::AUTO;
  s.motorCommanded = c.fsm.motorOutput();
  s.contactorClosed = in.contactorClosed;
  s.overloadTripped = in.overloadTripped;
  s.hhTripped = in.hhTripped;
  s.estopActive = in.estopActive;
  s.panelOk = c.panel.ok;
  s.level = in.level;
  for (uint8_t i = 0; i < kMaxProbes; ++i) {
    s.probeState[i] = c.probes.committed(i);
    s.probeSample[i] = c.probes.lastSample(i);
  }
  s.unusedProbeWet = c.probes.unusedChannelWet();
  s.activeFault = c.fsm.activeFault();
  s.latchedFault = c.fsm.latchedFault();
  s.lastStart = c.fsm.lastStartReason();
  s.lastStop = c.fsm.lastStopReason();
  s.currentRunMs = c.fsm.currentRunMs(now);
  s.startBlockedMs = c.fsm.startBlockedForMs(now);
  s.fillDemand = c.fsm.fillDemand();
  s.userInhibit = c.fsm.userInhibit();
  s.scheduleWindowOpen = in.scheduleWindowOpen;
  s.totalRuntimeS = c.motor.totalRuntimeS(now);
  s.totalStarts = c.motor.totalStarts();

  const DeviceSnapshot& p = c.last;
  const bool changed = s.state != p.state || s.mode != p.mode || s.softwareMode != p.softwareMode ||
                       s.localSwitch != p.localSwitch || s.motorCommanded != p.motorCommanded ||
                       s.contactorClosed != p.contactorClosed || s.overloadTripped != p.overloadTripped ||
                       s.hhTripped != p.hhTripped || s.estopActive != p.estopActive || s.panelOk != p.panelOk ||
                       s.level.valid != p.level.valid || s.level.wetCount != p.level.wetCount ||
                       s.activeFault != p.activeFault || s.fillDemand != p.fillDemand ||
                       s.userInhibit != p.userInhibit || s.scheduleWindowOpen != p.scheduleWindowOpen;
  if (changed) ++c.seq;
  s.seq = c.seq;
  c.last = s;
  SharedState::publish(s);
}

void controlLoop(void*) {
  ControlContext& c = *g_ctx;
  esp_task_wdt_add(nullptr);

  applyConfig(c);
  c.io.begin(c.cfg.localPanelInstalled);
  c.probesHw.begin();
  c.motor.begin(&c.io, g_boot.totalRuntimeS, g_boot.totalStarts);
  const uint32_t t0 = millis();
  c.startedMs = t0;
  c.fsm.begin(t0, g_boot.restoredMode, g_boot.restoredLatch);
  if (g_boot.restoredLatch != FaultCode::NONE) {
    LOGW(TAG, "restored latched fault %s", toString(g_boot.restoredLatch));
  }
  if (g_boot.powerOnReset) {
    Alert a;
    if (c.alerts.raise(AlertType::POWER_RESTORED, FaultCode::NONE, t0, 0, a)) SharedState::pushAlert(a);
  }

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(app::kControlPeriodMs));
    const uint32_t now = millis();

    if (SharedState::configVersion() != c.cfgVersion) {
      applyConfig(c);
      LOGI(TAG, "configuration applied (v%lu)", static_cast<unsigned long>(c.cfgVersion));
    }

    // ---- Inputs --------------------------------------------------------
    c.io.pollDirectInputs();
    const bool panelWasOk = c.panel.ok;
    if (c.cfg.localPanelInstalled) {
      c.io.pollPanel(c.panel);
    } else {
      c.panel = PanelInputs();
    }
    if (c.cfg.localPanelInstalled && !c.panel.ok && (panelWasOk || !c.panelFaultReported)) {
      Alert a;
      if (c.alerts.raise(AlertType::LOCAL_PANEL_FAULT, FaultCode::LOCAL_PANEL_FAULT, now, 0, a)) {
        SharedState::pushAlert(a);
      }
      c.panelFaultReported = true;
      LOGE(TAG, "front panel unreadable: forcing OFF");
    }
    if (c.panel.ok) c.panelFaultReported = false;

    if (hasElapsed(now, c.lastProbeMs, app::kProbePeriodMs)) {
      c.lastProbeMs = now;
      float vA[kMaxProbes], vB[kMaxProbes];
      c.probesHw.acquire(c.cfg.probeSettleUs, vA, vB);
      c.probes.update(vA, vB, now);
    }

    if (hasElapsed(now, c.lastScheduleMs, 1000)) {
      c.lastScheduleMs = now;
      SharedState::setTimeValid(TimeService::valid());
      c.sched = c.schedule.evaluate(TimeService::localTime());
      if (c.sched.fillDue) {
        c.fillDuePending = true;
        LOGI(TAG, "scheduled fill #%d due", c.sched.entryIndex);
      }
    }

    MachineInputs in;
    in.nowMs = now;
    ProbeState st[kMaxProbes];
    for (uint8_t i = 0; i < kMaxProbes; ++i) st[i] = c.probes.committed(i);
    in.level = estimateLevel(c.layout, st);
    if (!c.probes.hasData() || hasElapsed(now, c.probes.lastUpdateMs(), app::kProbeStaleMs)) {
      in.level = LevelReading();
      in.level.fault = FaultCode::SENSOR_STALE;
    }
    in.contactorClosed = c.cfg.contactorFeedbackInstalled ? c.io.contactorClosed() : false;
    in.overloadTripped = c.cfg.overloadInputInstalled && c.io.overloadTripped();
    in.hhTripped = c.panel.ok && c.panel.hhTripped;
    in.estopActive = c.panel.ok && c.panel.estopOpen;
    in.localSwitch = c.cfg.localPanelInstalled ? (c.panel.ok ? c.panel.selector : LocalSwitch::UNKNOWN)
                                               : LocalSwitch::AUTO;
    in.scheduleWindowOpen = !c.cfg.scheduleRestrictEnabled || c.sched.windowOpen;
    in.scheduleFillDue = c.fillDuePending;
    c.fillDuePending = false;

    // ---- Commands (panel first, then remote) ---------------------------
    handlePanelButtons(c, in, now);
    handleServiceButton(c, now);
    Command cmd;
    TaskHandle_t replyTo = nullptr;
    while (SharedState::nextCommand(cmd, replyTo)) {
      const CommandOutcome o = c.fsm.handleCommand(cmd, in);
      LOGI(TAG, "%s %s -> %s/%s", toString(cmd.source), toString(cmd.action), toString(o.status), toString(o.reason));
      SharedState::reply(replyTo, o);
    }

    // ---- Decide and actuate ---------------------------------------------
    c.fsm.update(in);
    if (c.motor.apply(c.fsm.motorOutput(), now)) {
      ConfigStore::saveCounters(c.motor.totalRuntimeS(now), c.motor.totalStarts());
    }

    // ---- Persistence, events, indicators ----------------------------------
    if (c.fsm.takeLatchDirty()) ConfigStore::saveLatch(c.fsm.latchedFault());
    if (c.fsm.takeModeDirty()) ConfigStore::saveMode(c.fsm.softwareMode());

    MachineEvent ev;
    while (c.fsm.popEvent(ev)) {
      if (ev.type == MachineEventType::STATE_CHANGED) {
        LOGI(TAG, "state %s -> %s", toString(ev.fromState), toString(ev.toState));
      } else if (ev.type == MachineEventType::MOTOR_STARTED) {
        LOGI(TAG, "MOTOR ON (%s) level=%u%%", toString(ev.startReason), ev.levelPercent);
      } else if (ev.type == MachineEventType::MOTOR_STOPPED) {
        LOGI(TAG, "MOTOR OFF (%s) after %lus", toString(ev.stopReason), static_cast<unsigned long>(ev.runtimeMs / 1000));
      } else if (ev.type == MachineEventType::FAULT_RAISED) {
        LOGW(TAG, "FAULT %s", toString(ev.fault));
      }
      Alert a;
      if (c.alerts.fromEvent(ev, a)) SharedState::pushAlert(a);
    }

    updateLeds(c, now);
    publishSnapshot(c, in, now);

    if (!g_healthy && c.fsm.state() != MotorState::BOOT_SELF_TEST && c.probes.hasData() &&
        hasElapsed(now, c.startedMs, app::kOtaSelfTestMs)) {
      g_healthy = true;
    }
    esp_task_wdt_reset();
  }
}

}  // namespace

void ControlTask::start(const ControlBootInfo& info) {
  g_boot = info;
  g_ctx = new ControlContext();
  xTaskCreate(controlLoop, "control", app::kControlTaskStack, nullptr, app::kControlTaskPriority, nullptr);
}

bool ControlTask::healthy() { return g_healthy; }

}  // namespace wintek
