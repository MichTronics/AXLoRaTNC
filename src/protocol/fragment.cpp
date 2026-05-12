#include "fragment.h"
#include <Arduino.h>
#include <string.h>
#include "util/timer.h"

namespace axlora::protocol {

uint8_t Fragmenter::makeChatPackets(const char* source, const char* destination, const uint8_t* data, size_t len,
                                    uint16_t messageId, Packet* outPackets, uint8_t packetCapacity) {
  if (data == nullptr || outPackets == nullptr || len == 0) {
    return 0;
  }
  const uint8_t total = static_cast<uint8_t>((len + MAX_FRAGMENT_PAYLOAD - 1) / MAX_FRAGMENT_PAYLOAD);
  if (total == 0 || total > MAX_FRAGMENTS || total > packetCapacity) {
    return 0;
  }
  for (uint8_t i = 0; i < total; ++i) {
    Packet& packet = outPackets[i];
    packet = Packet{};
    packet.type = PacketType::Chat;
    packet.messageId = messageId;
    setCallsign(packet.source, source);
    setCallsign(packet.destination, destination);
    packet.fragmentIndex = i;
    packet.fragmentTotal = total;
    packet.ttl = DEFAULT_TTL;
    packet.flags = FLAG_ACK_REQUEST;
    if (total > 1) {
      packet.flags |= FLAG_FRAGMENTED;
    }
    const size_t offset = static_cast<size_t>(i) * MAX_FRAGMENT_PAYLOAD;
    const size_t remaining = len - offset;
    packet.payloadLen = static_cast<uint8_t>(remaining > MAX_FRAGMENT_PAYLOAD ? MAX_FRAGMENT_PAYLOAD : remaining);
    memcpy(packet.payload, &data[offset], packet.payloadLen);
  }
  return total;
}

bool Reassembler::accept(const Packet& packet, uint8_t* out, size_t outCap, size_t& outLen) {
  if (packet.fragmentTotal <= 1) {
    if (packet.payloadLen > outCap) {
      return false;
    }
    memcpy(out, packet.payload, packet.payloadLen);
    outLen = packet.payloadLen;
    return true;
  }
  if (packet.fragmentTotal > MAX_FRAGMENTS || packet.fragmentIndex >= packet.fragmentTotal) {
    return false;
  }

  Slot* slot = nullptr;
  for (Slot& candidate : slots_) {
    if (candidate.active && candidate.messageId == packet.messageId && callsignEquals(candidate.source, packet.source)) {
      slot = &candidate;
      break;
    }
  }
  if (slot == nullptr) {
    for (Slot& candidate : slots_) {
      if (!candidate.active) {
        slot = &candidate;
        break;
      }
    }
  }
  if (slot == nullptr) {
    slot = &slots_[0];
  }

  if (!slot->active) {
    *slot = Slot{};
    slot->active = true;
    setCallsign(slot->source, packet.source);
    slot->messageId = packet.messageId;
    slot->total = packet.fragmentTotal;
  }

  memcpy(slot->data[packet.fragmentIndex], packet.payload, packet.payloadLen);
  slot->lengths[packet.fragmentIndex] = packet.payloadLen;
  slot->receivedMask |= static_cast<uint8_t>(1U << packet.fragmentIndex);
  slot->lastUpdate = util::nowMs();

  const uint8_t completeMask = static_cast<uint8_t>((1U << slot->total) - 1U);
  if ((slot->receivedMask & completeMask) != completeMask) {
    return false;
  }

  size_t assembled = 0;
  for (uint8_t i = 0; i < slot->total; ++i) {
    if (assembled + slot->lengths[i] > outCap) {
      slot->active = false;
      return false;
    }
    memcpy(&out[assembled], slot->data[i], slot->lengths[i]);
    assembled += slot->lengths[i];
  }
  slot->active = false;
  outLen = assembled;
  return true;
}

}

