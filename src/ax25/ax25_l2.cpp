#include "ax25_l2.h"
#include <Arduino.h>
#include <string.h>
#include "util/log.h"

namespace axlora::ax25 {

namespace {

uint8_t seqDistance(uint8_t from, uint8_t to) {
  return static_cast<uint8_t>((to - from) & 0x07);
}

}  // namespace

void LinkLayer::begin(const L2Config& config, TxCallback tx, DataCallback data, void* ctx) {
  config_ = config;
  if (maxFrame_ == 0 || maxFrame_ > WINDOW_SIZE) maxFrame_ = WINDOW_SIZE;
  tx_ = tx;
  data_ = data;
  ctx_ = ctx;
  setState(LinkState::Disconnected);
}

void LinkLayer::loop() {
  if (t1_.expired()) {
    ++stats_.t1Expired;
    if (config_.n2 != 0 && retryCount_ >= config_.n2) {
      LOG_PROTO("AX25 T1 N2=%u exceeded state=%s → Disconnected", config_.n2, stateName(state_));
      clearWindow();
      clearReceiveBuffer();
      txQueue_.clear();
      peerBusy_ = false;
      t1_.stop();
      t2_.stop();
      t2PendingAck_ = false;
      t4_.stop();
      setState(LinkState::Disconnected);
      return;
    }
    ++retryCount_;
    ++stats_.retries;
    LOG_PROTO("AX25 T1 expired state=%s retry=%u/%u outstanding=%u",
              stateName(state_), retryCount_, config_.n2, outstandingCount());
    if (state_ == LinkState::Connected && hasOutstanding()) {
      t2_.stop();
      t2PendingAck_ = false;
      setState(LinkState::Recovery);
      LOG_PROTO("AX25 TX RR P=1 NR=%u reason=T1_RECOVERY outstanding=%u", vr_, outstandingCount());
      sendSupervisory(SFrameType::RR, true);
    } else if (state_ == LinkState::Recovery) {
      LOG_PROTO("AX25 TX RR P=1 NR=%u reason=T1_RETRY retry=%u", vr_, retryCount_);
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
    t2_.stop();
    LOG_PROTO("AX25 T2 deferred-ACK TX RR NR=%u", vr_);
    sendSupervisory(SFrameType::RR);
  }
  if (state_ == LinkState::Connected && t3_.expired()) {
    t2_.stop();
    t2PendingAck_ = false;
    LOG_PROTO("AX25 T3 keepalive TX RR NR=%u", vr_);
    sendSupervisory(SFrameType::RR);
    t3_.start(config_.t3Ms);
  }
  // T4: probe peer if it has been in RNR (busy) too long
  if (state_ == LinkState::Connected && peerBusy_ && config_.t4Ms > 0 && t4_.expired()) {
    LOG_PROTO("AX25 T4: peer busy timeout, probing with RR P=1");
    sendSupervisory(SFrameType::RR, true);
    t1_.start(config_.t1Ms);
    t4_.start(config_.t4Ms);
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
  clearReceiveBuffer();
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
  clearReceiveBuffer();
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

void LinkLayer::setTimers(uint32_t t1Ms, uint32_t t2Ms, uint32_t t3Ms, uint32_t t4Ms) {
  config_.t1Ms = t1Ms;
  config_.t2Ms = t2Ms;
  config_.t3Ms = t3Ms;
  config_.t4Ms = t4Ms;
}

void LinkLayer::setRetryLimit(uint8_t n2) {
  config_.n2 = n2;
}

void LinkLayer::setMaxFrame(uint8_t maxFrame) {
  if (maxFrame == 0) maxFrame = 1;
  if (maxFrame > WINDOW_SIZE) maxFrame = WINDOW_SIZE;
  maxFrame_ = maxFrame;
}

void LinkLayer::notifyTxSent() {
  // Restart T1 from now — corrects for async TX queue delay (CSMA + LoRa airtime).
  // T1 was started when the frame was queued; restart it when the frame is actually on air.
  if (t1_.running()) {
    LOG_PROTO("AX25 T1 restarted from air-TX state=%s t1=%lums", stateName(state_),
              static_cast<unsigned long>(config_.t1Ms));
    t1_.start(config_.t1Ms);
  }
}

bool LinkLayer::setLocalAddress(const Address& local) {
  if (state_ != LinkState::Disconnected) {
    return false;
  }
  config_.local = local;
  peer_ = Address{};
  vs_ = 0;
  va_ = 0;
  vr_ = 0;
  retryCount_ = 0;
  peerBusy_ = false;
  t1_.stop();
  t2_.stop();
  t3_.stop();
  t4_.stop();
  t2PendingAck_ = false;
  clearWindow();
  clearReceiveBuffer();
  txQueue_.clear();
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
  return outstandingCount() >= maxFrame_;
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
      ++stats_.retx;
      transmit(slot.frame, false);
    }
  }
}

bool LinkLayer::retransmitOne(uint8_t nsValue) {
  for (const WindowSlot& slot : window_) {
    if (slot.active && ns(slot.frame.control) == nsValue) {
      ++stats_.retx;
      return transmit(slot.frame, false);
    }
  }
  return false;
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

void LinkLayer::clearReceiveBuffer() {
  for (ReceiveSlot& slot : receiveWindow_) {
    slot.active = false;
  }
  for (bool& pending : srejPending_) {
    pending = false;
  }
}

bool LinkLayer::receiveBuffered(uint8_t nsValue, Frame& out) {
  for (ReceiveSlot& slot : receiveWindow_) {
    if (slot.active && ns(slot.frame.control) == nsValue) {
      out = slot.frame;
      slot.active = false;
      return true;
    }
  }
  return false;
}

bool LinkLayer::storeReceiveBuffered(const Frame& frame) {
  for (ReceiveSlot& slot : receiveWindow_) {
    if (slot.active && ns(slot.frame.control) == ns(frame.control)) {
      return true;
    }
  }
  for (ReceiveSlot& slot : receiveWindow_) {
    if (!slot.active) {
      slot.active = true;
      slot.frame = frame;
      return true;
    }
  }
  return false;
}

bool LinkLayer::inReceiveWindow(uint8_t nsValue) const {
  const uint8_t distance = seqDistance(vr_, nsValue);
  return distance > 0 && distance < maxFrame_;
}

void LinkLayer::deliverIFrame(const Frame& frame) {
  srejPending_[ns(frame.control)] = false;
  vr_ = static_cast<uint8_t>((vr_ + 1) & 0x07);
  ++stats_.iRx;
  if (data_ != nullptr) {
    data_(frame.info, frame.infoLen, true, ctx_);
  }
}

void LinkLayer::deferAck() {
  if (!t2PendingAck_) {
    t2PendingAck_ = true;
    t2_.start(config_.t2Ms);
    LOG_PROTO("AX25 T2 armed deferred-ACK NR=%u t2=%lums", vr_,
              static_cast<unsigned long>(config_.t2Ms));
  }
}

bool LinkLayer::sendSupervisoryFrame(SFrameType type, uint8_t nrValue, bool pf, bool command) {
  Frame frame{};
  frame.destination = peer_;
  frame.source = config_.local;
  frame.command = command;
  frame.control = makeS(type, nrValue, pf);
  if (type == SFrameType::RR) ++stats_.rrTx;
  if (type == SFrameType::REJ) ++stats_.rejTx;
  return transmit(frame, false);
}

bool LinkLayer::sendSupervisoryNr(SFrameType type, uint8_t nrValue, bool poll) {
  return sendSupervisoryFrame(type, nrValue, poll, poll);
}

bool LinkLayer::sendSupervisoryFinal(SFrameType type, uint8_t nrValue) {
  return sendSupervisoryFrame(type, nrValue, true, false);
}

bool LinkLayer::sendSupervisory(SFrameType type, bool poll) {
  return sendSupervisoryNr(type, vr_, poll);
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
  // Validate N(R): must acknowledge only already-sent frames (AX.25 §4.4.5.1, Z bit)
  if (!nrValid(nr(frame.control))) {
    LOG_WARN("AX25 I-frame invalid NR=%u VA=%u VS=%u, sending FRMR", nr(frame.control), va_, vs_);
    ++stats_.nrInvalid;
    sendFrmr(frame.control, frame.command, 0x08);  // Z bit
    clearWindow();
    clearReceiveBuffer();
    txQueue_.clear();
    t1_.stop(); t2_.stop(); t2PendingAck_ = false; t4_.stop();
    peerBusy_ = false;
    setState(LinkState::Disconnected);
    return;
  }
  if (state_ == LinkState::Recovery) {
    LOG_PROTO("AX25 I-frame received in recovery, returning to connected");
    setState(LinkState::Connected);
    retryCount_ = 0;
  }
  processAck(nr(frame.control));
  if (ns(frame.control) == vr_) {
    deliverIFrame(frame);
    Frame buffered{};
    while (receiveBuffered(vr_, buffered)) {
      deliverIFrame(buffered);
    }
    const bool pollBit = frame.command && ((frame.control & 0x10) != 0);
    if (pollBit) {
      t2_.stop();
      t2PendingAck_ = false;
      LOG_PROTO("AX25 TX RR F=1 NR=%u reason=I_POLL_RESPONSE", vr_);
      sendSupervisoryFinal(SFrameType::RR, vr_);
    } else {
      deferAck();
    }
  } else if (inReceiveWindow(ns(frame.control)) && storeReceiveBuffered(frame)) {
    LOG_PROTO("AX25 out-of-seq I NS=%u VR=%u, sending SREJ", ns(frame.control), vr_);
    if (!srejPending_[vr_]) {
      srejPending_[vr_] = true;
      ++stats_.srejTx;
      if (frame.command && ((frame.control & 0x10) != 0)) {
        sendSupervisoryFinal(SFrameType::SREJ, vr_);
      } else {
        sendSupervisoryNr(SFrameType::SREJ, vr_);
      }
    }
  } else {
    LOG_PROTO("AX25 out-of-seq I NS=%u VR=%u, sending REJ", ns(frame.control), vr_);
    if (frame.command && ((frame.control & 0x10) != 0)) {
      sendSupervisoryFinal(SFrameType::REJ, vr_);
    } else {
      sendSupervisory(SFrameType::REJ);
    }
  }
}

void LinkLayer::handleS(const Frame& frame) {
  if (state_ != LinkState::Connected && state_ != LinkState::Recovery) {
    return;
  }
  if (!addressEquals(frame.source, peer_)) {
    return;
  }
  // Validate N(R): must acknowledge only already-sent frames (AX.25 §4.4.5.1, Z bit)
  if (!nrValid(nr(frame.control))) {
    LOG_WARN("AX25 S-frame invalid NR=%u VA=%u VS=%u, sending FRMR", nr(frame.control), va_, vs_);
    ++stats_.nrInvalid;
    sendFrmr(frame.control, frame.command, 0x08);  // Z bit
    clearWindow();
    clearReceiveBuffer();
    txQueue_.clear();
    t1_.stop(); t2_.stop(); t2PendingAck_ = false; t4_.stop();
    peerBusy_ = false;
    setState(LinkState::Disconnected);
    return;
  }
  const bool finalBit = (frame.control & 0x10) != 0;
  const uint8_t rxNr = nr(frame.control);
  LOG_PROTO("AX25 RX %s%s NR=%u pf=%u cmd=%u VR=%u VS=%u VA=%u state=%s",
            sType(frame.control) == SFrameType::RR  ? "RR"  :
            sType(frame.control) == SFrameType::RNR ? "RNR" :
            sType(frame.control) == SFrameType::REJ ? "REJ" : "SREJ",
            finalBit ? (frame.command ? "p" : "v") : "",
            rxNr, finalBit ? 1 : 0, frame.command ? 1 : 0,
            vr_, vs_, va_, stateName(state_));
  processAck(rxNr);
  if (frame.command && finalBit) {
    t2_.stop();
    t2PendingAck_ = false;
    LOG_PROTO("AX25 TX RR F=1 NR=%u reason=POLL_RESPONSE", vr_);
    sendSupervisoryFinal(SFrameType::RR, vr_);
  }
  switch (sType(frame.control)) {
    case SFrameType::RR:
      ++stats_.rrRx;
      if (peerBusy_) {
        peerBusy_ = false;
        t4_.stop();
        LOG_PROTO("AX25 peer RNR cleared");
      }
      if (state_ == LinkState::Recovery && finalBit) {
        LOG_PROTO("AX25 recovery complete via RR F=1 outstanding=%u", outstandingCount());
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
        if (config_.t4Ms > 0) {
          t4_.start(config_.t4Ms);
        }
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
      ++stats_.rejRx;
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
    case SFrameType::SREJ:
      peerBusy_ = false;
      ++stats_.srejRx;
      LOG_PROTO("AX25 SREJ received for NS=%u", nr(frame.control));
      if (retransmitOne(nr(frame.control))) {
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

  // SABME: peer wants mod-128 — we only support mod-8, respond with DM (AX.25 v2.2 §4.3.3.1)
  if (type == UFrameType::SABME) {
    ++stats_.sabmeRx;
    LOG_PROTO("AX25 SABME received (mod-128 not supported), sending DM");
    peer_ = frame.source;
    Frame dm{};
    dm.destination = frame.source;
    dm.source = config_.local;
    dm.command = false;
    dm.control = makeU(UFrameType::DM, true);  // F=1 (final, responding to P=1)
    transmit(dm, false);
    return;
  }

  if (type == UFrameType::SABM) {
    const bool wasConnected = (state_ == LinkState::Connected || state_ == LinkState::Recovery);
    peer_ = frame.source;
    vs_ = 0;
    va_ = 0;
    vr_ = 0;
    peerBusy_ = false;
    t2_.stop();
    t2PendingAck_ = false;
    t4_.stop();
    clearWindow();
    clearReceiveBuffer();
    txQueue_.clear();
    t1_.stop();
    if (wasConnected) {
      LOG_PROTO("AX25 SABM rx while %s → sequences reset (re-SABM)", stateName(state_));
    } else {
      LOG_PROTO("AX25 SABM rx state=%s → Connected", stateName(state_));
    }
    setState(LinkState::Connected);
    sendUnnumbered(UFrameType::UA);
    t3_.start(config_.t3Ms);
    return;
  }

  if (type == UFrameType::UA) {
    if (state_ == LinkState::Connecting) {
      LOG_PROTO("AX25 UA rx → Connected (retry=%u)", retryCount_);
      retryCount_ = 0;
      t1_.stop();
      setState(LinkState::Connected);
      t3_.start(config_.t3Ms);
      fillWindow();
    } else if (state_ == LinkState::Disconnecting) {
      t1_.stop();
      t2_.stop();
      t2PendingAck_ = false;
      t4_.stop();
      clearWindow();
      clearReceiveBuffer();
      txQueue_.clear();
      setState(LinkState::Disconnected);
    }
    return;
  }

  if (type == UFrameType::DISC) {
    peer_ = frame.source;
    sendUnnumbered(UFrameType::UA);
    clearWindow();
    clearReceiveBuffer();
    txQueue_.clear();
    t1_.stop();
    t2_.stop();
    t2PendingAck_ = false;
    t4_.stop();
    peerBusy_ = false;
    setState(LinkState::Disconnected);
    return;
  }

  if (type == UFrameType::DM) {
    clearWindow();
    clearReceiveBuffer();
    txQueue_.clear();
    t1_.stop();
    t2_.stop();
    t2PendingAck_ = false;
    t4_.stop();
    peerBusy_ = false;
    setState(LinkState::Disconnected);
    return;
  }

  if (type == UFrameType::FRMR) {
    LOG_PROTO("AX25 FRMR received, resetting link");
    clearWindow();
    clearReceiveBuffer();
    txQueue_.clear();
    t1_.stop();
    t2_.stop();
    t2PendingAck_ = false;
    t4_.stop();
    peerBusy_ = false;
    sendUnnumbered(UFrameType::DM);
    setState(LinkState::Disconnected);
    return;
  }

  // XID: respond with our mod-8 capabilities (AX.25 v2.2 §4.3.3.8)
  if (type == UFrameType::XID) {
    ++stats_.xidRx;
    if (frame.command) {
      sendXidResponse(frame);
    }
    return;
  }

  // TEST: echo info field back with F=1 (AX.25 v2.2 §4.3.3.9)
  if (type == UFrameType::TEST) {
    ++stats_.testRx;
    if (frame.command) {
      Frame resp{};
      resp.destination = frame.source;
      resp.source = config_.local;
      resp.command = false;
      resp.control = makeU(UFrameType::TEST, true);  // F=1
      resp.infoLen = frame.infoLen;
      memcpy(resp.info, frame.info, frame.infoLen);
      transmit(resp, false);
      LOG_PROTO("AX25 TEST echo sent len=%u", static_cast<unsigned>(frame.infoLen));
    }
    return;
  }

  // Unknown U-frame: send FRMR (W bit = undefined/not implemented) if connected,
  // or DM if disconnected (AX.25 v2.2 §4.4.5.1)
  LOG_WARN("AX25 unknown U-frame ctrl=0x%02X state=%s", frame.control, stateName(state_));
  if (state_ == LinkState::Connected || state_ == LinkState::Recovery) {
    sendFrmr(frame.control, frame.command, 0x01);  // W bit
  } else {
    Frame dm{};
    dm.destination = frame.source;
    dm.source = config_.local;
    dm.command = false;
    dm.control = makeU(UFrameType::DM, true);
    transmit(dm, false);
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

bool LinkLayer::nrValid(uint8_t nrValue) const {
  // NR must acknowledge only frames already sent: VA <= NR <= VS (mod 8)
  return seqDistance(va_, nrValue) <= seqDistance(va_, vs_);
}

void LinkLayer::sendFrmr(uint8_t rejectedControl, bool cr, uint8_t reasonBits) {
  // FRMR is always a response, F=1
  Frame frame{};
  frame.destination = peer_;
  frame.source = config_.local;
  frame.command = false;
  frame.control = makeU(UFrameType::FRMR, true);
  // 3-byte FRMR info: [rejected ctrl] [VR|CR|VS] [reason W/X/Y/Z]
  frame.info[0] = rejectedControl;
  frame.info[1] = static_cast<uint8_t>((vr_ << 5) | (cr ? 0x10 : 0x00) | (vs_ << 1));
  frame.info[2] = reasonBits;
  frame.infoLen = 3;
  transmit(frame, false);
  ++stats_.frmrTx;
  LOG_PROTO("AX25 FRMR tx ctrl=0x%02X reason=0x%02X", rejectedControl, reasonBits);
}

void LinkLayer::sendXidResponse(const Frame& rxFrame) {
  // Respond with XID advertising mod-8 capabilities (AX.25 v2.2 §4.3.3.8)
  Frame frame{};
  frame.destination = rxFrame.source;
  frame.source = config_.local;
  frame.command = false;  // response
  frame.control = makeU(UFrameType::XID, true);  // F=1
  uint8_t* p = frame.info;
  *p++ = 0x82;  // FI: HDLC Format Identifier
  *p++ = 0x80;  // GI: Parameter Group Identifier
  *p++ = 0x00;  // GL high byte (filled below)
  uint8_t* glLow = p++;  // GL low byte placeholder
  const uint8_t* paramStart = p;
  // Classes of Procedures: ABM half-duplex (0x0020)
  *p++ = 0x02; *p++ = 0x02; *p++ = 0x00; *p++ = 0x20;
  // HDLC Optional Functions: mod-8, basic I-frames
  *p++ = 0x03; *p++ = 0x03; *p++ = 0x86; *p++ = 0xA8; *p++ = 0x02;
  // I-field Length Receive: 256 bytes = 2048 bits (big-endian)
  *p++ = 0x06; *p++ = 0x02; *p++ = 0x08; *p++ = 0x00;
  // Window Size Receive
  *p++ = 0x07; *p++ = 0x01; *p++ = maxFrame_;
  // Acknowledgement Timer (T1, ms)
  const uint16_t t1 = static_cast<uint16_t>(config_.t1Ms > 0xFFFF ? 0xFFFF : config_.t1Ms);
  *p++ = 0x08; *p++ = 0x02; *p++ = static_cast<uint8_t>(t1 >> 8); *p++ = static_cast<uint8_t>(t1);
  // Retries (N2)
  *p++ = 0x09; *p++ = 0x01; *p++ = config_.n2;
  *glLow = static_cast<uint8_t>(p - paramStart);
  frame.infoLen = static_cast<size_t>(p - frame.info);
  transmit(frame, false);
  LOG_PROTO("AX25 XID response sent");
}

bool LinkLayer::addressedToLocal(const Frame& frame) const {
  return addressEquals(frame.destination, config_.local);
}

void LinkLayer::printStats() const {
  Serial.printf("ax25 state=%s ui_tx=%lu ui_rx=%lu i_tx=%lu i_rx=%lu queued=%lu q_depth=%u q_drops=%lu retries=%lu t1_expired=%lu retx=%lu rr_rx=%lu rr_tx=%lu rej_rx=%lu rej_tx=%lu srej_tx=%lu srej_rx=%lu fcs_drops=%lu\n",
                stateName(state_),
                static_cast<unsigned long>(stats_.uiTx),
                static_cast<unsigned long>(stats_.uiRx),
                static_cast<unsigned long>(stats_.iTx),
                static_cast<unsigned long>(stats_.iRx),
                static_cast<unsigned long>(stats_.queued),
                static_cast<unsigned>(txQueue_.size()),
                static_cast<unsigned long>(stats_.queueDrops),
                static_cast<unsigned long>(stats_.retries),
                static_cast<unsigned long>(stats_.t1Expired),
                static_cast<unsigned long>(stats_.retx),
                static_cast<unsigned long>(stats_.rrRx),
                static_cast<unsigned long>(stats_.rrTx),
                static_cast<unsigned long>(stats_.rejRx),
                static_cast<unsigned long>(stats_.rejTx),
                static_cast<unsigned long>(stats_.srejTx),
                static_cast<unsigned long>(stats_.srejRx),
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
