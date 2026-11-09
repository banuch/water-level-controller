// USB serial console for bench diagnostics and end-of-line factory
// provisioning (device id, setup code, broker URI/credentials, CA/client
// certificates). `factory lock` disables provisioning commands until the
// next factory reset of the identity namespace by the manufacturing tool.
#pragma once

#include <stddef.h>

namespace wintek {

class WiFiManager;

class SerialConsole {
 public:
  void begin(WiFiManager* wifi);
  void loop();

 private:
  void execute(char* line);
  void finishPem();

  WiFiManager* wifi_ = nullptr;
  char line_[160] = {};
  size_t len_ = 0;
  // PEM capture mode
  char pemKey_[8] = {};
  char* pem_ = nullptr;
  size_t pemLen_ = 0;
};

}  // namespace wintek
