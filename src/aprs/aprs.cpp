#include "aprs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace axlora::aprs {

namespace {

// Parse "DDMM.MM[NS]" (8 chars) into decimal degrees.
bool parseLat(const char* s, double& out) {
  if (s == nullptr) return false;
  const double deg = (s[0] - '0') * 10.0 + (s[1] - '0');
  const double min = (s[2] - '0') * 10.0 + (s[3] - '0')
                   + (s[4] - '0') * 0.1 + (s[5] - '0') * 0.01;
  out = deg + min / 60.0;
  if (s[7] == 'S' || s[7] == 's') out = -out;
  return true;
}

// Parse "DDDMM.MM[EW]" (9 chars) into decimal degrees.
bool parseLon(const char* s, double& out) {
  if (s == nullptr) return false;
  const double deg = (s[0] - '0') * 100.0 + (s[1] - '0') * 10.0 + (s[2] - '0');
  const double min = (s[3] - '0') * 10.0 + (s[4] - '0')
                   + (s[5] - '0') * 0.1 + (s[6] - '0') * 0.01;
  out = deg + min / 60.0;
  if (s[8] == 'W' || s[8] == 'w') out = -out;
  return true;
}

// Decode a 4-char base91 value: chars must be in range 33–124.
bool base91Decode(const char* s, uint32_t& out) {
  for (int i = 0; i < 4; ++i) {
    if (s[i] < 33 || s[i] > 124) return false;
  }
  out = static_cast<uint32_t>((s[0] - 33) * 91 * 91 * 91 +
                               (s[1] - 33) * 91 * 91 +
                               (s[2] - 33) * 91 +
                               (s[3] - 33));
  return true;
}

// Parse compressed position: symtable(1) + latB91(4) + lonB91(4) + symcode(1) + cs(3) + type(1)
// Returns true and fills lat/lon on success.
bool tryParseCompressedPosition(const char* s, size_t slen, double& lat, double& lon) {
  if (slen < 13) return false;
  // s[0] = symbol table ('/' or '\')
  // s[1..4] = compressed lat (base91)
  // s[5..8] = compressed lon (base91)
  uint32_t latVal = 0, lonVal = 0;
  if (!base91Decode(s + 1, latVal) || !base91Decode(s + 5, lonVal)) return false;
  lat =  90.0 - static_cast<double>(latVal)  / 380926.0;
  lon = -180.0 + static_cast<double>(lonVal) / 190463.0;
  return true;
}

// Detect if the position data starting at s is in compressed format.
// Compressed: s[0] is '/' or '\' (symbol table), s[1] is a base91 char (33–124, not a digit).
static bool isCompressedPos(const char* s, size_t slen) {
  if (slen < 13) return false;
  if (s[0] != '/' && s[0] != '\\') return false;
  // Uncompressed lat starts with digits; compressed lat bytes are 33–124 and include non-digits
  return (s[1] < '0' || s[1] > '9');
}

// Attempt to parse position (compressed or uncompressed) starting at s.
bool tryParsePosition(const char* s, size_t slen, char* summary, size_t cap) {
  double lat = 0.0, lon = 0.0;
  if (isCompressedPos(s, slen)) {
    if (!tryParseCompressedPosition(s, slen, lat, lon)) {
      snprintf(summary, cap, "POS(comp)");
      return true;
    }
    const char* comment = slen > 13 ? s + 13 : "";
    const int commentLen = static_cast<int>(slen > 13 ? slen - 13 : 0);
    if (commentLen > 0) {
      snprintf(summary, cap, "POS %.4f %.4f  %.48s",
               static_cast<double>(lat), static_cast<double>(lon), comment);
    } else {
      snprintf(summary, cap, "POS %.4f %.4f", static_cast<double>(lat), static_cast<double>(lon));
    }
    return true;
  }
  // Uncompressed: DDMM.MM[NS] symtable DDDMM.MM[EW] symcode ...
  if (slen < 19) return false;
  char latBuf[9]{}, lonBuf[10]{};
  memcpy(latBuf, s, 8);
  memcpy(lonBuf, s + 9, 9);
  if (!parseLat(latBuf, lat) || !parseLon(lonBuf, lon)) return false;
  const char* comment = slen > 19 ? s + 19 : "";
  const int commentLen = static_cast<int>(slen > 19 ? slen - 19 : 0);
  if (commentLen > 0) {
    snprintf(summary, cap, "POS %.4f %.4f  %.48s",
             static_cast<double>(lat), static_cast<double>(lon), comment);
  } else {
    snprintf(summary, cap, "POS %.4f %.4f", static_cast<double>(lat), static_cast<double>(lon));
  }
  return true;
}

}  // namespace

