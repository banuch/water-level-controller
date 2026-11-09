// Application-wide constants (not user configurable).
#pragma once

#include <stdint.h>

#ifndef WINTEK_FW_VERSION
#define WINTEK_FW_VERSION "0.3.0"
#endif

#ifndef WINTEK_HW_REV
#define WINTEK_HW_REV "DEVKIT"  // product PCB builds set e.g. -DWINTEK_HW_REV=\"A\"
#endif

namespace app {

constexpr const char* kModel = "WT-TANK-C3";
constexpr const char* kFirmwareVersion = WINTEK_FW_VERSION;
constexpr const char* kHardwareRevision = WINTEK_HW_REV;
constexpr uint8_t kSchemaVersion = 1;

// Tasks
constexpr uint32_t kControlPeriodMs = 20;
constexpr uint32_t kProbePeriodMs = 250;
constexpr uint32_t kProbeStaleMs = 2000;
constexpr uint32_t kControlTaskStack = 6144;
constexpr uint32_t kNetworkTaskStack = 12288;
constexpr uint32_t kOtaTaskStack = 10240;
constexpr uint8_t kControlTaskPriority = 5;
constexpr uint8_t kNetworkTaskPriority = 3;
constexpr uint8_t kOtaTaskPriority = 2;
constexpr uint32_t kWatchdogTimeoutS = 15;  // TLS handshakes in the network task can take seconds

// Local panel
constexpr uint32_t kButtonDebounceMs = 50;
constexpr uint32_t kResetHoldMs = 3000;           // STOP held: fault reset
constexpr uint32_t kServicePairingMinMs = 1000;   // service button 1-3 s: open pairing window
constexpr uint32_t kServiceProvisionMs = 5000;    // 5-10 s: provisioning access point
constexpr uint32_t kServiceFactoryResetMs = 15000;
constexpr uint32_t kPanelFaultReads = 3;

// Connectivity
constexpr uint32_t kWifiBackoffMinMs = 2000;
constexpr uint32_t kWifiBackoffMaxMs = 300000;
constexpr uint32_t kMqttBackoffMinMs = 2000;
constexpr uint32_t kMqttBackoffMaxMs = 300000;
constexpr uint32_t kProvisioningWindowMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kPairingWindowMs = 2UL * 60UL * 1000UL;
constexpr uint16_t kHttpPort = 80;
constexpr uint16_t kWsPort = 81;
constexpr uint8_t kMaxPairedClients = 4;
constexpr uint32_t kNonceTtlMs = 30000;
constexpr uint32_t kStatePublishMinIntervalMs = 1000;
constexpr uint32_t kMqttMaxMessage = 2048;

// OTA
constexpr uint8_t kOtaMaxBootAttempts = 3;
constexpr uint32_t kOtaSelfTestMs = 60000;

}  // namespace app
