// MQTT over TLS using the ESP-IDF esp-mqtt client bundled with the
// Arduino-ESP32 core (QoS 0/1/2, LWT, own task — never blocks control).
//
// Authentication: the broker CA must be provisioned (factory NVS "mqCa");
// without it the client refuses to connect (no unverified TLS ever).
// Device credentials: per-device username/password, or mutual TLS when a
// client certificate + key are provisioned ("mqCert"/"mqKey").
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mqtt_client.h>
#include <stddef.h>
#include <stdint.h>

#include "storage/ConfigStore.h"

namespace wintek {

struct InboundMessage {
  char topic[96];
  size_t len;
  bool retained;
  char data[1];  // flexible payload (allocated len + 1)
};

class MqttManager {
 public:
  bool begin(const FactoryIdentity& id);
  void loop(uint32_t nowMs, bool wifiUp, bool timeValid);
  bool configured() const { return configured_; }
  bool connected() const { return connected_; }
  // Returns false if not connected (caller keeps data for later).
  bool publish(const char* subtopic, const char* payload, int qos, bool retain);
  // Caller must free() the message.
  InboundMessage* popInbound();
  const char* topicPrefix() const { return prefix_; }
  // True exactly once after each (re)connection: caller re-syncs state.
  bool takeJustConnected();

 private:
  static void onEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
  void handleEvent(esp_mqtt_event_handle_t e);

  esp_mqtt_client_handle_t client_ = nullptr;
  QueueHandle_t inbound_ = nullptr;
  bool configured_ = false;
  bool started_ = false;
  volatile bool connected_ = false;
  volatile bool justConnected_ = false;
  volatile bool disconnectedEvent_ = false;
  uint32_t nextReconnectMs_ = 0;
  uint32_t backoffMs_ = 0;
  uint32_t wifiUpSinceMs_ = 0;
  bool wifiWasUp_ = false;

  char prefix_[48] = {};
  char lwtTopic_[64] = {};
  char clientId_[32] = {};
  char* ca_ = nullptr;
  char* cert_ = nullptr;
  char* key_ = nullptr;
  FactoryIdentity id_;

  // Fragment reassembly (esp-mqtt delivers large payloads in chunks).
  InboundMessage* partial_ = nullptr;
  size_t partialTotal_ = 0;
};

}  // namespace wintek