bool parse(const uint8_t* info, size_t len, char* summary, size_t cap) {
  if (info == nullptr || len == 0 || summary == nullptr || cap == 0) return false;
  summary[0] = '\0';
  const char type = static_cast<char>(info[0]);
  const char* s   = reinterpret_cast<const char*>(info + 1);
  const size_t sl = len - 1;

  if (type == '!' || type == '=') {
    if (!tryParsePosition(s, sl, summary, cap)) {
      snprintf(summary, cap, "POS");
    }
    return true;
  }

  if (type == '/' || type == '@') {
    // 7-char timestamp before position
    if (sl < 8) { snprintf(summary, cap, "POS(ts)"); return true; }
    if (!tryParsePosition(s + 7, sl - 7, summary, cap)) {
      snprintf(summary, cap, "POS(ts)");
    }
    return true;
  }

  if (type == ':') {
    // Message — 9-char padded destination + ':' + body
    char dest[10]{};
    const size_t copyLen = sl < 9 ? sl : 9;
    memcpy(dest, s, copyLen);
    for (int i = 8; i >= 0 && dest[i] == ' '; --i) dest[i] = '\0';
    const char* body = (sl > 10) ? s + 10 : "";
    const int bodyLen = static_cast<int>(sl > 10 ? sl - 10 : 0);
    snprintf(summary, cap, "MSG>%s: %.*s", dest, bodyLen > 40 ? 40 : bodyLen, body);
    return true;
  }

  if (type == '>') {
    const int statusLen = static_cast<int>(sl > 60 ? 60 : sl);
    snprintf(summary, cap, "STATUS: %.*s", statusLen, s);
    return true;
  }

  // Object: ';' + 9-char name + '*'(live)/'_'(killed) + timestamp(7) + position
  if (type == ';') {
    char name[10]{};
    const size_t nLen = sl < 9 ? sl : 9;
    memcpy(name, s, nLen);
    for (int i = 8; i >= 0 && name[i] == ' '; --i) name[i] = '\0';
    const bool live = (sl >= 10 && s[9] == '*');
    if (sl >= 17 && tryParsePosition(s + 17, sl - 17, summary, cap)) {
      char tmp[80]{};
      snprintf(tmp, sizeof(tmp), "OBJ %s(%s) %s", name, live ? "live" : "killed", summary);
      snprintf(summary, cap, "%s", tmp);
    } else {
      snprintf(summary, cap, "OBJ %s(%s)", name, live ? "live" : "killed");
    }
    return true;
  }

  // Item: ')' + 3-9 char name ending with '!'(live)/'_'(killed) + position
  if (type == ')') {
    char name[10]{};
    size_t nameEnd = 0;
    bool live = true;
    for (size_t i = 0; i < sl && i < 9; ++i) {
      if (s[i] == '!' || s[i] == '_') { live = (s[i] == '!'); nameEnd = i; break; }
      name[i] = s[i];
    }
    if (nameEnd >= 3 && sl > nameEnd + 1 &&
        tryParsePosition(s + nameEnd + 1, sl - nameEnd - 1, summary, cap)) {
      char tmp[80]{};
      snprintf(tmp, sizeof(tmp), "ITEM %s(%s) %s", name, live ? "live" : "killed", summary);
      snprintf(summary, cap, "%s", tmp);
    } else {
      snprintf(summary, cap, "ITEM %s", name);
    }
    return true;
  }

  if (type == '_') {
    snprintf(summary, cap, "WEATHER");
    return true;
  }

  if (type == 'T') {
    snprintf(summary, cap, "TELEMETRY");
    return true;
  }

  return false;
}

