#include "app/NetworkTask.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "app/ControlTask.h"
#include "app/SerialConsole.h"
#include "app/SharedState.h"
#include "app/StateJson.h"
#include "app_config.h"
#include "connectivity/LocalApi.h"
#include "connectivity/LocalAuth.h"
#include "connectivity/MqttManager.h"
#include "connectivity/WiFiManager.h"
#include "diagnostics/Logger.h"
#include "services/CommandProcessor.h"
#include "services/OtaManager.h"
#include "services/TimeService.h"
#include "storage/ConfigJson.h"

namespace wintek {

namespace {

constexpr const char* TAG = "net";
constexpr size_t kAlertBacklog = 32;

class Network : public ApiBackend {
 public:
  FactoryIdentity id;
  char bootId[12] = {};
  WiFiManager wifi;
  MqttManager mqtt;
  LocalAuth auth;
  LocalApi api;
  CommandDeduplicator dedup;
  SerialConsole console;

  uint32_t pairingUntilMs = 0;
  uint32_t lastTelemetryMs = 0;
  uint32_t lastStatePublishMs = 0;
  uint32_t lastStateSeq = 0xFFFFFFFF;
  uint32_t lastWsBroadcastMs = 0;
  uint32_t lastWsSeq = 0xFFFFFFFF;
  uint32_t lastOtaSeq = 0xFFFFFFFF;
  bool forceStateSync = true;

  // Alerts not yet delivered to the broker (device keeps working offline).
  Alert backlog[kAlertBacklog];
  size_t backlogHead = 0, backlogCount = 0;

  DeviceIdentityView view() {
    DeviceIdentityView v{id.deviceId, bootId, wifi.rssi(), mqtt.connected()};
    return v;
  }

  // ------------------------------------------------------------------ ApiBackend
  int handleCommand(const char* body, size_t len, bool local, JsonObject ack) override {
    ParsedCommand pc;
    const ParseError pe = parseCommandJson(body, len, pc);
    const DeviceSnapshot snap0 = SharedState::snapshot();
    if (pe != ParseError::OK) {
      CommandOutcome o;
      o.status = CommandStatus::INVALID;
      buildAck(ack, pc.commandId[0] ? pc.commandId : "unknown", o, false, snap0, view());
      ack["error"] = toString(pe);
      return 400;
    }
    CommandOutcome prev;
    if (dedup.lookup(pc.commandId, prev)) {
      buildAck(ack, pc.commandId, prev, true, snap0, view());
      return 200;
    }
    CommandOutcome o;
    o.status = checkFreshness(pc, TimeService::valid(), TimeService::nowEpochS(), !local);
    if (o.status == CommandStatus::ACCEPTED) {
      if (pc.kind == CommandKind::OTA_UPDATE) {
        o.status = OtaManager::start(pc.ota) ? CommandStatus::ACCEPTED : CommandStatus::REJECTED;
        if (o.status != CommandStatus::ACCEPTED) o.reason = RejectReason::UNSUPPORTED;
      } else if (pc.cmd.action == CommandAction::ENTER_OTA || pc.cmd.action == CommandAction::EXIT_OTA) {
        o.status = CommandStatus::REJECTED;  // only the OTA manager drives maintenance mode
        o.reason = RejectReason::UNSUPPORTED;
      } else {
        pc.cmd.source = local ? CommandSource::LOCAL_API : CommandSource::CLOUD;
        o = SharedState::submit(pc.cmd);
      }
    }
    if (o.status != CommandStatus::TIMEOUT) dedup.remember(pc.commandId, o);
    // Report the state observed AFTER execution; the app shows ON only when
    // motorCommanded and (if installed) contactorClosed confirm it.
    vTaskDelay(pdMS_TO_TICKS(app::kControlPeriodMs * 2));
    buildAck(ack, pc.commandId, o, false, SharedState::snapshot(), view());
    if (pc.kind == CommandKind::OTA_UPDATE && o.status != CommandStatus::ACCEPTED) {
      ack["error"] = OtaManager::status().error;
    }
    forceStateSync = true;
    return o.status == CommandStatus::INVALID ? 400 : 200;
  }

  void fillInfo(JsonObject o) override {
    o["deviceId"] = id.deviceId;
    o["model"] = app::kModel;
    o["firmwareVersion"] = app::kFirmwareVersion;
    o["hardwareRevision"] = id.hwRev;
    o["apiVersion"] = 1;
    o["wifiConnected"] = wifi.connected();
    o["provisioning"] = wifi.provisioning();
    o["cloudConnected"] = mqtt.connected();
  }

  void fillState(JsonObject o) override {
    buildState(o, SharedState::snapshot(), view(), SharedState::config());
    const OtaStatus s = OtaManager::status();
    JsonObject ota = o["ota"].to<JsonObject>();
    ota["phase"] = toString(s.phase);
    ota["progress"] = s.progressPercent;
  }

  void fillDiagnostics(JsonObject o) override { buildDiagnostics(o, SharedState::snapshot(), view()); }

  void fillConfig(JsonObject o) override { configToJson(SharedState::config(), o); }

