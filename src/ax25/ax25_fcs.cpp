#include "ax25_fcs.h"

namespace axlora::ax25 {

uint16_t fcs(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x0001U) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408U) : static_cast<uint16_t>(crc >> 1);
    }
  }
  return static_cast<uint16_t>(~crc);
}

bool checkFcs(const uint8_t* data, size_t lenWithFcs) {
  if (data == nullptr || lenWithFcs < 2) {
    return false;
  }
  const size_t dataLen = lenWithFcs - 2;
  const uint16_t got = static_cast<uint16_t>(data[dataLen] | (static_cast<uint16_t>(data[dataLen + 1]) << 8));
  return got == fcs(data, dataLen);
}

bool appendFcs(const uint8_t* data, size_t len, uint8_t* out, size_t outCap, size_t& outLen) {
  if (data == nullptr || out == nullptr || outCap < len + 2) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    out[i] = data[i];
  }
  const uint16_t crc = fcs(data, len);
  out[len] = static_cast<uint8_t>(crc & 0xFF);
  out[len + 1] = static_cast<uint8_t>(crc >> 8);
  outLen = len + 2;
  return true;
}

}