bool parseMessage(const uint8_t* info, size_t len, AprsMessage& out) {
  if (info == nullptr || len < 11) return false;
  if (static_cast<char>(info[0]) != ':') return false;
  out = AprsMessage{};
  const char* s = reinterpret_cast<const char*>(info + 1);
  const size_t sl = len - 1;
  // 9-char padded addressee
  size_t addrLen = sl < 9 ? sl : 9;
  memcpy(out.addressee, s, addrLen);
  for (int i = 8; i >= 0 && out.addressee[i] == ' '; --i) out.addressee[i] = '\0';
  // Separator ':'
  if (sl < 10 || s[9] != ':') return false;
  // Message body
  const char* body = s + 10;
  const size_t bodyLen = sl - 10;
  size_t copyLen = bodyLen < sizeof(out.text) - 1 ? bodyLen : sizeof(out.text) - 1;
  // Check for ack/rej
  if (bodyLen >= 3 && strncmp(body, "ack", 3) == 0) {
    out.isAck = true;
    const char* num = body + 3;
    size_t nLen = bodyLen - 3;
    if (nLen >= sizeof(out.msgNum)) nLen = sizeof(out.msgNum) - 1;
    memcpy(out.msgNum, num, nLen);
    return true;
  }
  if (bodyLen >= 3 && strncmp(body, "rej", 3) == 0) {
    out.isRej = true;
    const char* num = body + 3;
    size_t nLen = bodyLen - 3;
    if (nLen >= sizeof(out.msgNum)) nLen = sizeof(out.msgNum) - 1;
    memcpy(out.msgNum, num, nLen);
    return true;
  }
  // Find optional message number '{NNN}'
  const char* brace = nullptr;
  for (size_t i = 0; i < bodyLen; ++i) {
    if (body[i] == '{') { brace = body + i; break; }
  }
  if (brace != nullptr) {
    copyLen = static_cast<size_t>(brace - body);
    if (copyLen >= sizeof(out.text)) copyLen = sizeof(out.text) - 1;
    const char* num = brace + 1;
    const size_t numLen = bodyLen - static_cast<size_t>(brace - body) - 1;
    size_t nCopy = numLen < sizeof(out.msgNum) - 1 ? numLen : sizeof(out.msgNum) - 1;
    memcpy(out.msgNum, num, nCopy);
  }
  memcpy(out.text, body, copyLen);
  return true;
}

bool encodeMessageAck(const char* addressee, const char* msgNum,
                      char* out, size_t cap) {
  if (out == nullptr || cap < 16 || addressee == nullptr || msgNum == nullptr) return false;
  // Format: `:ADDRESSEE:ack{msgNum}` where addressee is right-padded to 9 chars
  snprintf(out, cap, ":%-9s:ack%s", addressee, msgNum);
  return true;
}

bool encodePosition(double lat, double lon,
                    const char* symbol, const char* comment,
                    char* out, size_t cap) {
  if (out == nullptr || cap < 22) return false;
  const char latH = lat >= 0.0 ? 'N' : 'S';
  const char lonH = lon >= 0.0 ? 'E' : 'W';
  lat = fabs(lat);
  lon = fabs(lon);
  const int latDeg = static_cast<int>(lat);
  const int lonDeg = static_cast<int>(lon);
  const double latMin = (lat - latDeg) * 60.0;
  const double lonMin = (lon - lonDeg) * 60.0;
  const char symTable = (symbol && symbol[0]) ? symbol[0] : '/';
  const char symCode  = (symbol && symbol[1]) ? symbol[1] : '>';
  snprintf(out, cap, "!%02d%05.2f%c%c%03d%05.2f%c%c%s",
           latDeg, latMin, latH, symTable,
           lonDeg, lonMin, lonH, symCode,
           comment ? comment : "");
  return true;
}

}  // namespace axlora::aprs
