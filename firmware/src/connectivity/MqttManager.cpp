#include "connectivity/MqttManager.h"

#include <Arduino.h>
#include <esp_random.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "diagnostics/Logger.h"

namespace wintek {

namespace {
constexpr const char* TAG = "mqtt";
constexpr size_t kPemMax = 4096;
constexpr uint32_t kTimeSyncGraceMs = 30000;

char* loadPemAlloc(const char* key) {
  char* buf = static_cast<char*>(malloc(kPemMax));
  if (buf == nullptr) return nullptr;
  if (ConfigStore::loadPem(key, buf, kPemMax) == 0) {
    free(buf);
    return nullptr;
  }
  return buf;
}
}  // namespace

bool MqttManager::begin(const FactoryIdentity& id) {
  id_ = id;
  snprintf(prefix_, sizeof(prefix_), "wintek/v1/%s/", id.deviceId);
  snprintf(lwtTopic_, sizeof(lwtTopic_), "%savailability", prefix_);
  snprintf(clientId_, sizeof(clientId_), "%s", id.deviceId);
  inbound_ = xQueueCreate(6, sizeof(InboundMessage*));
  backoffMs_ = app::kMqttBackoffMinMs;

  if (id_.mqttUri[0] == '\0') {
    LOGW(TAG, "broker URI not provisioned: cloud disabled (local control still available)");
    return false;
  }
  if (strncmp(id_.mqttUri, "mqtts://", 8) != 0) {
    LOGE(TAG, "refusing non-TLS broker URI");
    return false;
  }
  ca_ = loadPemAlloc("mqCa");
  if (ca_ == nullptr) {
    LOGE(TAG, "broker CA certificate not provisioned: cloud disabled");
    return false;
  }
  cert_ = loadPemAlloc("mqCert");
  key_ = loadPemAlloc("mqKey");
  if ((cert_ == nullptr) != (key_ == nullptr)) {
    LOGE(TAG, "client certificate and key must both be provisioned");
    free(cert_);
    free(key_);
    cert_ = key_ = nullptr;
  }

  esp_mqtt_client_config_t cfg = {};
  cfg.uri = id_.mqttUri;
  cfg.client_id = clientId_;
  cfg.username = id_.mqttUser;
  cfg.password = id_.mqttPass[0] ? id_.mqttPass : nullptr;
  cfg.cert_pem = ca_;
  cfg.client_cert_pem = cert_;
  cfg.client_key_pem = key_;
  cfg.lwt_topic = lwtTopic_;
  cfg.lwt_msg = "offline";
  cfg.lwt_msg_len = 7;
  cfg.lwt_qos = 1;
  cfg.lwt_retain = 1;
  cfg.keepalive = 60;
  cfg.disable_clean_session = 0;  // clean session: never receive queued (stale) commands
  cfg.disable_auto_reconnect = true;  // reconnection with our own exponential backoff
  cfg.buffer_size = app::kMqttMaxMessage;
  cfg.out_buffer_size = app::kMqttMaxMessage;
  cfg.network_timeout_ms = 10000;
  cfg.task_prio = 4;
  cfg.task_stack = 6144;
  client_ = esp_mqtt_client_init(&cfg);
  if (client_ == nullptr) {
    LOGE(TAG, "client init failed");
    return false;
  }
  esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MqttManager::onEvent, this);
  configured_ = true;
  LOGI(TAG, "configured for %s (%s)", id_.mqttUri, cert_ ? "mTLS" : "password");
  return true;
}

void MqttManager::onEvent(void* arg, esp_event_base_t, int32_t, void* data) {
  static_cast<MqttManager*>(arg)->handleEvent(static_cast<esp_mqtt_event_handle_t>(data));
}

