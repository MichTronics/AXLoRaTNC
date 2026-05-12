#pragma once

#include "protocol/packet.h"

namespace axlora::mesh {

class RoutingTable {
 public:
  bool shouldAcceptLocal(const protocol::Packet& packet, const char* localCallsign) const;
};

}

