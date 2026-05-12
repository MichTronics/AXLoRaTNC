#pragma once

#include <stdint.h>
#include "axlora_config.h"
#include "packet.h"

namespace axlora::protocol {

class DedupTable {
 public:
  bool seen(const Packet& packet);
  void remember(const Packet& packet, uint32_t now);
  uint32_t duplicateCount() const { return duplicateCount_; }

 private:
  struct Entry {
    bool used = false;
    char source[CALLSIGN_FIELD_LEN + 1]{};
    uint16_t messageId = 0;
    uint8_t fragmentIndex = 0;
    uint32_t lastSeen = 0;
  };

  Entry entries_[DEDUP_ENTRIES]{};
  uint8_t next_ = 0;
  uint32_t duplicateCount_ = 0;
};

}

