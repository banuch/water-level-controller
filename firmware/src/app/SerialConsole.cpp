#include "app/SerialConsole.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>

#include "app/SharedState.h"
#include "app_config.h"
#include "connectivity/WiFiManager.h"
#include "diagnostics/Logger.h"
#include "storage/ConfigJson.h"
#include "storage/ConfigStore.h"

namespace wintek {

namespace {
constexpr size_t kPemCap = 4096;

bool factoryLocked() {
  Preferences p;
  bool locked = false;
  if (p.begin("factory", true)) {
    locked = p.getBool("locked", false);
    p.end();
  }
  return locked;
}

void printHelp() {
  Serial.println(F("commands:\n"
                   "  status                      current state snapshot\n"
                   "  config                      effective configuration (JSON)\n"
                   "  logs                        recent log lines\n"
                   "  wifi <ssid> [password]      set Wi-Fi credentials\n"
                   "  factory set <key> <value>   keys: devId setup hwRev mqUri mqUser mqPass\n"
                   "  factory pem <key>           then paste PEM; keys: mqCa mqCert mqKey otaCa\n"
                   "  factory lock                disable factory commands\n"
                   "  reboot"));
}
}  // namespace

void SerialConsole::begin(WiFiManager* wifi) { wifi_ = wifi; }

void SerialConsole::finishPem() {
  pem_[pemLen_] = '\0';
  const bool ok = ConfigStore::savePem(pemKey_, pem_);
  Serial.printf("pem %s %s (%u bytes)\n", pemKey_, ok ? "saved" : "REJECTED", static_cast<unsigned>(pemLen_));
  free(pem_);
  pem_ = nullptr;
  pemLen_ = 0;
}

void SerialConsole::execute(char* line) {
  char* save = nullptr;
  char* cmd = strtok_r(line, " ", &save);
  if (cmd == nullptr) return;

  if (strcmp(cmd, "help") == 0) {
    printHelp();
  } else if (strcmp(cmd, "status") == 0) {
    const DeviceSnapshot s = SharedState::snapshot();
    Serial.printf("state=%s mode=%s switch=%s motor=%d contactor=%d level=%s%u%% fault=%s run=%lus\n",
                  toString(s.state), toString(s.mode), toString(s.localSwitch), s.motorCommanded, s.contactorClosed,
                  s.level.valid ? "" : "INVALID ", s.level.levelPercent, toString(s.activeFault),
                  static_cast<unsigned long>(s.currentRunMs / 1000));
    for (uint8_t i = 0; i < kMaxProbes; ++i) {
      Serial.printf("  probe%u %s vA=%d vB=%d Rw=%dk\n", i, toString(s.probeState[i]),
                    static_cast<int>(s.probeSample[i].vAMv), static_cast<int>(s.probeSample[i].vBMv),
                    static_cast<int>(s.probeSample[i].waterKohm));
    }
  } else if (strcmp(cmd, "config") == 0) {
    JsonDocument d;
    configToJson(SharedState::config(), d.to<JsonObject>());
    serializeJsonPretty(d, Serial);
    Serial.println();
  } else if (strcmp(cmd, "logs") == 0) {
    const size_t cap = Logger::kLines * Logger::kLineLen + 1;
    char* buf = static_cast<char*>(malloc(cap));
    if (buf != nullptr) {
      Logger::dump(buf, cap);
      Serial.print(buf);
      free(buf);
    }
  } else if (strcmp(cmd, "wifi") == 0) {
    const char* ssid = strtok_r(nullptr, " ", &save);
    const char* pass = strtok_r(nullptr, "", &save);
    Serial.println(wifi_ != nullptr && wifi_->setCredentials(ssid, pass ? pass : "") ? "wifi saved" : "wifi INVALID");
  } else if (strcmp(cmd, "factory") == 0) {
    if (factoryLocked()) {
      Serial.println("factory commands locked");
      return;
    }
    const char* sub = strtok_r(nullptr, " ", &save);
    if (sub != nullptr && strcmp(sub, "set") == 0) {
      const char* key = strtok_r(nullptr, " ", &save);
      const char* val = strtok_r(nullptr, "", &save);
      const bool ok = key && val && ConfigStore::saveFactoryField(key, val);
      Serial.printf("factory %s %s (reboot to apply)\n", key ? key : "?", ok ? "saved" : "REJECTED");
    } else if (sub != nullptr && strcmp(sub, "pem") == 0) {
      const char* key = strtok_r(nullptr, " ", &save);
      if (key == nullptr || strlen(key) >= sizeof(pemKey_)) {
        Serial.println("usage: factory pem <mqCa|mqCert|mqKey|otaCa>");
        return;
      }
      strcpy(pemKey_, key);
      pem_ = static_cast<char*>(malloc(kPemCap));
      pemLen_ = 0;
      Serial.println("paste PEM now (ends at -----END ...-----)");
    } else if (sub != nullptr && strcmp(sub, "lock") == 0) {
      Preferences p;
      if (p.begin("factory", false)) {
        p.putBool("locked", true);
        p.end();
      }
      Serial.println("factory locked");
    } else {
      printHelp();
    }
  } else if (strcmp(cmd, "reboot") == 0) {
    Serial.println("rebooting");
    delay(100);
    ESP.restart();
  } else {
    printHelp();
  }
}

void SerialConsole::loop() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c != '\n') {
      if (len_ < sizeof(line_) - 1) line_[len_++] = static_cast<char>(c);
      continue;
    }
    line_[len_] = '\0';
    if (pem_ != nullptr) {
      if (pemLen_ + len_ + 2 < kPemCap) {
        memcpy(pem_ + pemLen_, line_, len_);
        pemLen_ += len_;
        pem_[pemLen_++] = '\n';
      }
      if (strncmp(line_, "-----END ", 9) == 0) finishPem();
    } else {
      execute(line_);
    }
    len_ = 0;
  }
}

}  // namespace wintek
