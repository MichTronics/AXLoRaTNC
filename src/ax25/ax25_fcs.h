#pragma once

#include <stddef.h>
#include <stdint.h>

namespace axlora::ax25 {

uint16_t fcs(const uint8_t* data, size_t len);
bool checkFcs(const uint8_t* data, size_t lenWithFcs);
bool appendFcs(const uint8_t* data, size_t len, uint8_t* out, size_t outCap, size_t& outLen);

}
