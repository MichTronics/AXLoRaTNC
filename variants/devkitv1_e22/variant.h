#pragma once

#include "pins.h"
#include "radio_config.h"

namespace axlora::variant {

static constexpr const char* NAME = "devkitv1_e22";
static constexpr const char* BOARD_NAME = "ESP32 DevKit V1";
static constexpr const char* RADIO_MODULE = "EBYTE E22-900M30S/M33S";
static constexpr bool HAS_OLED = true;
static constexpr bool HAS_GPS = false;
static constexpr bool HAS_BLE = false;
static constexpr bool HAS_DS18B20 = false;
static constexpr bool HAS_AM2302 = false;
static constexpr bool HAS_VBAT = false;
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr const char* DEFAULT_CALLSIGN = "N0CALL";

}
