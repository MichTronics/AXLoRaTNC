#pragma once

#include "pins.h"
#include "radio_config.h"

namespace axlora::variant {

static constexpr const char* NAME = "tbeam";
static constexpr bool HAS_OLED = true;
static constexpr bool HAS_GPS = true;
static constexpr bool HAS_BLE = true;
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr const char* DEFAULT_CALLSIGN = "N0CALL";

}

