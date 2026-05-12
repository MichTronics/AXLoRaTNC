#pragma once

#include <stdint.h>
#include "axlora_config.h"
#include "packet.h"

namespace axlora::protocol {

class AckTracker {
 public:
  bool track(const Packet& packet, uint32_t now);
  bool acknowledge(const Packet& packet);
  bool dueForRetry(uint32_t now, Packet& packetOut);
  void clear(uint16_t messageId, const char* destination);
  uint32_t retryCount() const { return retryCount_; }
  uint32_t failCount() const { return failCount_; }

 private:
  struct Pending {
    bool active = false;
    Packet packet{};
    uint8_t retries = 0;
    uint32_t lastTx = 0;
  };

  Pending pending_[ACK_TRACKERS]{};
  uint32_t retryCount_ = 0;
  uint32_t failCount_ = 0;
};

void makeAck(const Packet& received, const char* localCallsign, Packet& ackOut);

}

