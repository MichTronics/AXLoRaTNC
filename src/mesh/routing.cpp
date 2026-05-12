#include "routing.h"

namespace axlora::mesh {

bool RoutingTable::shouldAcceptLocal(const protocol::Packet& packet, const char* localCallsign) const {
  return protocol::callsignEquals(packet.destination, localCallsign) || protocol::isBroadcast(packet.destination);
}

}

