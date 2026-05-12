#include "packet.h"
#include <string.h>
#include "util/crc.h"

namespace axlora::protocol {
namespace {

static constexpr size_t FIXED_HEADER_LEN = 34;
static constexpr char BROADCAST[] = "CQ";

void writeU16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value >> 8);
  out[1] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t readU16(const uint8_t* in) {
  return static_cast<uint16_t>((static_cast<uint16_t>(in[0]) << 8) | in[1]);
}

}

void setCallsign(char* field, const char* callsign) {
  memset(field, 0, CALLSIGN_FIELD_LEN + 1);
  if (callsign == nullptr) {
    return;
  }
  size_t i = 0;
  for (; i < CALLSIGN_FIELD_LEN && callsign[i] != '\0'; ++i) {
    char c = callsign[i];
    field[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
  }
}

bool callsignEquals(const char* left, const char* right) {
  return strncmp(left, right, CALLSIGN_FIELD_LEN) == 0;
}

bool isBroadcast(const char* callsign) {
  return callsignEquals(callsign, BROADCAST);
}

bool encode(const Packet& packet, uint8_t* out, size_t outCap, size_t& outLen) {
  if (outCap < FIXED_HEADER_LEN + packet.payloadLen) {
    return false;
  }

  out[0] = packet.version;
  out[1] = static_cast<uint8_t>(packet.type);
  writeU16(&out[2], packet.messageId);
  memcpy(&out[4], packet.source, CALLSIGN_FIELD_LEN);
  memcpy(&out[14], packet.destination, CALLSIGN_FIELD_LEN);
  out[24] = packet.fragmentIndex;
  out[25] = packet.fragmentTotal;
  out[26] = packet.ttl;
  out[27] = packet.hopCounter;
  out[28] = packet.flags;
  out[29] = packet.payloadLen;
  uint16_t payloadCrc = util::crc16Ccitt(packet.payload, packet.payloadLen);
  writeU16(&out[30], payloadCrc);
  writeU16(&out[32], 0);
  memcpy(&out[34], packet.payload, packet.payloadLen);
  const uint16_t headerCrc = util::crc16Ccitt(out, FIXED_HEADER_LEN - 2);
  writeU16(&out[32], headerCrc);
  outLen = FIXED_HEADER_LEN + packet.payloadLen;
  return true;
}

bool decode(const uint8_t* data, size_t len, Packet& packet) {
  if (data == nullptr || len < FIXED_HEADER_LEN) {
    return false;
  }
  const uint8_t payloadLen = data[29];
  if (len != FIXED_HEADER_LEN + payloadLen) {
    return false;
  }
  uint8_t headerCopy[FIXED_HEADER_LEN]{};
  memcpy(headerCopy, data, FIXED_HEADER_LEN);
  headerCopy[32] = 0;
  headerCopy[33] = 0;
  if (util::crc16Ccitt(headerCopy, FIXED_HEADER_LEN - 2) != readU16(&data[32])) {
    return false;
  }
  if (util::crc16Ccitt(&data[34], payloadLen) != readU16(&data[30])) {
    return false;
  }

  packet.version = data[0];
  packet.type = static_cast<PacketType>(data[1]);
  packet.messageId = readU16(&data[2]);
  memcpy(packet.source, &data[4], CALLSIGN_FIELD_LEN);
  packet.source[CALLSIGN_FIELD_LEN] = '\0';
  memcpy(packet.destination, &data[14], CALLSIGN_FIELD_LEN);
  packet.destination[CALLSIGN_FIELD_LEN] = '\0';
  packet.fragmentIndex = data[24];
  packet.fragmentTotal = data[25];
  packet.ttl = data[26];
  packet.hopCounter = data[27];
  packet.flags = data[28];
  packet.payloadLen = payloadLen;
  packet.payloadCrc = readU16(&data[30]);
  memcpy(packet.payload, &data[34], payloadLen);
  return packet.version == PROTOCOL_VERSION;
}

const char* packetTypeName(PacketType type) {
  switch (type) {
    case PacketType::Chat: return "CHAT";
    case PacketType::Ack: return "ACK";
    case PacketType::Beacon: return "BEACON";
    case PacketType::Telemetry: return "TELEM";
    default: return "UNKNOWN";
  }
}

}
