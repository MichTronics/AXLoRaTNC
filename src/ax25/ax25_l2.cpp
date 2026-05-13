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
      LOG_PROTO("AX25 N2 exceeded, disconnecting");
      clearWindow();
      txQueue_.clear();
      peerBusy_ = false;
      t1_.stop();
      setState(LinkState::Disconnected);
      return;
    }
    ++retryCount_;
    ++stats_.retries;
    if (state_ == LinkState::Connected && hasOutstanding()) {
      t2_.stop();
      t2PendingAck_ = false;
      setState(LinkState::Recovery);
      sendSupervisory(SFrameType::RR, true);
    } else if (state_ == LinkState::Recovery) {
      sendSupervisory(SFrameType::RR, true);
    } else if (state_ == LinkState::Connecting) {
      sendUnnumbered(UFrameType::SABM);
    } else if (state_ == LinkState::Disconnecting) {
      sendUnnumbered(UFrameType::DISC);
    }
    t1_.start(config_.t1Ms);
  }
  if (state_ == LinkState::Connected && t2_.expired()) {
    t2PendingAck_ = false;
    sendSupervisory(SFrameType::RR);  // Deferred ack
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
  peerBusy_ = false;
  t2_.stop();
  t2PendingAck_ = false;
  txQueue_.clear();
  setState(LinkState::Connecting);
  return sendUnnumbered(UFrameType::SABM);
}

bool LinkLayer::disconnect() {
  if (state_ == LinkState::Disconnected) {
    return true;
  }
  retryCount_ = 0;
  txQueue_.clear();
  t2_.stop();
  t2PendingAck_ = false;
  setState(LinkState::Disconnecting);
  return sendUnnumbered(UFrameType::DISC);
}

bool LinkLayer::sendConnected(const uint8_t* data, size_t len) {
  if (state_ != LinkState::Connected || data == nullptr || len == 0) {
    return false;
  }
  if (!windowFull() && !peerBusy_ && txQueue_.empty()) {
    return sendIFrame(data, len);
  }
  QueuedInfo queued{};
  queued.len = len > MAX_INFO_LEN ? MAX_INFO_LEN : len;
  memcpy(queued.data, data, queued.len);
  if (!txQueue_.push(queued)) {
    ++stats_.queueDrops;
    return false;
  }
  ++stats_.queued;
  LOG_PROTO("AX25 queued connected data depth=%u", static_cast<unsigned>(txQueue_.size()));
  return true;
}

bool LinkLayer::sendIFrame(const uint8_t* data, size_t len) {
  if (state_ != LinkState::Connected || windowFull() || peerBusy_ || data == nullptr || len == 0) {
    return false;
  }
  // Cancel T2: NR in this I-frame piggybacks the ack
  t2PendingAck_ = false;
  t2_.stop();
  Frame frame{};
  frame.destination = peer_;
  frame.source = config_.local;
  frame.command = true;   // I frames are always commands
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
    storeOutstanding(frame);
    retryCount_ = 0;
    t1_.start(config_.t1Ms);
  }
  return ok;
}

bool LinkLayer::sendNextQueued() {
  if (state_ != LinkState::Connected || windowFull()) {
    return false;
  }
  QueuedInfo queued{};
  if (!txQueue_.pop(queued)) {
    return false;
  }
  if (!sendIFrame(queued.data, queued.len)) {
    txQueue_.push(queued);
    return false;
  }
  LOG_PROTO("AX25 sent queued data remaining=%u", static_cast<unsigned>(txQueue_.size()));
  return true;
}

void LinkLayer::fillWindow() {
  while (state_ == LinkState::Connected && !windowFull() && !txQueue_.empty()) {
    if (!sendNextQueued()) {
      break;
    }
  }
}

void LinkLayer::clearWindow() {
  for (WindowSlot& slot : window_) {
    slot.active = false;
  }
}

bool LinkLayer::windowFull() const {
  return outstandingCount() >= WINDOW_SIZE;
}

