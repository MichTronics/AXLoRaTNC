#pragma once

#include <stddef.h>
#include <stdint.h>

namespace axlora::ax25 {

static constexpr uint8_t CALLSIGN_LEN = 6;
static constexpr uint8_t MAX_REPEATERS = 6;
static constexpr uint8_t MAX_ADDRS = 2 + MAX_REPEATERS;

struct Address {
  char callsign[CALLSIGN_LEN + 1]{};
  uint8_t ssid = 0;
  bool repeated = false;
};

bool parseAddress(const char* text, Address& out);
void formatAddress(const Address& addr, char* out, size_t outCap);
bool encodeAddress(const Address& addr, bool last, uint8_t* out);
bool decodeAddress(const uint8_t* in, Address& out, bool& last);
bool addressEquals(const Address& left, const Address& right);

}
