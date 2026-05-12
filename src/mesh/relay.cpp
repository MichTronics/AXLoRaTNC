#include "relay.h"
#include <Arduino.h>
#include <string.h>
#include "util/log.h"
#include "util/timer.h"

namespace axlora::mesh {

void MeshNode::begin(const char* callsign) {
  protocol::setCallsign(callsign_, callsign);
}

bool MeshNode::setCallsign(const char* callsign) {
  if (callsign == nullptr || callsign[0] == '\0') {
    return false;
  }
  const size_t len = strnlen(callsign, protocol::CALLSIGN_FIELD_LEN + 1);
  if (len == 0 || len > protocol::CALLSIGN_FIELD_LEN) {
    return false;
  }
  protocol::setCallsign(callsign_, callsign);
  return true;
}

bool MeshNode::sendChat(const char* destination, const char* text) {
  if (destination == nullptr || text == nullptr) {
    return false;
  }
  const size_t len = strnlen(text, MAX_FRAGMENT_PAYLOAD * MAX_FRAGMENTS + 1);
  if (len == 0 || len > MAX_FRAGMENT_PAYLOAD * MAX_FRAGMENTS) {
    return false;
  }
  protocol::Packet packets[MAX_FRAGMENTS]{};
  const uint16_t id = nextMessageId_++;
  const uint8_t count = fragmenter_.makeChatPackets(callsign_, destination,
                                                     reinterpret_cast<const uint8_t*>(text),
                                                     len, id, packets, MAX_FRAGMENTS);
  if (count == 0) {
    return false;
  }
  bool queued = true;
  for (uint8_t i = 0; i < count; ++i) {
    queued = enqueueTx(packets[i], true) && queued;
  }
  LOG_PROTO("queued chat id=%u fragments=%u dest=%s", id, count, destination);
  return queued;
}

void MeshNode::loop() {
  serviceRadioRx();
  serviceAckRetries();

  PendingRelay relay{};
  if (relayQueue_.peek(relay) && static_cast<int32_t>(util::nowMs() - relay.dueMs) >= 0) {
    relayQueue_.pop(relay);
    enqueueTx(relay.packet, false);
  }

  serviceTx();
}

bool MeshNode::enqueueTx(const protocol::Packet& packet, bool trackAck) {
  if (!txQueue_.push(packet)) {
    ++stats_.dropped;
    return false;
  }
  if (trackAck && (packet.flags & protocol::FLAG_ACK_REQUEST) != 0 && !protocol::isBroadcast(packet.destination)) {
    ack_.track(packet, util::nowMs());
  }
  return true;
}

void MeshNode::serviceRadioRx() {
  radio::RxPacket rx{};
  const radio::Result result = radio::driver().receive(rx);
  if (result != radio::Result::Ok) {
    return;
  }

  protocol::Packet packet{};
  if (!protocol::decode(rx.data, rx.len, packet)) {
    ++stats_.dropped;
    LOG_WARN("drop invalid packet len=%u rssi=%.1f snr=%.1f",
             static_cast<unsigned>(rx.len),
             static_cast<double>(rx.rssi),
             static_cast<double>(rx.snr));
    return;
  }
  LOG_PROTO("rx %s id=%u %s>%s ttl=%u hops=%u rssi=%.1f snr=%.1f",
            protocol::packetTypeName(packet.type), packet.messageId, packet.source, packet.destination,
            packet.ttl, packet.hopCounter, static_cast<double>(rx.rssi), static_cast<double>(rx.snr));
  handlePacket(packet, rx.rssi, rx.snr);
}

void MeshNode::serviceTx() {
  protocol::Packet packet{};
  if (!txQueue_.pop(packet)) {
    return;
  }

  uint8_t encoded[MAX_PACKET_BYTES]{};
  size_t len = 0;
  if (!protocol::encode(packet, encoded, sizeof(encoded), len)) {
    ++stats_.dropped;
    return;
  }
  const radio::Result result = radio::driver().send(encoded, len);
  if (result == radio::Result::Busy) {
    txQueue_.push(packet);
    return;
  }
  if (result == radio::Result::Ok) {
    LOG_RADIO("tx ok id=%u bytes=%u dest=%s", packet.messageId, static_cast<unsigned>(len), packet.destination);
  } else {
    ++stats_.dropped;
    LOG_WARN("tx failed id=%u result=%s", packet.messageId, radio::resultName(result));
  }
}

void MeshNode::serviceAckRetries() {
  protocol::Packet retry{};
  if (ack_.dueForRetry(util::nowMs(), retry)) {
    LOG_PROTO("retry id=%u dest=%s", retry.messageId, retry.destination);
    enqueueTx(retry, false);
  }
}

void MeshNode::handlePacket(const protocol::Packet& packet, float rssi, float snr) {
  const uint32_t now = util::nowMs();
  neighbors_.observe(packet, rssi, snr, now);

  if (protocol::callsignEquals(packet.source, callsign_)) {
    return;
  }

  if (dedup_.seen(packet)) {
    ++stats_.duplicates;
    return;
  }
  dedup_.remember(packet, now);

  if (packet.type == protocol::PacketType::Ack) {
    if (ack_.acknowledge(packet)) {
      LOG_PROTO("ack received id=%u from=%s", packet.messageId, packet.source);
    }
    maybeRelay(packet);
    return;
  }

  if (routing_.shouldAcceptLocal(packet, callsign_)) {
    if ((packet.flags & protocol::FLAG_ACK_REQUEST) != 0 && !protocol::isBroadcast(packet.destination)) {
      protocol::Packet ack{};
      protocol::makeAck(packet, callsign_, ack);
      enqueueTx(ack, false);
    }
    deliverChat(packet);
  }

  maybeRelay(packet);
}

void MeshNode::deliverChat(const protocol::Packet& packet) {
  if (packet.type != protocol::PacketType::Chat) {
    return;
  }
  uint8_t assembled[MAX_FRAGMENT_PAYLOAD * MAX_FRAGMENTS + 1]{};
  size_t len = 0;
  if (!reassembler_.accept(packet, assembled, sizeof(assembled) - 1, len)) {
    return;
  }
  assembled[len] = '\0';
  ++stats_.delivered;
  Serial.printf("[CHAT] %s: %s\n", packet.source, reinterpret_cast<char*>(assembled));
}

void MeshNode::maybeRelay(const protocol::Packet& packet) {
  if (packet.ttl == 0 || protocol::callsignEquals(packet.destination, callsign_)) {
    return;
  }
  protocol::Packet relay = packet;
  --relay.ttl;
  ++relay.hopCounter;
  relay.flags |= protocol::FLAG_RELAYED;
  PendingRelay pending{};
  pending.packet = relay;
  pending.dueMs = util::nowMs() + RELAY_DEFER_MS;
  if (relayQueue_.push(pending)) {
    ++stats_.relayed;
  } else {
    ++stats_.dropped;
  }
}

void MeshNode::printInfo() const {
  Serial.printf("AXLoRa variant=%s callsign=%s freq=%.3f MHz radio=%s\n",
                variant::NAME,
                callsign_,
                static_cast<double>(variant::DEFAULT_FREQUENCY_MHZ),
                variant::RADIO_TYPE == variant::RadioType::SX1262 ? "SX1262" : "SX1276");
  Serial.printf("OLED=%u GPS=%u BLE=%u TX_LED=%d RX_LED=%d BAT_ADC=%d\n",
                variant::HAS_OLED, variant::HAS_GPS, variant::HAS_BLE,
                variant::PIN_LED_TX, variant::PIN_LED_RX, variant::PIN_BATTERY_ADC);
}

void MeshNode::printStats() const {
  const radio::Stats& rs = radio::stats();
  Serial.printf("mesh delivered=%lu relayed=%lu dropped=%lu duplicates=%lu retries=%lu ack_fail=%lu\n",
                static_cast<unsigned long>(stats_.delivered),
                static_cast<unsigned long>(stats_.relayed),
                static_cast<unsigned long>(stats_.dropped),
                static_cast<unsigned long>(stats_.duplicates),
                static_cast<unsigned long>(ack_.retryCount()),
                static_cast<unsigned long>(ack_.failCount()));
  Serial.printf("radio tx_ok=%lu tx_fail=%lu rx_ok=%lu rx_fail=%lu duty_drops=%lu\n",
                static_cast<unsigned long>(rs.txOk),
                static_cast<unsigned long>(rs.txFail),
                static_cast<unsigned long>(rs.rxOk),
                static_cast<unsigned long>(rs.rxFail),
                static_cast<unsigned long>(rs.dutyDrops));
}

void MeshNode::printNeighbors() const {
  neighbors_.print();
}

}
