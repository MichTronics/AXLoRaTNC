#pragma once

#include "pins.h"
#include "radio_config.h"

namespace axlora::variant {

static constexpr const char* NAME         = "lilygo_t3_v161";
static constexpr const char* BOARD_NAME   = "LilyGo TTGO T3 v1.6.1";
static constexpr const char* RADIO_MODULE = "SX1276";
static constexpr bool HAS_OLED    = true;
static constexpr bool HAS_GPS     = false;
static constexpr bool HAS_BLE     = true;
static constexpr bool HAS_DS18B20 = false;
static constexpr bool HAS_AM2302  = false;
static constexpr bool HAS_VBAT    = true;   // Battery ADC on GPIO 35
static constexpr uint32_t SERIAL_BAUD          = 115200;
static constexpr const char* DEFAULT_CALLSIGN  = "N0CALL";

}
