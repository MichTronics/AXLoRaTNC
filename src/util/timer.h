#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace axlora::util {

inline bool elapsed(uint32_t now, uint32_t since, uint32_t intervalMs) {
  return static_cast<uint32_t>(now - since) >= intervalMs;
}

inline uint32_t nowMs() {
  return millis();
}

}

