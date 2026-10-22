#include "ParkProtocol.h"

#include <stdio.h>
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
