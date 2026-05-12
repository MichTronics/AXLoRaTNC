#include "ax25_addr.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace axlora::ax25 {

bool parseAddress(const char* text, Address& out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  out = Address{};
  uint8_t callLen = 0;
  const char* p = text;
  while (*p != '\0' && *p != '-' && callLen < CALLSIGN_LEN) {
    out.callsign[callLen++] = static_cast<char>(toupper(static_cast<unsigned char>(*p++)));
  }
  if (callLen == 0) {
    return false;
  }
  if (*p != '\0' && *p != '-') {
    return false;
  }
  if (*p == '-') {
    const int ssid = atoi(p + 1);
    if (ssid < 0 || ssid > 15) {
      return false;
    }
    out.ssid = static_cast<uint8_t>(ssid);
  }
  return true;
}

void formatAddress(const Address& addr, char* out, size_t outCap) {
  if (out == nullptr || outCap == 0) {
    return;
  }
  if (addr.ssid == 0) {
    snprintf(out, outCap, "%s", addr.callsign);
  } else {
    snprintf(out, outCap, "%s-%u", addr.callsign, addr.ssid);
  }
}

bool encodeAddress(const Address& addr, bool last, uint8_t* out) {
  if (out == nullptr || addr.callsign[0] == '\0') {
    return false;
  }
  for (uint8_t i = 0; i < CALLSIGN_LEN; ++i) {
    const char c = addr.callsign[i] == '\0' ? ' ' : static_cast<char>(toupper(static_cast<unsigned char>(addr.callsign[i])));
    out[i] = static_cast<uint8_t>(c << 1);
  }
  out[6] = static_cast<uint8_t>(0x60 | ((addr.ssid & 0x0F) << 1));
  if (addr.repeated) {
    out[6] |= 0x80;
  }
  if (last) {
    out[6] |= 0x01;
  }
  return true;
}

bool decodeAddress(const uint8_t* in, Address& out, bool& last) {
  if (in == nullptr) {
    return false;
  }
  out = Address{};
  for (uint8_t i = 0; i < CALLSIGN_LEN; ++i) {
    const char c = static_cast<char>((in[i] >> 1) & 0x7F);
    if (c != ' ') {
      const size_t len = strlen(out.callsign);
      if (len < CALLSIGN_LEN) {
        out.callsign[len] = c;
      }
    }
  }
  out.ssid = static_cast<uint8_t>((in[6] >> 1) & 0x0F);
  out.repeated = (in[6] & 0x80) != 0;
  last = (in[6] & 0x01) != 0;
  return out.callsign[0] != '\0';
}

bool addressEquals(const Address& left, const Address& right) {
  return strncmp(left.callsign, right.callsign, CALLSIGN_LEN) == 0 && left.ssid == right.ssid;
}

}
