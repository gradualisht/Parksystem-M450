#include "ParkProtocol.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

std::string jsonEscape(const std::string &text) {
  std::string out;
  for (size_t i = 0; i < text.size(); i++) {
    unsigned char c = (unsigned char)text[i];
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
      out += buf;
    } else {
      out += (char)c;
    }
  }
  return out;
}

// Position direkt hinter "key": (Leerzeichen uebersprungen) oder npos.
static size_t valueStart(const std::string &body, const std::string &key) {
  std::string needle = "\"" + key + "\"";
  size_t pos = body.find(needle);
  if (pos == std::string::npos) {
    return std::string::npos;
  }
  pos += needle.size();
  while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) {
    pos++;
  }
  if (pos >= body.size() || body[pos] != ':') {
    return std::string::npos;
  }
  pos++;
  while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) {
    pos++;
  }
  return pos;
}

std::string jsonGetString(const std::string &body, const std::string &key,
                          const std::string &fallback) {
  size_t pos = valueStart(body, key);
  if (pos == std::string::npos || pos >= body.size() || body[pos] != '"') {
    return fallback;
  }
  pos++;
  std::string out;
  while (pos < body.size() && body[pos] != '"') {
    if (body[pos] == '\\' && pos + 1 < body.size()) {
      pos++;
      char c = body[pos];
      out += (c == 'n') ? '\n' : c;
    } else {
      out += body[pos];
    }
    pos++;
  }
  if (pos >= body.size()) {
    return fallback;  // String nicht abgeschlossen
  }
  return out;
}

long jsonGetInt(const std::string &body, const std::string &key, long fallback) {
  size_t pos = valueStart(body, key);
  if (pos == std::string::npos || pos >= body.size()) {
    return fallback;
  }
  char c = body[pos];
  if (!(c == '-' || (c >= '0' && c <= '9'))) {
    return fallback;
  }
  return strtol(body.c_str() + pos, NULL, 10);
}

std::string urlEncode(const std::string &text) {
  std::string out;
  for (size_t i = 0; i < text.size(); i++) {
    unsigned char c = (unsigned char)text[i];
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 c == '-' || c == '_' || c == '.' || c == '~';
    if (plain) {
      out += (char)c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned)c);
      out += buf;
    }
  }
  return out;
}

std::string pseudonymize(const std::string &id) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < id.size(); i++) {
    hash ^= (unsigned char)id[i];
    hash *= 16777619u;
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "#%04x", (unsigned)(hash & 0xFFFFu));
  return buf;
}

std::string maskIdInLogLine(const std::string &line, const std::string &onlyId) {
  std::string out;
  size_t pos = 0;
  while (pos < line.size()) {
    size_t hit = line.find("id=", pos);
    // Nur ein eigenstaendiges Feld "id=" (Zeilenanfang oder Leerzeichen davor), nicht z. B. "paid="
    while (hit != std::string::npos && hit > 0 && line[hit - 1] != ' ') {
      hit = line.find("id=", hit + 1);
    }
    if (hit == std::string::npos) {
      out += line.substr(pos);
      break;
    }
    size_t valueStart = hit + 3;
    size_t valueEnd = line.find(' ', valueStart);
    if (valueEnd == std::string::npos) {
      valueEnd = line.size();
    }
    std::string value = line.substr(valueStart, valueEnd - valueStart);
    out += line.substr(pos, valueStart - pos);
    out += (value.empty() || (!onlyId.empty() && value != onlyId)) ? value : pseudonymize(value);
    pos = valueEnd;
  }
  return out;
}
