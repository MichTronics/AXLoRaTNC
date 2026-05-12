#pragma once

#include <stdint.h>
#include "axlora_config.h"
#include "protocol/packet.h"

namespace axlora::mesh {

class NeighborTable {
 public:
  void observe(const protocol::Packet& packet, float rssi, float snr, uint32_t now);
  void print() const;
  uint8_t count() const;

 private:
  struct Neighbor {
    bool used = false;
    char callsign[protocol::CALLSIGN_FIELD_LEN + 1]{};
    float lastRssi = 0.0f;
    float lastSnr = 0.0f;
    uint32_t lastSeen = 0;
    uint16_t heard = 0;
  };

  Neighbor entries_[NEIGHBOR_ENTRIES]{};
  uint8_t next_ = 0;
};

}

