#include "connectivity/LocalApi.h"

#include <ESPmDNS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include "app_config.h"
#include "diagnostics/Logger.h"

namespace wintek {

namespace {
constexpr const char* TAG = "api";
constexpr uint32_t kWsAuthTimeoutMs = 5000;
const char* kHeaderKeys[] = {"X-Wintek-Token-Id", "X-Wintek-Nonce", "X-Wintek-Signature"};
LocalApi* g_self = nullptr;

const char* authError(AuthResult r) {
  switch (r) {
    case AuthResult::MISSING: return "AUTH_MISSING";
    case AuthResult::BAD_NONCE: return "AUTH_BAD_NONCE";
    case AuthResult::UNKNOWN_TOKEN: return "AUTH_UNKNOWN_CLIENT";
    case AuthResult::BAD_SIGNATURE: return "AUTH_BAD_SIGNATURE";
    case AuthResult::LOCKED_OUT: return "AUTH_LOCKED_OUT";
    default: return "AUTH_FAILED";
  }
}
}  // namespace

void LocalApi::begin(ApiBackend* backend, LocalAuth* auth, const char* hostname, const char* deviceId) {
  backend_ = backend;
  auth_ = auth;
  g_self = this;
  strncpy(hostname_, hostname, sizeof(hostname_) - 1);
  strncpy(deviceId_, deviceId, sizeof(deviceId_) - 1);

  http_ = new WebServer(app::kHttpPort);
  ws_ = new WebSocketsServer(app::kWsPort);
  http_->collectHeaders(kHeaderKeys, 3);

  // ---- Unauthenticated: identification and nonce only ---------------------
  http_->on("/api/v1/info", HTTP_GET, [this]() {
    JsonDocument d;
    backend_->fillInfo(d.to<JsonObject>());
    d["paired"] = auth_->pairedCount() > 0;
    d["pairingOpen"] = backend_->pairingAllowed();
    sendJson(200, d);
  });
  http_->on("/api/v1/nonce", HTTP_GET, [this]() {
    JsonDocument d;
    char n[33];
    auth_->issueNonce(n, millis());
    d["nonce"] = n;
    d["ttlMs"] = app::kNonceTtlMs;
    sendJson(200, d);
  });

  // ---- Pairing (physical-access window only) ------------------------------
  http_->on("/api/v1/pair", HTTP_POST, [this]() {
    if (!backend_->pairingAllowed()) return sendError(403, "PAIRING_CLOSED", "press the service button for 1-3 s");
    JsonDocument req;
    if (deserializeJson(req, http_->arg("plain")) != DeserializationError::Ok) return sendError(400, "MALFORMED_JSON");
    char tokenId[9], wrapped[65];
    const int slot = auth_->pair(req["nonce"] | "", req["clientNonce"] | "", req["clientName"] | "app",
                                 req["proof"] | "", millis(), tokenId, wrapped);
    if (slot < 0) return sendError(401, "PAIRING_FAILED");
    JsonDocument d;
    d["tokenId"] = tokenId;
    d["wrappedToken"] = wrapped;
    d["deviceId"] = deviceId_;
    sendJson(200, d);
  });

  // ---- Authenticated endpoints ---------------------------------------------
  http_->on("/api/v1/unpair", HTTP_POST, [this]() {
    if (!authenticate("POST", "/api/v1/unpair", millis())) return;
    auth_->unpair(http_->header("X-Wintek-Token-Id").c_str());
    JsonDocument d;
    d["ok"] = true;
    sendJson(200, d);
  });
  http_->on("/api/v1/state", HTTP_GET, [this]() {
    if (!authenticate("GET", "/api/v1/state", millis())) return;
    JsonDocument d;
    backend_->fillState(d.to<JsonObject>());
    sendJson(200, d);
  });
  http_->on("/api/v1/diagnostics", HTTP_GET, [this]() {
    if (!authenticate("GET", "/api/v1/diagnostics", millis())) return;
    JsonDocument d;
    backend_->fillDiagnostics(d.to<JsonObject>());
    sendJson(200, d);
  });
  http_->on("/api/v1/logs", HTTP_GET, [this]() {
    if (!authenticate("GET", "/api/v1/logs", millis())) return;
    const size_t cap = Logger::kLines * Logger::kLineLen + 1;
    char* buf = static_cast<char*>(malloc(cap));
    if (buf == nullptr) return sendError(503, "NO_MEMORY");
    Logger::dump(buf, cap);
    http_->send(200, "text/plain", buf);
    free(buf);
  });
  http_->on("/api/v1/command", HTTP_POST, [this]() {
    if (!authenticate("POST", "/api/v1/command", millis())) return;
    const String& body = http_->arg("plain");
    JsonDocument d;
    const int code = backend_->handleCommand(body.c_str(), body.length(), true, d.to<JsonObject>());
    sendJson(code, d);
  });
  http_->on("/api/v1/config", HTTP_GET, [this]() {
    if (!authenticate("GET", "/api/v1/config", millis())) return;
    JsonDocument d;
    backend_->fillConfig(d.to<JsonObject>());
    sendJson(200, d);
  });
  http_->on("/api/v1/config", HTTP_PUT, [this]() {
    if (!authenticate("PUT", "/api/v1/config", millis())) return;
    JsonDocument req;
    if (deserializeJson(req, http_->arg("plain")) != DeserializationError::Ok || !req.is<JsonObject>()) {
      return sendError(400, "MALFORMED_JSON");
    }
    JsonDocument d;
    const int code = backend_->patchConfig(req.as<JsonObjectConst>(), d.to<JsonObject>());
    sendJson(code, d);
  });
  http_->on("/api/v1/wifi", HTTP_POST, [this]() {
    if (!authenticate("POST", "/api/v1/wifi", millis())) return;
    JsonDocument req;
    if (deserializeJson(req, http_->arg("plain")) != DeserializationError::Ok) return sendError(400, "MALFORMED_JSON");
    JsonDocument d;
    const int code = backend_->setWifi(req["ssid"] | "", req["password"] | "", d.to<JsonObject>());
    sendJson(code, d);
  });
  http_->on("/api/v1/factory-reset", HTTP_POST, [this]() {
    if (!authenticate("POST", "/api/v1/factory-reset", millis())) return;
    JsonDocument req;
    deserializeJson(req, http_->arg("plain"));
    if (strcmp(req["confirm"] | "", "FACTORY-RESET") != 0) return sendError(400, "CONFIRMATION_REQUIRED");
    JsonDocument d;
    const int code = backend_->factoryReset(d.to<JsonObject>());
    sendJson(code, d);
  });
  http_->onNotFound([this]() { sendError(404, "NOT_FOUND"); });
  http_->begin();

  ws_->begin();
  ws_->onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    onWsEvent(num, static_cast<int>(type), payload, length);
  });
  LOGI(TAG, "local API on :%u, live state on :%u", app::kHttpPort, app::kWsPort);
}

