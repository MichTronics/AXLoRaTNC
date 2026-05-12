#include "neighbor.h"
#include <Arduino.h>
#include <string.h>
#include "util/timer.h"

namespace axlora::mesh {

void NeighborTable::observe(const protocol::Packet& packet, float rssi, float snr, uint32_t now) {
  Neighbor* slot = nullptr;
  for (Neighbor& entry : entries_) {
    if (entry.used && protocol::callsignEquals(entry.callsign, packet.source)) {
      slot = &entry;
      break;
    }
  }
  if (slot == nullptr) {
    slot = &entries_[next_];
    next_ = static_cast<uint8_t>((next_ + 1) % NEIGHBOR_ENTRIES);
    *slot = Neighbor{};
    slot->used = true;
    protocol::setCallsign(slot->callsign, packet.source);
  }
  slot->lastRssi = rssi;
  slot->lastSnr = snr;
  slot->lastSeen = now;
  ++slot->heard;
}

uint8_t NeighborTable::count() const {
  uint8_t total = 0;
  const uint32_t now = util::nowMs();
  for (const Neighbor& entry : entries_) {
    if (entry.used && !util::elapsed(now, entry.lastSeen, NEIGHBOR_TTL_MS)) {
      ++total;
    }
  }
  return total;
}

void NeighborTable::print() const {
  const uint32_t now = util::nowMs();
  Serial.println("Callsign    RSSI    SNR    Heard    AgeMs");
  for (const Neighbor& entry : entries_) {
    if (!entry.used || util::elapsed(now, entry.lastSeen, NEIGHBOR_TTL_MS)) {
      continue;
    }
    Serial.printf("%-10s  %6.1f  %5.1f  %5u  %7lu\n",
                  entry.callsign,
                  static_cast<double>(entry.lastRssi),
                  static_cast<double>(entry.lastSnr),
                  entry.heard,
                  static_cast<unsigned long>(now - entry.lastSeen));
  }
}

}

