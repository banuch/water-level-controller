// Command envelope parsing, freshness checks and duplicate suppression.
// Shared by the MQTT and local API paths so both apply identical rules
// before the command reaches the state machine (which applies the safety
// rules). Hardware independent; uses ArduinoJson (portable).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/Types.h"
#include "services/OtaManifest.h"

namespace wintek {

constexpr size_t kCommandIdMax = 48;
constexpr int64_t kMaxCommandLifetimeS = 120;  // expiresAt - timestamp
constexpr int64_t kMaxClockSkewS = 30;         // tolerate app clock ahead of device

enum class CommandKind : uint8_t { MOTOR = 0, OTA_UPDATE };

struct ParsedCommand {
  char commandId[kCommandIdMax + 1] = {};
  CommandKind kind = CommandKind::MOTOR;
  Command cmd;
  bool hasTimestamp = false;
  bool hasExpiry = false;
  int64_t issuedAtS = 0;   // unix seconds
  int64_t expiresAtS = 0;  // unix seconds
  OtaManifest ota;
};

enum class ParseError : uint8_t {
  OK = 0,
  MALFORMED_JSON,
  MISSING_COMMAND_ID,
  BAD_COMMAND_ID,
  UNKNOWN_ACTION,
  BAD_MODE,
  BAD_TIMESTAMP,
  BAD_OTA,
};

const char* toString(ParseError e);

ParseError parseCommandJson(const char* json, size_t len, ParsedCommand& out);

// Parses "YYYY-MM-DDTHH:MM:SS[.fff](Z|+hh:mm|-hh:mm)" into unix seconds.
bool parseIso8601(const char* s, int64_t& outEpochS);
// Formats unix seconds as "YYYY-MM-DDTHH:MM:SSZ" (buf >= 21 bytes).
void formatIso8601(int64_t epochS, char* buf, size_t len);

// requireExpiry: cloud commands must carry timestamp + expiresAt; local API
// commands are protected by single-use nonces instead and may omit them.
CommandStatus checkFreshness(const ParsedCommand& c, bool timeValid, int64_t nowS, bool requireExpiry);

class CommandDeduplicator {
 public:
  static constexpr uint8_t kSize = 24;
  bool lookup(const char* commandId, CommandOutcome& previous) const;
  void remember(const char* commandId, const CommandOutcome& outcome);
  void clear() { count_ = head_ = 0; }

 private:
  struct Entry {
    char id[kCommandIdMax + 1];
    CommandOutcome outcome;
  };
  Entry entries_[kSize] = {};
  uint8_t count_ = 0;
  uint8_t head_ = 0;
};

}  // namespace wintek
