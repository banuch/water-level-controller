// Local LAN API: HTTP REST (port 80) for commands/configuration and a
// WebSocket (port 81) for live state, plus mDNS "_wintek._tcp" discovery.
// All control endpoints require a paired client signature (LocalAuth).
// Plain HTTP on the LAN: integrity and replay protection come from the
// HMAC request signatures; payload confidentiality is NOT provided (see
// docs/ARCHITECTURE.md §8 for the limitation and mitigations).
#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "connectivity/LocalAuth.h"

class WebServer;
class WebSocketsServer;

namespace wintek {

// Implemented by the network task; keeps LocalApi free of policy.
class ApiBackend {
 public:
  virtual ~ApiBackend() = default;
  // Parses, validates, de-duplicates and executes a command; fills `ack`.
  virtual int handleCommand(const char* body, size_t len, bool local, JsonObject ack) = 0;
  virtual void fillInfo(JsonObject o) = 0;
  virtual void fillState(JsonObject o) = 0;
  virtual void fillDiagnostics(JsonObject o) = 0;
  virtual void fillConfig(JsonObject o) = 0;
  virtual int patchConfig(JsonObjectConst patch, JsonObject result) = 0;  // HTTP status
  virtual int setWifi(const char* ssid, const char* pass, JsonObject result) = 0;
  virtual int factoryReset(JsonObject result) = 0;
  virtual bool pairingAllowed() = 0;
};

class LocalApi {
 public:
  void begin(ApiBackend* backend, LocalAuth* auth, const char* hostname, const char* deviceId);
  void loop(uint32_t nowMs);
  // Push the current state to authenticated WebSocket clients.
  void broadcastState();
  void broadcastAlert(JsonObjectConst alert);

 private:
  bool authenticate(const char* method, const char* path, uint32_t now);
  void sendJson(int code, JsonDocument& doc);
  void sendError(int code, const char* error, const char* detail = nullptr);
  void onWsEvent(uint8_t num, int type, uint8_t* payload, size_t length);

  ApiBackend* backend_ = nullptr;
  LocalAuth* auth_ = nullptr;
  WebServer* http_ = nullptr;
  WebSocketsServer* ws_ = nullptr;
  bool mdnsStarted_ = false;
  char hostname_[32] = {};
  char deviceId_[24] = {};

  static constexpr uint8_t kMaxWs = 4;
  struct WsClient {
    bool connected = false;
    bool authed = false;
    uint32_t connectedMs = 0;
    char nonce[33] = {};
  };
  WsClient wsClients_[kMaxWs];
};

}  // namespace wintek
