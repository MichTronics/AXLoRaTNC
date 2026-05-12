#pragma once

#if !defined(AXLORA_VARIANT_DEVKITV1_E22) && !defined(AXLORA_VARIANT_HELTEC_V3) && !defined(AXLORA_VARIANT_TBEAM)
#error "Select an AXLoRa hardware variant with a build flag, for example -DAXLORA_VARIANT_DEVKITV1_E22"
#endif

#include "variant.h"

namespace axlora {

static constexpr uint8_t MAX_CALLSIGN_LEN = 10;
static constexpr uint8_t MAX_PAYLOAD_LEN = 255;
static constexpr uint16_t MAX_PACKET_BYTES = 330;
static constexpr uint8_t MAX_FRAGMENT_PAYLOAD = 96;
static constexpr uint8_t MAX_FRAGMENTS = 8;
static constexpr uint8_t DEFAULT_TTL = 4;
static constexpr uint8_t TX_QUEUE_DEPTH = 8;
static constexpr uint8_t RX_QUEUE_DEPTH = 8;
static constexpr uint8_t DEDUP_ENTRIES = 32;
static constexpr uint8_t NEIGHBOR_ENTRIES = 16;
static constexpr uint8_t REASSEMBLY_SLOTS = 4;
static constexpr uint8_t ACK_TRACKERS = 8;
static constexpr uint8_t MAX_RETRIES = 3;
static constexpr uint32_t ACK_TIMEOUT_MS = 2500;
static constexpr uint32_t RELAY_DEFER_MS = 120;
static constexpr uint32_t NEIGHBOR_TTL_MS = 600000;

}
