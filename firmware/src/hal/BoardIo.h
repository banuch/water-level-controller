// Digital I/O: contactor drive and feedback, overload input, service button
// and the TCA9555 front-panel expander. Used only by the control task.
#pragma once

#include <stdint.h>

#include "core/Types.h"

namespace wintek {

struct PanelInputs {
  bool ok = false;  // expander answered
  LocalSwitch selector = LocalSwitch::UNKNOWN;
  bool startPressed = false;
  bool stopPressed = false;  // NC button: also true if the wire is broken
  bool hhTripped = false;
  bool estopOpen = false;
  bool hhCircuitOk = false;
};

struct PanelOutputs {
  bool ledWifi = false;
  bool ledMotor = false;
  bool ledFault = false;
  bool buzzer = false;
};

class BoardIo {
 public:
  // Must be called first thing in setup(): forces the contactor output OFF.
  static void earlySafeInit();

  bool begin(bool panelInstalled);
  void setContactor(bool on);
  bool contactorCommanded() const { return contactorOn_; }

  // Debounced (3 consecutive 20 ms samples) direct inputs.
  void pollDirectInputs();
  bool contactorClosed() const { return fbState_; }
  bool overloadTripped() const { return olState_; }
  bool serviceButtonDown() const { return svcState_; }

  // Panel (expander). Returns false after kPanelFaultReads failed reads.
  bool pollPanel(PanelInputs& out);
  void writePanel(const PanelOutputs& o);
  bool panelPresent() const { return panelPresent_; }

 private:
  bool readExpanderPort0(uint8_t& v);
  bool writeExpanderReg(uint8_t reg, uint8_t v);

  bool contactorOn_ = false;
  bool panelInstalled_ = false;
  bool panelPresent_ = false;
  uint8_t panelFailures_ = 0;
  uint8_t lastOutPort_ = 0xFF;

  bool fbState_ = false, olState_ = false, svcState_ = false;
  uint8_t fbCnt_ = 0, olCnt_ = 0, svcCnt_ = 0;
  uint8_t panelStable_ = 0xFF, panelLast_ = 0xFF, panelCnt_ = 0;
};

}  // namespace wintek
