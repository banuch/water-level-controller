// Shared helpers for host-side unit tests.
#pragma once

#include "motor/MotorStateMachine.h"
#include "sensors/LevelEstimator.h"
#include "storage/Config.h"

namespace wintek {
namespace test {

inline DeviceConfig defaultConfig() {
  DeviceConfig c;
  c.contactorFeedbackInstalled = true;
  c.overloadInputInstalled = true;
  c.localPanelInstalled = true;
  return c;
}

// Level by number of wet probes (0..4) with the default 25/50/75/95 layout.
inline LevelReading level(uint8_t wet) {
  ProbeLayout layout;
  ProbeState s[kMaxProbes];
  for (uint8_t i = 0; i < kMaxProbes; ++i) s[i] = (i < wet) ? ProbeState::WET : ProbeState::DRY;
  return estimateLevel(layout, s);
}

inline LevelReading invalidLevel() {
  LevelReading r;
  r.valid = false;
  r.fault = FaultCode::SENSOR_SEQUENCE;
  return r;
}

// Simulated plant: contactor feedback follows the output instantly.
struct Rig {
  DeviceConfig cfg = defaultConfig();
  MotorStateMachine m;
  MachineInputs in;
  uint32_t now = 1000;
  bool feedbackFollows = true;

  void begin(ControlMode mode = ControlMode::AUTO, FaultCode latch = FaultCode::NONE) {
    m.configure(cfg);
    in.nowMs = now;
    in.level = level(2);
    in.localSwitch = LocalSwitch::AUTO;
    m.begin(now, mode, latch);
  }
  void tick(uint32_t ms = 100) {
    now += ms;
    in.nowMs = now;
    if (feedbackFollows) in.contactorClosed = m.motorOutput();
    m.update(in);
    if (feedbackFollows) in.contactorClosed = m.motorOutput();
    in.scheduleFillDue = false;
  }
  void run(uint32_t totalMs, uint32_t step = 100) {
    for (uint32_t t = 0; t < totalMs; t += step) tick(step);
  }
  void passBoot() { run(cfg.restartDelayMs + 200); }
  CommandOutcome send(CommandAction a, CommandSource src = CommandSource::CLOUD, bool modeSpec = false,
                      ControlMode mode = ControlMode::AUTO) {
    Command c;
    c.action = a;
    c.source = src;
    c.modeSpecified = modeSpec;
    c.mode = mode;
    in.nowMs = now;
    CommandOutcome o = m.handleCommand(c, in);
    if (feedbackFollows) in.contactorClosed = m.motorOutput();
    return o;
  }
  void drainEvents() {
    MachineEvent e;
    while (m.popEvent(e)) {
    }
  }
};

}  // namespace test
}  // namespace wintek
