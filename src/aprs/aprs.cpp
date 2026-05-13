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

// Attempt to parse uncompressed position starting at s.
// s points just after the type byte (and optional timestamp).
bool tryParsePosition(const char* s, size_t slen, char* summary, size_t cap) {
  if (slen < 19) return false;  // need DDMM.MM[NS] + sym + DDDMM.MM[EW] + sym
  char latBuf[9]{}, lonBuf[10]{};
  memcpy(latBuf, s, 8);
  memcpy(lonBuf, s + 9, 9);
  double lat = 0.0, lon = 0.0;
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
    // Message — 9-char padded destination address
    char dest[10]{};
    const size_t copyLen = sl < 9 ? sl : 9;
    memcpy(dest, s, copyLen);
    // Strip trailing spaces from dest
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
