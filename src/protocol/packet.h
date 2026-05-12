#pragma once

#include <stddef.h>
#include <stdint.h>
#include "axlora_config.h"

namespace axlora::protocol {

static constexpr uint8_t PROTOCOL_VERSION = 1;
static constexpr uint8_t CALLSIGN_FIELD_LEN = 10;
static constexpr uint8_t FLAG_ACK_REQUEST = 0x01;
static constexpr uint8_t FLAG_RELAYED = 0x02;
static constexpr uint8_t FLAG_FRAGMENTED = 0x04;

enum class PacketType : uint8_t {
  Chat = 1,
  Ack = 2,
  Beacon = 3,
  Telemetry = 4,
};

struct Packet {
  uint8_t version = PROTOCOL_VERSION;
  PacketType type = PacketType::Chat;
  uint16_t messageId = 0;
  char source[CALLSIGN_FIELD_LEN + 1]{};
  char destination[CALLSIGN_FIELD_LEN + 1]{};
  uint8_t fragmentIndex = 0;
  uint8_t fragmentTotal = 1;
  uint8_t ttl = DEFAULT_TTL;
  uint8_t hopCounter = 0;
  uint8_t flags = 0;
  uint8_t payloadLen = 0;
  uint8_t payload[MAX_PAYLOAD_LEN]{};
  uint16_t payloadCrc = 0;
};

bool encode(const Packet& packet, uint8_t* out, size_t outCap, size_t& outLen);
bool decode(const uint8_t* data, size_t len, Packet& packet);
void setCallsign(char* field, const char* callsign);
bool callsignEquals(const char* left, const char* right);
bool isBroadcast(const char* callsign);
const char* packetTypeName(PacketType type);

}

