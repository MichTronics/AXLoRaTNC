#pragma once

#include <stdint.h>

namespace axlora::variant {

enum class RadioType {
  SX1262,
  SX1276,
  FSK
};

static constexpr RadioType RADIO_TYPE = RadioType::SX1276;
static constexpr float DEFAULT_FREQUENCY_MHZ = 869.480f;
static constexpr float DEFAULT_FREQUENCY_CORRECTION_MHZ = 0.050f;
static constexpr float DEFAULT_BANDWIDTH_KHZ = 125.0f;
static constexpr uint8_t DEFAULT_SPREADING_FACTOR = 7;
static constexpr uint8_t DEFAULT_CODING_RATE = 5;
static constexpr uint8_t DEFAULT_SYNC_WORD = 0x12;
static constexpr int8_t DEFAULT_TX_POWER_DBM = 14;
static constexpr int8_t MAX_TX_POWER_DBM = 14;
static constexpr float DEFAULT_CURRENT_LIMIT_MA = 100.0f;
static constexpr uint16_t PREAMBLE_LENGTH = 8;
static constexpr uint32_t DUTY_CYCLE_PPM = 10000;
static constexpr bool USE_DIO2_RF_SWITCH = false;
static constexpr bool HAS_EXTERNAL_RF_SWITCH = false;
static constexpr float TCXO_VOLTAGE = 0.0f;
static constexpr uint32_t SENSOR_TX_INTERVAL_MS = 60000;
static constexpr uint32_t SENSOR_PACKET_INTERVAL_MS = 2000;

}