bool LinkLayer::hasOutstanding() const {
  return outstandingCount() > 0;
}

uint8_t LinkLayer::outstandingCount() const {
  uint8_t count = 0;
  for (const WindowSlot& slot : window_) {
    if (slot.active) {
      ++count;
    }
  }
  return count;
}

void LinkLayer::retransmitWindow() {
  for (const WindowSlot& slot : window_) {
    if (slot.active) {
      transmit(slot.frame, false);
    }
  }
}

void LinkLayer::storeOutstanding(const Frame& frame) {
  for (WindowSlot& slot : window_) {
    if (!slot.active) {
      slot.active = true;
      slot.frame = frame;
      return;
    }
  }
}

bool LinkLayer::sendSupervisory(SFrameType type, bool poll) {
  Frame frame{};
  frame.destination = peer_;
  frame.source = config_.local;
  frame.command = poll;   // poll=true → command, poll=false → response
  frame.control = makeS(type, vr_, poll);
  return transmit(frame, false);
}

bool LinkLayer::sendUnnumbered(UFrameType type) {
  Frame frame{};
  frame.destination = peer_;
  frame.source = config_.local;
  frame.command = (type == UFrameType::SABM || type == UFrameType::DISC || type == UFrameType::UI);
  frame.control = makeU(type, true);
  const bool ok = transmit(frame, false);
  if (ok && (type == UFrameType::SABM || type == UFrameType::DISC)) {
    t1_.start(config_.t1Ms);
  }
  return ok;
}

void LinkLayer::handleI(const Frame& frame) {
  if (state_ != LinkState::Connected && state_ != LinkState::Recovery) {
    return;
  }
  if (!addressEquals(frame.source, peer_)) {
    return;
  }
  if (state_ == LinkState::Recovery) {
    LOG_PROTO("AX25 I-frame received in recovery, returning to connected");
    setState(LinkState::Connected);
    retryCount_ = 0;
  }
  processAck(nr(frame.control));
  if (ns(frame.control) == vr_) {
    vr_ = static_cast<uint8_t>((vr_ + 1) & 0x07);
    ++stats_.iRx;
    if (data_ != nullptr) {
      data_(frame.info, frame.infoLen, true, ctx_);
    }
    // T2: defer RR ack to allow piggybacking on an outgoing I-frame
    if (!t2PendingAck_) {
      t2PendingAck_ = true;
      t2_.start(config_.t2Ms);
    }
  } else {
    LOG_PROTO("AX25 out-of-seq I NS=%u VR=%u, sending REJ", ns(frame.control), vr_);
    ++stats_.rejTx;
    sendSupervisory(SFrameType::REJ);
  }
}