  int patchConfig(JsonObjectConst patch, JsonObject result) override {
    DeviceConfig c = SharedState::config();
    const char* field = nullptr;
    const ConfigError e = applyConfigPatch(patch, c, field);
    if (e != ConfigError::OK) {
      result["ok"] = false;
      result["error"] = toString(e);
      result["field"] = field;
      return 422;
    }
    if (!ConfigStore::saveConfig(c)) {
      result["ok"] = false;
      result["error"] = "STORAGE";
      return 500;
    }
    SharedState::setConfig(c);
    result["ok"] = true;
    configToJson(c, result["config"].to<JsonObject>());
    forceStateSync = true;
    publishConfig("sync", nullptr);
    return 200;
  }

  int setWifi(const char* ssid, const char* pass, JsonObject result) override {
    const bool ok = wifi.setCredentials(ssid, pass);
    result["ok"] = ok;
    if (!ok) result["error"] = "INVALID_CREDENTIALS";
    return ok ? 200 : 422;
  }

  int factoryReset(JsonObject result) override {
    const DeviceSnapshot s = SharedState::snapshot();
    if (s.motorCommanded || s.contactorClosed) {
      result["ok"] = false;
      result["error"] = "MOTOR_RUNNING";
      return 409;
    }
    result["ok"] = true;
    doFactoryReset();
    return 200;
  }

  bool pairingAllowed() override {
    return wifi.provisioning() || (pairingUntilMs != 0 && static_cast<int32_t>(millis() - pairingUntilMs) < 0);
  }

  // ------------------------------------------------------------------ helpers
  void doFactoryReset() {
    LOGW(TAG, "FACTORY RESET");
    // Force the motor off first through the normal command path.
    Command off;
    off.action = CommandAction::MOTOR_OFF;
    off.source = CommandSource::SYSTEM;
    SharedState::submit(off);
    ConfigStore::factoryReset();
    auth.unpairAll();
    wifi.forgetCredentials();
    delay(500);
    ESP.restart();
  }

  void publishJson(const char* sub, JsonDocument& d, int qos, bool retain) {
    String s;
    serializeJson(d, s);
    mqtt.publish(sub, s.c_str(), qos, retain);
  }

  void publishConfig(const char* requestId, const char* error) {
    if (!mqtt.connected()) return;
    JsonDocument d;
    d["schemaVersion"] = app::kSchemaVersion;
    d["deviceId"] = id.deviceId;
    d["requestId"] = requestId;
    d["ok"] = error == nullptr;
    if (error != nullptr) d["error"] = error;
    writeTimestamp(d.as<JsonObject>(), "timestamp");
    configToJson(SharedState::config(), d["config"].to<JsonObject>());
    publishJson("config/ack", d, 1, false);
  }

  void handleConfigMessage(const InboundMessage& m) {
    JsonDocument req;
    if (deserializeJson(req, m.data, m.len) != DeserializationError::Ok) return;
    const char* requestId = req["requestId"] | "";
    if (strlen(requestId) < 4 || strlen(requestId) > kCommandIdMax) return;
    // Same freshness rules as motor commands.
    ParsedCommand pc;
    pc.hasTimestamp = parseIso8601(req["timestamp"].as<const char*>(), pc.issuedAtS);
    pc.hasExpiry = parseIso8601(req["expiresAt"].as<const char*>(), pc.expiresAtS);
    if (checkFreshness(pc, TimeService::valid(), TimeService::nowEpochS(), true) != CommandStatus::ACCEPTED) {
      publishConfig(requestId, "EXPIRED");
      return;
    }
    JsonDocument result;
    const int code = patchConfig(req["config"].as<JsonObjectConst>(), result.to<JsonObject>());
    if (code != 200) {
      publishConfig(requestId, result["error"] | "INVALID");
    } else {
      publishConfig(requestId, nullptr);
    }
  }

  void handleInbound() {
    InboundMessage* m;
    while ((m = mqtt.popInbound()) != nullptr) {
      const char* sub = m->topic + strlen(mqtt.topicPrefix());
      if (m->retained) {
        // Never act on retained commands/config (could be arbitrarily old).
        LOGW(TAG, "ignoring retained message on %s", sub);
      } else if (strcmp(sub, "command") == 0) {
        JsonDocument ack;
        handleCommand(m->data, m->len, false, ack.to<JsonObject>());
        publishJson("command/ack", ack, 1, false);
      } else if (strcmp(sub, "config") == 0) {
        handleConfigMessage(*m);
      }
      free(m);
    }
  }

  void queueAlert(const Alert& a) {
    backlog[(backlogHead + backlogCount) % kAlertBacklog] = a;
    if (backlogCount < kAlertBacklog) {
      ++backlogCount;
    } else {
      backlogHead = (backlogHead + 1) % kAlertBacklog;  // drop oldest
    }
  }

