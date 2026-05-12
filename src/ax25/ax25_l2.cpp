#include "ax25_l2.h"
#include <Arduino.h>
#include <string.h>
#include "util/log.h"

namespace axlora::ax25 {

void LinkLayer::begin(const L2Config& config, TxCallback tx, DataCallback data, void* ctx) {
  config_ = config;
  tx_ = tx;
  data_ = data;
  ctx_ = ctx;
  setState(LinkState::Disconnected);
}

void LinkLayer::loop() {
  if (t1_.expired()) {
    if (retryCount_ >= config_.n2) {
      LOG_PROTO("AX25 N2 exceeded");
      hasOutstanding_ = false;
      t1_.stop();
      setState(LinkState::Disconnected);
      return;
    }
    ++retryCount_;
    ++stats_.retries;
    if (hasOutstanding_) {
      transmit(outstanding_, false);
    } else if (state_ == LinkState::Connecting) {
      sendUnnumbered(UFrameType::SABM);
    } else if (state_ == LinkState::Disconnecting) {
      sendUnnumbered(UFrameType::DISC);
    }
    t1_.start(config_.t1Ms);
  }
  if (state_ == LinkState::Connected && t3_.expired()) {
    sendSupervisory(SFrameType::RR);
    t3_.start(config_.t3Ms);
  }
}

bool LinkLayer::sendUi(const Address& destination, const uint8_t* data, size_t len) {
  Frame frame{};
  frame.destination = destination;
  frame.source = config_.local;
  frame.control = CTRL_UI;
  frame.pid = PID_NO_LAYER3;
  frame.infoLen = len > MAX_INFO_LEN ? MAX_INFO_LEN : len;
  memcpy(frame.info, data, frame.infoLen);
  const bool ok = transmit(frame, false);
  if (ok) {
    ++stats_.uiTx;
  }
  return ok;
}

bool LinkLayer::connectTo(const Address& destination) {
  if (state_ != LinkState::Disconnected) {
    return false;
  }
  peer_ = destination;
  vs_ = 0;
  va_ = 0;
  vr_ = 0;
  retryCount_ = 0;
  setState(LinkState::Connecting);
  return sendUnnumbered(UFrameType::SABM);
}

bool LinkLayer::disconnect() {
  if (state_ == LinkState::Disconnected) {
    return true;
  }
  retryCount_ = 0;
  setState(LinkState::Disconnecting);
  return sendUnnumbered(UFrameType::DISC);
}

bool LinkLayer::sendConnected(const uint8_t* data, size_t len) {
  if (state_ != LinkState::Connected || hasOutstanding_ || data == nullptr || len == 0) {
    return false;
  }
  Frame frame{};
  frame.destination = peer_;
  frame.source = config_.local;
  frame.control = makeI(vs_, vr_, false);
  frame.pid = PID_NO_LAYER3;
  frame.infoLen = len > MAX_INFO_LEN ? MAX_INFO_LEN : len;
  memcpy(frame.info, data, frame.infoLen);
  if (!transmit(frame, true)) {
    return false;
  }
  vs_ = static_cast<uint8_t>((vs_ + 1) & 0x07);
  ++stats_.iTx;
  return true;
}

void LinkLayer::receive(const uint8_t* data, size_t len) {
  Frame frame{};
  if (!decodeFrame(data, len, frame, true)) {
    ++stats_.fcsDrops;
    LOG_WARN("AX25 drop bad frame/fcs len=%u", static_cast<unsigned>(len));
    return;
  }
  if (!addressedToLocal(frame)) {
    return;
  }
  switch (kind(frame.control)) {
    case FrameKind::I: handleI(frame); break;
    case FrameKind::S: handleS(frame); break;
    case FrameKind::U: handleU(frame); break;
    default: break;
  }
}

void LinkLayer::setState(LinkState state) {
  if (state_ != state) {
    state_ = state;
    ++stats_.stateChanges;
    LOG_PROTO("AX25 state=%s", stateName(state_));
  }
}

bool LinkLayer::transmit(const Frame& frame, bool remember) {
  if (tx_ == nullptr) {
    return false;
  }
  uint8_t bytes[MAX_PACKET_BYTES]{};
  size_t len = 0;
  if (!encodeFrame(frame, bytes, sizeof(bytes), len, true)) {
    return false;
  }
  const bool ok = tx_(bytes, len, ctx_);
  if (ok && remember) {
    outstanding_ = frame;
    hasOutstanding_ = true;
    retryCount_ = 0;
    t1_.start(config_.t1Ms);
  }
  return ok;
}

bool LinkLayer::sendSupervisory(SFrameType type) {
  Frame frame{};
  frame.destination = peer_;
  frame.source = config_.local;
  frame.control = makeS(type, vr_, false);
  return transmit(frame, false);
}

bool LinkLayer::sendUnnumbered(UFrameType type) {
  Frame frame{};
  frame.destination = peer_;
  frame.source = config_.local;
  frame.control = makeU(type, true);
  const bool ok = transmit(frame, false);
  if (ok && (type == UFrameType::SABM || type == UFrameType::DISC)) {
    t1_.start(config_.t1Ms);
  }
  return ok;
}

void LinkLayer::handleI(const Frame& frame) {
  if (state_ != LinkState::Connected || !addressEquals(frame.source, peer_)) {
    return;
  }
  processAck(nr(frame.control));
  if (ns(frame.control) == vr_) {
    vr_ = static_cast<uint8_t>((vr_ + 1) & 0x07);
    ++stats_.iRx;
    if (data_ != nullptr) {
      data_(frame.info, frame.infoLen, true, ctx_);
    }
  }
  sendSupervisory(SFrameType::RR);
}

void LinkLayer::handleS(const Frame& frame) {
  if (state_ != LinkState::Connected || !addressEquals(frame.source, peer_)) {
    return;
  }
  processAck(nr(frame.control));
  if (sType(frame.control) == SFrameType::REJ && hasOutstanding_) {
    transmit(outstanding_, false);
    t1_.start(config_.t1Ms);
  }
}

void LinkLayer::handleU(const Frame& frame) {
  const UFrameType type = uType(frame.control);
  if (type == UFrameType::UI) {
    ++stats_.uiRx;
    if (data_ != nullptr) {
      data_(frame.info, frame.infoLen, false, ctx_);
    }
    return;
  }
  if (type == UFrameType::SABM) {
    peer_ = frame.source;
    vs_ = 0;
    va_ = 0;
    vr_ = 0;
    hasOutstanding_ = false;
    setState(LinkState::Connected);
    sendUnnumbered(UFrameType::UA);
    t1_.stop();
    t3_.start(config_.t3Ms);
    return;
  }
  if (type == UFrameType::UA) {
    if (state_ == LinkState::Connecting) {
      retryCount_ = 0;
      t1_.stop();
      setState(LinkState::Connected);
      t3_.start(config_.t3Ms);
    } else if (state_ == LinkState::Disconnecting) {
      t1_.stop();
      hasOutstanding_ = false;
      setState(LinkState::Disconnected);
    }
    return;
  }
  if (type == UFrameType::DISC) {
    peer_ = frame.source;
    sendUnnumbered(UFrameType::UA);
    hasOutstanding_ = false;
    t1_.stop();
    setState(LinkState::Disconnected);
    return;
  }
  if (type == UFrameType::DM) {
    hasOutstanding_ = false;
    t1_.stop();
    setState(LinkState::Disconnected);
  }
}

void LinkLayer::processAck(uint8_t nrValue) {
  if (hasOutstanding_ && nrValue == vs_) {
    hasOutstanding_ = false;
    va_ = nrValue;
    retryCount_ = 0;
    t1_.stop();
  }
}

bool LinkLayer::addressedToLocal(const Frame& frame) const {
  return addressEquals(frame.destination, config_.local);
}

void LinkLayer::printStats() const {
  Serial.printf("ax25 state=%s ui_tx=%lu ui_rx=%lu i_tx=%lu i_rx=%lu retries=%lu fcs_drops=%lu\n",
                stateName(state_),
                static_cast<unsigned long>(stats_.uiTx),
                static_cast<unsigned long>(stats_.uiRx),
                static_cast<unsigned long>(stats_.iTx),
                static_cast<unsigned long>(stats_.iRx),
                static_cast<unsigned long>(stats_.retries),
                static_cast<unsigned long>(stats_.fcsDrops));
}

void LinkLayer::printStatus() const {
  char local[12]{};
  char peer[12]{};
  formatAddress(config_.local, local, sizeof(local));
  formatAddress(peer_, peer, sizeof(peer));
  Serial.printf("AX25 local=%s peer=%s state=%s VS=%u VA=%u VR=%u\n", local, peer, stateName(state_), vs_, va_, vr_);
}

const char* LinkLayer::stateName(LinkState state) {
  switch (state) {
    case LinkState::Disconnected: return "DISCONNECTED";
    case LinkState::Connecting: return "CONNECTING";
    case LinkState::Connected: return "CONNECTED";
    case LinkState::Disconnecting: return "DISCONNECTING";
    case LinkState::Recovery: return "RECOVERY";
    default: return "?";
  }
}

}
