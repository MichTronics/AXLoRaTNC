#pragma once

#include <stddef.h>
#include <stdint.h>

namespace axlora::util {

uint16_t crc16Ccitt(const uint8_t* data, size_t len, uint16_t seed = 0xFFFF);

}