void MqttManager::handleEvent(esp_mqtt_event_handle_t e) {
  switch (e->event_id) {
    case MQTT_EVENT_CONNECTED: {
      connected_ = true;
      justConnected_ = true;
      char t[96];
      snprintf(t, sizeof(t), "%scommand", prefix_);
      esp_mqtt_client_subscribe(client_, t, 1);
      snprintf(t, sizeof(t), "%sconfig", prefix_);
      esp_mqtt_client_subscribe(client_, t, 1);
      esp_mqtt_client_publish(client_, lwtTopic_, "online", 6, 1, 1);
      break;
    }
    case MQTT_EVENT_DISCONNECTED:
      connected_ = false;
      disconnectedEvent_ = true;
      break;
    case MQTT_EVENT_DATA: {
      if (e->current_data_offset == 0) {
        free(partial_);
        partial_ = nullptr;
        if (e->total_data_len <= 0 || static_cast<size_t>(e->total_data_len) > app::kMqttMaxMessage ||
            e->topic_len <= 0 || static_cast<size_t>(e->topic_len) >= sizeof(InboundMessage::topic)) {
          break;  // oversize or malformed: drop
        }
        partial_ = static_cast<InboundMessage*>(malloc(sizeof(InboundMessage) + e->total_data_len));
        if (partial_ == nullptr) break;
        memcpy(partial_->topic, e->topic, e->topic_len);
        partial_->topic[e->topic_len] = '\0';
        partial_->len = static_cast<size_t>(e->total_data_len);
        partial_->retained = e->retain;
        partialTotal_ = 0;
      }
      if (partial_ == nullptr) break;
      if (e->current_data_offset + e->data_len > static_cast<int>(partial_->len)) {
        free(partial_);
        partial_ = nullptr;
        break;
      }
      memcpy(partial_->data + e->current_data_offset, e->data, e->data_len);
      partialTotal_ += static_cast<size_t>(e->data_len);
      if (partialTotal_ == partial_->len) {
        partial_->data[partial_->len] = '\0';
        if (xQueueSend(inbound_, &partial_, 0) != pdTRUE) free(partial_);
        partial_ = nullptr;
      }
      break;
    }
    case MQTT_EVENT_ERROR:
      if (e->error_handle != nullptr && e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        LOGW(TAG, "transport error tls=0x%x stack=0x%x sock=%d", e->error_handle->esp_tls_last_esp_err,
             e->error_handle->esp_tls_stack_err, e->error_handle->esp_transport_sock_errno);
      } else if (e->error_handle != nullptr &&
                 e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        LOGE(TAG, "broker refused connection (code %d)", e->error_handle->connect_return_code);
      }
      break;
    default:
      break;
  }
}

void MqttManager::loop(uint32_t now, bool wifiUp, bool timeValid) {
  if (!configured_) return;
  if (wifiUp && !wifiWasUp_) wifiUpSinceMs_ = now;
  wifiWasUp_ = wifiUp;

  if (!wifiUp) {
    if (started_) {
      esp_mqtt_client_stop(client_);
      started_ = false;
      connected_ = false;
    }
    return;
  }
  if (!started_) {
    // Prefer a synchronised clock (command expiry checks), but do not wait forever.
    if (!timeValid && !hasElapsed(now, wifiUpSinceMs_, kTimeSyncGraceMs)) return;
    if (esp_mqtt_client_start(client_) == ESP_OK) {
      started_ = true;
      disconnectedEvent_ = false;
      LOGI(TAG, "connecting");
    }
    return;
  }
  if (connected_) {
    backoffMs_ = app::kMqttBackoffMinMs;
    return;
  }
  if (disconnectedEvent_) {
    disconnectedEvent_ = false;
    const uint32_t jitter = backoffMs_ / 5;
    const uint32_t delay = backoffMs_ - jitter + (esp_random() % (2 * jitter + 1));
    nextReconnectMs_ = now + delay;
    LOGW(TAG, "disconnected, reconnect in %lus", static_cast<unsigned long>(delay / 1000));
    backoffMs_ = backoffMs_ * 2 > app::kMqttBackoffMaxMs ? app::kMqttBackoffMaxMs : backoffMs_ * 2;
    return;
  }
  if (nextReconnectMs_ != 0 && static_cast<int32_t>(now - nextReconnectMs_) >= 0) {
    nextReconnectMs_ = 0;
    esp_mqtt_client_reconnect(client_);
  }
}

bool MqttManager::publish(const char* sub, const char* payload, int qos, bool retain) {
  if (!connected_) return false;
  char topic[96];
  snprintf(topic, sizeof(topic), "%s%s", prefix_, sub);
  return esp_mqtt_client_publish(client_, topic, payload, 0, qos, retain ? 1 : 0) >= 0;
}

InboundMessage* MqttManager::popInbound() {
  InboundMessage* m = nullptr;
  if (inbound_ != nullptr && xQueueReceive(inbound_, &m, 0) == pdTRUE) return m;
  return nullptr;
}

bool MqttManager::takeJustConnected() {
  const bool j = justConnected_;
  justConnected_ = false;
  return j;
}

}  // namespace wintek
