#include "dedup.h"
#include <string.h>

namespace axlora::protocol {

bool DedupTable::seen(const Packet& packet) {
  for (Entry& entry : entries_) {
    if (entry.used && entry.messageId == packet.messageId &&
        entry.fragmentIndex == packet.fragmentIndex &&
        callsignEquals(entry.source, packet.source)) {
      ++duplicateCount_;
      return true;
    }
  }
  return false;
}

void DedupTable::remember(const Packet& packet, uint32_t now) {
  Entry& entry = entries_[next_];
  entry.used = true;
  memcpy(entry.source, packet.source, CALLSIGN_FIELD_LEN + 1);
  entry.messageId = packet.messageId;
  entry.fragmentIndex = packet.fragmentIndex;
  entry.lastSeen = now;
  next_ = static_cast<uint8_t>((next_ + 1) % DEDUP_ENTRIES);
}

}

