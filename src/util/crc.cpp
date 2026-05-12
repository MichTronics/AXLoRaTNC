#include "crc.h"

namespace axlora::util {

uint16_t crc16Ccitt(const uint8_t* data, size_t len, uint16_t seed) {
  uint16_t crc = seed;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) ? static_cast<uint16_t>((crc << 1) ^ 0x1021U) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

}