bool LocalApi::authenticate(const char* method, const char* path, uint32_t now) {
  const String& body = http_->arg("plain");
  const AuthResult r = auth_->verify(method, path, http_->header("X-Wintek-Token-Id").c_str(),
                                     http_->header("X-Wintek-Nonce").c_str(),
                                     http_->header("X-Wintek-Signature").c_str(),
                                     reinterpret_cast<const uint8_t*>(body.c_str()), body.length(), now);
  if (r == AuthResult::OK) return true;
  sendError(r == AuthResult::LOCKED_OUT ? 429 : 401, authError(r));
  return false;
}

void LocalApi::sendJson(int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  http_->sendHeader("Cache-Control", "no-store");
  http_->send(code, "application/json", out);
}

void LocalApi::sendError(int code, const char* error, const char* detail) {
  JsonDocument d;
  d["error"] = error;
  if (detail != nullptr) d["detail"] = detail;
  sendJson(code, d);
}

void LocalApi::onWsEvent(uint8_t num, int type, uint8_t* payload, size_t length) {
  if (num >= kMaxWs) {
    ws_->disconnect(num);
    return;
  }
  WsClient& c = wsClients_[num];
  switch (static_cast<WStype_t>(type)) {
    case WStype_CONNECTED: {
      c = WsClient();
      c.connected = true;
      c.connectedMs = millis();
      auth_->issueNonce(c.nonce, c.connectedMs);
      JsonDocument d;
      d["type"] = "challenge";
      d["nonce"] = c.nonce;
      String s;
      serializeJson(d, s);
      ws_->sendTXT(num, s);
      break;
    }
    case WStype_DISCONNECTED:
      c = WsClient();
      break;
    case WStype_TEXT: {
      JsonDocument req;
      if (deserializeJson(req, payload, length) != DeserializationError::Ok) break;
      const char* t = req["type"] | "";
      if (!c.authed && strcmp(t, "auth") == 0) {
        if (auth_->verifyWs(req["tokenId"] | "", c.nonce, req["signature"] | "", millis()) == AuthResult::OK) {
          c.authed = true;
          ws_->sendTXT(num, "{\"type\":\"auth\",\"ok\":true}");
          broadcastState();
        } else {
          ws_->sendTXT(num, "{\"type\":\"auth\",\"ok\":false}");
          ws_->disconnect(num);
        }
      }
      // Commands are deliberately NOT accepted over the WebSocket: they go
      // through the signed REST endpoint so each one is individually authenticated.
      break;
    }
    default:
      break;
  }
}

void LocalApi::broadcastState() {
  if (ws_ == nullptr) return;
  JsonDocument d;
  d["type"] = "state";
  backend_->fillState(d["data"].to<JsonObject>());
  String s;
  serializeJson(d, s);
  for (uint8_t i = 0; i < kMaxWs; ++i) {
    if (wsClients_[i].authed) ws_->sendTXT(i, s);
  }
}

void LocalApi::broadcastAlert(JsonObjectConst alert) {
  if (ws_ == nullptr) return;
  JsonDocument d;
  d["type"] = "alert";
  d["data"] = alert;
  String s;
  serializeJson(d, s);
  for (uint8_t i = 0; i < kMaxWs; ++i) {
    if (wsClients_[i].authed) ws_->sendTXT(i, s);
  }
}

void LocalApi::loop(uint32_t now) {
  const bool netUp = WiFi.status() == WL_CONNECTED || (WiFi.getMode() & WIFI_AP);
  if (netUp && !mdnsStarted_) {
    if (MDNS.begin(hostname_)) {
      MDNS.addService("wintek", "tcp", app::kHttpPort);
      MDNS.addServiceTxt("wintek", "tcp", "id", static_cast<const char*>(deviceId_));
      MDNS.addServiceTxt("wintek", "tcp", "fw", app::kFirmwareVersion);
      MDNS.addServiceTxt("wintek", "tcp", "api", "1");
      MDNS.addServiceTxt("wintek", "tcp", "ws", "81");
      mdnsStarted_ = true;
    }
  }
  http_->handleClient();
  ws_->loop();
  for (uint8_t i = 0; i < kMaxWs; ++i) {
    WsClient& c = wsClients_[i];
    if (c.connected && !c.authed && hasElapsed(now, c.connectedMs, kWsAuthTimeoutMs)) {
      ws_->disconnect(i);
      c = WsClient();
    }
  }
}

}  // namespace wintek