void LinkLayer::handleS(const Frame& frame) {
  if (state_ != LinkState::Connected && state_ != LinkState::Recovery) {
    return;
  }
  if (!addressEquals(frame.source, peer_)) {
    return;
  }
  const bool finalBit = (frame.control & 0x10) != 0;
  processAck(nr(frame.control));
  switch (sType(frame.control)) {
    case SFrameType::RR:
      if (peerBusy_) {
        peerBusy_ = false;
        LOG_PROTO("AX25 peer RNR cleared");
      }
      if (state_ == LinkState::Recovery && finalBit) {
        LOG_PROTO("AX25 recovery complete via RR F=1");
        setState(LinkState::Connected);
        retryCount_ = 0;
        if (hasOutstanding()) {
          retransmitWindow();
          t1_.start(config_.t1Ms);
        } else {
          t1_.stop();
          fillWindow();
        }
      } else if (state_ == LinkState::Connected && !peerBusy_) {
        fillWindow();
      }
      break;
    case SFrameType::RNR:
      if (!peerBusy_) {
        peerBusy_ = true;
        LOG_PROTO("AX25 peer RNR busy");
      }
      if (state_ == LinkState::Recovery && finalBit) {
        LOG_PROTO("AX25 recovery: peer busy (RNR F=1), back to connected");
        setState(LinkState::Connected);
        retryCount_ = 0;
        t1_.stop();
      }
      break;
    case SFrameType::REJ:
      peerBusy_ = false;
      if (state_ == LinkState::Recovery) {
        setState(LinkState::Connected);
        retryCount_ = 0;
      }
      if (hasOutstanding()) {
        retransmitWindow();
        t1_.start(config_.t1Ms);
      }
      break;
    default:
      break;
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
    peerBusy_ = false;
    t2_.stop();
    t2PendingAck_ = false;
    clearWindow();
    txQueue_.clear();
    t1_.stop();
    setState(LinkState::Connected);
    sendUnnumbered(UFrameType::UA);
    t3_.start(config_.t3Ms);
    return;
  }
  if (type == UFrameType::UA) {
    if (state_ == LinkState::Connecting) {
      retryCount_ = 0;
      t1_.stop();
      setState(LinkState::Connected);
      t3_.start(config_.t3Ms);
      fillWindow();
    } else if (state_ == LinkState::Disconnecting) {
      t1_.stop();
      t2_.stop();
      t2PendingAck_ = false;
      clearWindow();
      txQueue_.clear();
      setState(LinkState::Disconnected);
    }
    return;
  }
  if (type == UFrameType::DISC) {
    peer_ = frame.source;
    sendUnnumbered(UFrameType::UA);
    clearWindow();
    txQueue_.clear();
    t1_.stop();
    setState(LinkState::Disconnected);
    return;
  }
  if (type == UFrameType::DM) {
    clearWindow();
    txQueue_.clear();
    t1_.stop();
    t2_.stop();
    t2PendingAck_ = false;
    setState(LinkState::Disconnected);
  }
}

void LinkLayer::processAck(uint8_t nrValue) {
  bool ackedAny = false;
  while (va_ != nrValue) {
    const uint8_t ackSeq = va_;
    bool found = false;
    for (WindowSlot& slot : window_) {
      if (slot.active && ns(slot.frame.control) == ackSeq) {
        slot.active = false;
        found = true;
        ackedAny = true;
        break;
      }
    }
    va_ = static_cast<uint8_t>((va_ + 1) & 0x07);
    if (!found && !hasOutstanding()) {
      break;
    }
  }
  if (ackedAny) {
    retryCount_ = 0;
    if (hasOutstanding()) {
      t1_.start(config_.t1Ms);
    } else {
      t1_.stop();
    }
    fillWindow();
  }
}

bool LinkLayer::addressedToLocal(const Frame& frame) const {
  return addressEquals(frame.destination, config_.local);
}

void LinkLayer::printStats() const {
  Serial.printf("ax25 state=%s ui_tx=%lu ui_rx=%lu i_tx=%lu i_rx=%lu queued=%lu q_depth=%u q_drops=%lu retries=%lu rej_tx=%lu fcs_drops=%lu\n",
                stateName(state_),
                static_cast<unsigned long>(stats_.uiTx),
                static_cast<unsigned long>(stats_.uiRx),
                static_cast<unsigned long>(stats_.iTx),
                static_cast<unsigned long>(stats_.iRx),
                static_cast<unsigned long>(stats_.queued),
                static_cast<unsigned>(txQueue_.size()),
                static_cast<unsigned long>(stats_.queueDrops),
                static_cast<unsigned long>(stats_.retries),
                static_cast<unsigned long>(stats_.rejTx),
                static_cast<unsigned long>(stats_.fcsDrops));
}

void LinkLayer::printStatus() const {
  char local[12]{};
  char peer[12]{};
  formatAddress(config_.local, local, sizeof(local));
  formatAddress(peer_, peer, sizeof(peer));
  Serial.printf("AX25 local=%s peer=%s state=%s VS=%u VA=%u VR=%u outstanding=%u win=%u q_depth=%u\n",
                local, peer, stateName(state_), vs_, va_, vr_,
                hasOutstanding(), outstandingCount(), static_cast<unsigned>(txQueue_.size()));
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
