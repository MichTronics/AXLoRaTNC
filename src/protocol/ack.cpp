#include "ack.h"
#include <string.h>

namespace axlora::protocol {

bool AckTracker::track(const Packet& packet, uint32_t now) {
  for (Pending& item : pending_) {
    if (!item.active) {
      item.active = true;
      item.packet = packet;
      item.retries = 0;
      item.lastTx = now;
      return true;
    }
  }
  return false;
}

bool AckTracker::acknowledge(const Packet& packet) {
  if (packet.type != PacketType::Ack || packet.payloadLen < 3) {
    return false;
  }
  const uint16_t ackedId = static_cast<uint16_t>((packet.payload[0] << 8) | packet.payload[1]);
  const uint8_t ackedFragment = packet.payload[2];
  bool matched = false;
  for (Pending& item : pending_) {
    if (item.active && item.packet.messageId == ackedId &&
        item.packet.fragmentIndex == ackedFragment &&
        callsignEquals(item.packet.destination, packet.source)) {
      item.active = false;
      matched = true;
    }
  }
  return matched;
}

bool AckTracker::dueForRetry(uint32_t now, Packet& packetOut) {
  for (Pending& item : pending_) {
    if (!item.active || static_cast<uint32_t>(now - item.lastTx) < ACK_TIMEOUT_MS) {
      continue;
    }
    if (item.retries >= MAX_RETRIES) {
      item.active = false;
      ++failCount_;
      return false;
    }
    item.lastTx = now;
    ++item.retries;
    ++retryCount_;
    packetOut = item.packet;
    return true;
  }
  return false;
}

void AckTracker::clear(uint16_t messageId, const char* destination) {
  for (Pending& item : pending_) {
    if (item.active && item.packet.messageId == messageId && callsignEquals(item.packet.destination, destination)) {
      item.active = false;
    }
  }
}

void makeAck(const Packet& received, const char* localCallsign, Packet& ackOut) {
  ackOut = Packet{};
  ackOut.type = PacketType::Ack;
  ackOut.messageId = received.messageId;
  setCallsign(ackOut.source, localCallsign);
  setCallsign(ackOut.destination, received.source);
  ackOut.fragmentIndex = received.fragmentIndex;
  ackOut.ttl = DEFAULT_TTL;
  ackOut.fragmentTotal = 1;
  ackOut.payloadLen = 3;
  ackOut.payload[0] = static_cast<uint8_t>(received.messageId >> 8);
  ackOut.payload[1] = static_cast<uint8_t>(received.messageId & 0xFF);
  ackOut.payload[2] = received.fragmentIndex;
}

}
