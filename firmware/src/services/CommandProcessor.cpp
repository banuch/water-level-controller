#include "services/CommandProcessor.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

namespace wintek {

const char* toString(ParseError e) {
  switch (e) {
    case ParseError::OK: return "OK";
    case ParseError::MALFORMED_JSON: return "MALFORMED_JSON";
    case ParseError::MISSING_COMMAND_ID: return "MISSING_COMMAND_ID";
    case ParseError::BAD_COMMAND_ID: return "BAD_COMMAND_ID";
    case ParseError::UNKNOWN_ACTION: return "UNKNOWN_ACTION";
    case ParseError::BAD_MODE: return "BAD_MODE";
    case ParseError::BAD_TIMESTAMP: return "BAD_TIMESTAMP";
    case ParseError::BAD_OTA: return "BAD_OTA";
  }
  return "UNKNOWN";
}

namespace {

// Howard Hinnant's days_from_civil.
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<int>(yy + (m <= 2));
}

bool readDigits(const char*& p, int n, int& out) {
  out = 0;
  for (int i = 0; i < n; ++i) {
    if (p[i] < '0' || p[i] > '9') return false;
    out = out * 10 + (p[i] - '0');
  }
  p += n;
  return true;
}

bool validId(const char* s) {
  const size_t n = strlen(s);
  if (n < 4 || n > kCommandIdMax) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = s[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.' || c == ':';
    if (!ok) return false;
  }
  return true;
}

bool copyStr(JsonVariantConst v, char* dst, size_t cap) {
  const char* s = v.as<const char*>();
  if (s == nullptr) return false;
  const size_t n = strlen(s);
  if (n >= cap) return false;
  memcpy(dst, s, n + 1);
  return true;
}

}  // namespace

bool parseIso8601(const char* s, int64_t& out) {
  if (s == nullptr) return false;
  const char* p = s;
  int Y, M, D, h, mi, sec;
  if (!readDigits(p, 4, Y) || *p++ != '-') return false;
  if (!readDigits(p, 2, M) || *p++ != '-') return false;
  if (!readDigits(p, 2, D) || (*p != 'T' && *p != 't' && *p != ' ')) return false;
  ++p;
  if (!readDigits(p, 2, h) || *p++ != ':') return false;
  if (!readDigits(p, 2, mi) || *p++ != ':') return false;
  if (!readDigits(p, 2, sec)) return false;
  if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || mi > 59 || sec > 60) return false;
  if (*p == '.') {
    ++p;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') ++p;
  }
  int offsetS = 0;
  if (*p == 'Z' || *p == 'z') {
    ++p;
  } else if (*p == '+' || *p == '-') {
    const int sign = (*p == '-') ? -1 : 1;
    ++p;
    int oh, om;
    if (!readDigits(p, 2, oh) || *p++ != ':' || !readDigits(p, 2, om)) return false;
    if (oh > 14 || om > 59) return false;
    offsetS = sign * (oh * 3600 + om * 60);
  } else {
    return false;  // timezone designator required
  }
  if (*p != '\0') return false;
  out = daysFromCivil(Y, static_cast<unsigned>(M), static_cast<unsigned>(D)) * 86400 + h * 3600 + mi * 60 + sec -
        offsetS;
  return true;
}

void formatIso8601(int64_t epochS, char* buf, size_t len) {
  int64_t days = epochS / 86400;
  int64_t rem = epochS % 86400;
  if (rem < 0) {
    rem += 86400;
    --days;
  }
  int y;
  unsigned m, d;
  civilFromDays(days, y, m, d);
  snprintf(buf, len, "%04d-%02u-%02uT%02d:%02d:%02dZ", y, m, d, static_cast<int>(rem / 3600),
           static_cast<int>((rem % 3600) / 60), static_cast<int>(rem % 60));
}