  void deliverAlerts() {
    Alert a;
    while (SharedState::popAlert(a)) {
      JsonDocument d;
      buildAlert(d.to<JsonObject>(), a, view());
      api.broadcastAlert(d.as<JsonObjectConst>());
      queueAlert(a);
    }
    while (backlogCount > 0 && mqtt.connected()) {
      JsonDocument d;
      buildAlert(d.to<JsonObject>(), backlog[backlogHead], view());
      String s;
      serializeJson(d, s);
      if (!mqtt.publish("alerts", s.c_str(), 1, false)) break;
      backlogHead = (backlogHead + 1) % kAlertBacklog;
      --backlogCount;
    }
  }

  void publishPeriodic(uint32_t now) {
    const DeviceSnapshot s = SharedState::snapshot();
    const DeviceConfig cfg = SharedState::config();
    const bool stateChanged = s.seq != lastStateSeq;

    if ((s.seq != lastWsSeq && hasElapsed(now, lastWsBroadcastMs, 250)) || hasElapsed(now, lastWsBroadcastMs, 5000)) {
      lastWsBroadcastMs = now;
      lastWsSeq = s.seq;
      api.broadcastState();
    }

    if (!mqtt.connected()) return;
    if ((stateChanged || forceStateSync) && hasElapsed(now, lastStatePublishMs, app::kStatePublishMinIntervalMs)) {
      JsonDocument d;
      fillState(d.to<JsonObject>());
      publishJson("state", d, 1, true);
      lastStateSeq = s.seq;
      lastStatePublishMs = now;
      forceStateSync = false;
    }
    const uint32_t interval = (s.motorCommanded ? 10U : cfg.telemetryIntervalS) * 1000U;
    if (hasElapsed(now, lastTelemetryMs, interval)) {
      JsonDocument d;
      buildTelemetry(d.to<JsonObject>(), s, view());
      publishJson("telemetry", d, 0, false);
      lastTelemetryMs = now;
    }
    const OtaStatus os = OtaManager::status();
    if (os.seq != lastOtaSeq) {
      JsonDocument d;
      d["schemaVersion"] = app::kSchemaVersion;
      d["deviceId"] = id.deviceId;
      writeTimestamp(d.as<JsonObject>(), "timestamp");
      d["currentVersion"] = app::kFirmwareVersion;
      d["phase"] = toString(os.phase);
      d["progress"] = os.progressPercent;
      d["targetVersion"] = os.targetVersion;
      if (os.error[0]) d["error"] = os.error;
      publishJson("ota/status", d, 1, true);
      lastOtaSeq = os.seq;
    }
  }

  void handleSystemRequests(uint32_t now) {
    switch (SharedState::takeSystemRequest()) {
      case SystemRequest::OPEN_PAIRING:
        pairingUntilMs = now + app::kPairingWindowMs;
        LOGI(TAG, "pairing window open for %lus", static_cast<unsigned long>(app::kPairingWindowMs / 1000));
        break;
      case SystemRequest::START_PROVISIONING:
        wifi.startProvisioning(now);
        break;
      case SystemRequest::FACTORY_RESET: {
        const DeviceSnapshot s = SharedState::snapshot();
        if (s.motorCommanded || s.contactorClosed) {
          LOGW(TAG, "factory reset refused: motor running");
        } else {
          doFactoryReset();
        }
        break;
      }
      case SystemRequest::NONE:
        break;
    }
    if (pairingUntilMs != 0 && static_cast<int32_t>(now - pairingUntilMs) >= 0) pairingUntilMs = 0;
  }

  void run() {
    esp_task_wdt_add(nullptr);
    const DeviceConfig cfg = SharedState::config();
    TimeService::begin(cfg.timezone);
    wifi.begin(id.deviceId, id.setupCode);
    auth.begin(id.setupCode);
    api.begin(this, &auth, wifi.hostname(), id.deviceId);
    mqtt.begin(id);
    console.begin(&wifi);
    LOGI(TAG, "device %s fw %s hw %s", id.deviceId, app::kFirmwareVersion, id.hwRev);

    for (;;) {
      const uint32_t now = millis();
      wifi.loop(now);
      mqtt.loop(now, wifi.connected(), TimeService::valid());
      if (mqtt.takeJustConnected()) {
        LOGI(TAG, "cloud connected");
        forceStateSync = true;
        lastOtaSeq = 0xFFFFFFFF;
        publishConfig("sync", nullptr);
      }
      SharedState::setConnectivity(wifi.connected(), mqtt.connected(), wifi.provisioning(), pairingAllowed());
      handleInbound();
      api.loop(now);
      deliverAlerts();
      publishPeriodic(now);
      handleSystemRequests(now);
      OtaManager::confirmIfHealthy(ControlTask::healthy());
      console.loop();
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
};

Network* g_net = nullptr;

void networkLoop(void*) { g_net->run(); }

}  // namespace

void NetworkTask::start(const FactoryIdentity& id, const char* bootId) {
  g_net = new Network();
  g_net->id = id;
  strncpy(g_net->bootId, bootId, sizeof(g_net->bootId) - 1);
  xTaskCreate(networkLoop, "network", app::kNetworkTaskStack, nullptr, app::kNetworkTaskPriority, nullptr);
}

}  // namespace wintek
