#pragma once

#include <stdint.h>
#include "axlora_config.h"
#include "protocol/ack.h"
#include "protocol/dedup.h"
#include "protocol/fragment.h"
#include "protocol/packet.h"
#include "radio/radio.h"
#include "util/ringbuffer.h"
#include "neighbor.h"
#include "routing.h"

namespace axlora::mesh {

struct Stats {
  uint32_t relayed = 0;
  uint32_t dropped = 0;
  uint32_t delivered = 0;
  uint32_t duplicates = 0;
};

class MeshNode {
 public:
  void begin(const char* callsign);
  void loop();
  bool sendChat(const char* destination, const char* text);
  void printInfo() const;
  void printStats() const;
  void printNeighbors() const;
  NeighborTable& neighbors() { return neighbors_; }
  const char* callsign() const { return callsign_; }
  bool setCallsign(const char* callsign);

 private:
  struct PendingRelay {
    protocol::Packet packet{};
    uint32_t dueMs = 0;
  };

  bool enqueueTx(const protocol::Packet& packet, bool trackAck);
  void serviceRadioRx();
  void serviceTx();
  void serviceAckRetries();
  void handlePacket(const protocol::Packet& packet, float rssi, float snr);
  void deliverChat(const protocol::Packet& packet);
  void maybeRelay(const protocol::Packet& packet);

  char callsign_[protocol::CALLSIGN_FIELD_LEN + 1]{};
  uint16_t nextMessageId_ = 1;
  protocol::DedupTable dedup_;
  protocol::AckTracker ack_;
  protocol::Fragmenter fragmenter_;
  protocol::Reassembler reassembler_;
  NeighborTable neighbors_;
  RoutingTable routing_;
  util::RingBuffer<protocol::Packet, TX_QUEUE_DEPTH> txQueue_;
  util::RingBuffer<PendingRelay, TX_QUEUE_DEPTH> relayQueue_;
  Stats stats_;
};

}