ParseError parseCommandJson(const char* json, size_t len, ParsedCommand& out) {
  out = ParsedCommand();
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) {
    return ParseError::MALFORMED_JSON;
  }
  JsonObjectConst o = doc.as<JsonObjectConst>();

  if (!o["commandId"].is<const char*>()) return ParseError::MISSING_COMMAND_ID;
  if (!copyStr(o["commandId"], out.commandId, sizeof(out.commandId)) || !validId(out.commandId)) {
    return ParseError::BAD_COMMAND_ID;
  }

  const char* action = o["action"].as<const char*>();
  if (action == nullptr) return ParseError::UNKNOWN_ACTION;

  if (o["timestamp"].is<const char*>()) {
    if (!parseIso8601(o["timestamp"].as<const char*>(), out.issuedAtS)) return ParseError::BAD_TIMESTAMP;
    out.hasTimestamp = true;
  }
  if (o["expiresAt"].is<const char*>()) {
    if (!parseIso8601(o["expiresAt"].as<const char*>(), out.expiresAtS)) return ParseError::BAD_TIMESTAMP;
    out.hasExpiry = true;
  }

  if (strcmp(action, "OTA_UPDATE") == 0) {
    out.kind = CommandKind::OTA_UPDATE;
    JsonObjectConst ota = o["ota"].as<JsonObjectConst>();
    if (ota.isNull()) return ParseError::BAD_OTA;
    if (!copyStr(ota["version"], out.ota.version, sizeof(out.ota.version)) ||
        !copyStr(ota["url"], out.ota.url, sizeof(out.ota.url)) ||
        !copyStr(ota["sha256"], out.ota.sha256Hex, sizeof(out.ota.sha256Hex)) ||
        !copyStr(ota["signature"], out.ota.signatureB64, sizeof(out.ota.signatureB64)) ||
        !copyStr(ota["hwRev"], out.ota.hwRev, sizeof(out.ota.hwRev)) || !ota["size"].is<uint32_t>()) {
      return ParseError::BAD_OTA;
    }
    out.ota.size = ota["size"].as<uint32_t>();
    out.ota.stopMotorIfRunning = ota["stopMotorIfRunning"] | true;
    return ParseError::OK;
  }

  out.kind = CommandKind::MOTOR;
  if (!parseCommandAction(action, out.cmd.action)) return ParseError::UNKNOWN_ACTION;
  if (o["mode"].is<const char*>()) {
    if (!parseControlMode(o["mode"].as<const char*>(), out.cmd.mode)) return ParseError::BAD_MODE;
    out.cmd.modeSpecified = true;
  } else if (!o["mode"].isNull()) {
    return ParseError::BAD_MODE;
  }
  if (out.cmd.action == CommandAction::SET_MODE && !out.cmd.modeSpecified) return ParseError::BAD_MODE;
  return ParseError::OK;
}

CommandStatus checkFreshness(const ParsedCommand& c, bool timeValid, int64_t nowS, bool requireExpiry) {
  if (requireExpiry) {
    if (!c.hasTimestamp || !c.hasExpiry) return CommandStatus::INVALID;
    // Without a synchronised clock the device cannot prove freshness, so a
    // remote command is refused rather than possibly replayed.
    if (!timeValid) return CommandStatus::EXPIRED;
  }
  if (c.hasTimestamp && c.hasExpiry) {
    if (c.expiresAtS <= c.issuedAtS) return CommandStatus::INVALID;
    if (c.expiresAtS - c.issuedAtS > kMaxCommandLifetimeS) return CommandStatus::INVALID;
  }
  if (timeValid) {
    if (c.hasExpiry && nowS >= c.expiresAtS) return CommandStatus::EXPIRED;
    if (c.hasTimestamp && c.issuedAtS > nowS + kMaxClockSkewS) return CommandStatus::INVALID;
    if (c.hasTimestamp && nowS - c.issuedAtS > kMaxCommandLifetimeS) return CommandStatus::EXPIRED;
  }
  return CommandStatus::ACCEPTED;
}

bool CommandDeduplicator::lookup(const char* id, CommandOutcome& previous) const {
  for (uint8_t i = 0; i < count_; ++i) {
    if (strncmp(entries_[i].id, id, kCommandIdMax) == 0) {
      previous = entries_[i].outcome;
      return true;
    }
  }
  return false;
}

void CommandDeduplicator::remember(const char* id, const CommandOutcome& outcome) {
  Entry& e = entries_[head_];
  strncpy(e.id, id, kCommandIdMax);
  e.id[kCommandIdMax] = '\0';
  e.outcome = outcome;
  head_ = static_cast<uint8_t>((head_ + 1) % kSize);
  if (count_ < kSize) ++count_;
}

}  // namespace wintek
