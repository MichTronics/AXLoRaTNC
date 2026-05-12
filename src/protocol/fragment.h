#pragma once

#include <stddef.h>
#include <stdint.h>
#include "axlora_config.h"
#include "packet.h"

namespace axlora::protocol {

class Fragmenter {
 public:
  uint8_t makeChatPackets(const char* source, const char* destination, const uint8_t* data, size_t len,
                          uint16_t messageId, Packet* outPackets, uint8_t packetCapacity);
};

class Reassembler {
 public:
  bool accept(const Packet& packet, uint8_t* out, size_t outCap, size_t& outLen);

 private:
  struct Slot {
    bool active = false;
    char source[CALLSIGN_FIELD_LEN + 1]{};
    uint16_t messageId = 0;
    uint8_t total = 0;
    uint8_t receivedMask = 0;
    uint8_t lengths[MAX_FRAGMENTS]{};
    uint8_t data[MAX_FRAGMENTS][MAX_FRAGMENT_PAYLOAD]{};
    uint32_t lastUpdate = 0;
  };

  Slot slots_[REASSEMBLY_SLOTS]{};
};

}

