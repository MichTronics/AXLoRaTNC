#include "tnc.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include "aprs/aprs.h"
#include "ax25/ax25_fcs.h"
#include "ax25/ax25_frame.h"
#include "util/crc.h"
#include "util/log.h"
#include "util/timer.h"

namespace axlora::tnc {
namespace {

static void formatUptime(uint32_t ms, char* buf, size_t cap) {
  const uint32_t s = ms / 1000;
  snprintf(buf, cap, "%lu:%02lu:%02lu",
           static_cast<unsigned long>(s / 3600),
           static_cast<unsigned long>((s % 3600) / 60),
           static_cast<unsigned long>(s % 60));
}

void addressToText(const ax25::Address& address, char* out, size_t outCap) {
  ax25::formatAddress(address, out, outCap);
}

bool textToAddress(const char* text, ax25::Address& out) {
  return text != nullptr && text[0] != '\0' && ax25::parseAddress(text, out);
}

bool isUiFrame(const ax25::Frame& frame) {
  return ax25::kind(frame.control) == ax25::FrameKind::U &&
         ax25::uType(frame.control) == ax25::UFrameType::UI;
}

static size_t formatMonitorHeader(const ax25::Frame& frame, float rssi, float snr,
                                  char* buf, size_t cap) {
  size_t pos = 0;
  auto app = [&](const char* s) {
    for (; *s != '\0' && pos + 1 < cap; ++s) buf[pos++] = *s;
  };
  char src[12]{}, dst[12]{};
  ax25::formatAddress(frame.source,      src, sizeof(src));
  ax25::formatAddress(frame.destination, dst, sizeof(dst));
  app("fm "); app(src); app(" to "); app(dst);
  for (uint8_t i = 0; i < frame.repeaterCount; ++i) {
    char rep[12]{};
    ax25::formatAddress(frame.repeaters[i], rep, sizeof(rep));
    app(i == 0 ? " via " : ","); app(rep);
  }
  char meta[64]{};
  switch (ax25::kind(frame.control)) {
    case ax25::FrameKind::I:
      snprintf(meta, sizeof(meta), " ctl I ns %u nr %u pid %02X rssi %.0f snr %.0f",
               ax25::ns(frame.control), ax25::nr(frame.control), frame.pid,
               static_cast<double>(rssi), static_cast<double>(snr));
      break;
    case ax25::FrameKind::S:
      snprintf(meta, sizeof(meta), " ctl S%u nr %u rssi %.0f snr %.0f",
               static_cast<unsigned>((frame.control >> 2) & 0x03), ax25::nr(frame.control),
               static_cast<double>(rssi), static_cast<double>(snr));
      break;
    default:
      snprintf(meta, sizeof(meta), " ctl U pid %02X rssi %.0f snr %.0f",
               frame.pid, static_cast<double>(rssi), static_cast<double>(snr));
      break;
  }
  app(meta);
  if (pos < cap) buf[pos] = '\0';
  return pos;
}

static const char* skipSpaces(const char* s) {
  while (s != nullptr && *s == ' ') ++s;
  return s;
}

static bool parseByteValue(const char* s, uint8_t& out) {
  s = skipSpaces(s);
  if (s == nullptr || *s == '\0') return false;
  char* end = nullptr;
  const long value = strtol(s, &end, 10);
  if (end == s || value < 0 || value > 255) return false;
  out = static_cast<uint8_t>(value);
  return true;
}

static void formatByteParam(char prefix, uint8_t value, char* out, size_t cap) {
  snprintf(out, cap, "%c %u", prefix, static_cast<unsigned>(value));
}

static bool isValidBaud(uint32_t baud) {
  static constexpr uint32_t valid[] = {
    300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
  };
  for (auto v : valid) { if (baud == v) return true; }
  return false;
}


}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Tnc::begin(const char* callsign) {
  loadSettings();
  applySerialBaud();
  mailbox_.begin();
  // Prefer NVS callsign over compile-time default
  const char* cs = (savedCallsign_[0] != '\0') ? savedCallsign_ : callsign;
  if (!ax25::parseAddress(cs, local_)) {
    ax25::parseAddress("N0CALL", local_);
  }
  if (beacon_.destination.callsign[0] == '\0') {
    ax25::parseAddress("CQ", beacon_.destination);
  }
  if (dedUnprotoDestination_.callsign[0] == '\0') {
    ax25::parseAddress("CQ", dedUnprotoDestination_);
  }
  if (beacon_.text[0] == '\0') {
    strncpy(beacon_.text, "AXLoRaTNC LoRa AX.25", sizeof(beacon_.text) - 1);
  }
  if (netrom_.alias[0] == '\0') {
    memcpy(netrom_.alias, "AXLORA", 6);
    netrom_.alias[6] = '\0';
  }
  if (netrom_.ident[0] == '\0') {
    strncpy(netrom_.ident, "AXLoRaTNC NET/ROM node", sizeof(netrom_.ident) - 1);
  }
  ax25::L2Config config{};
  config.local = local_;
  config.t1Ms = static_cast<uint32_t>(dedFrack_) * 10UL;
  config.t2Ms = static_cast<uint32_t>(dedT2_) * 10UL;
  config.t3Ms = static_cast<uint32_t>(dedT3_) * 10UL;
  config.n2 = dedRetryLimit_;
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    channelCtx_[i].tnc   = this;
    channelCtx_[i].chIdx = i;
    channels_[i].link.begin(config, radioTxCallback, dataCallback, &channelCtx_[i]);
    channels_[i].link.setMaxFrame(dedMaxFrame_);
  }
}

void Tnc::loop(bool radioReady) {
  serviceSerial();
  if (radioReady) {
    if (!radioConfigApplied_) {
      applyRadioConfig();
      radioConfigApplied_ = true;
    }
    serviceRadio();
  }
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    channels_[i].link.loop();
  }
  checkLinkStatusEvents();
  if (serialMode_ == SerialMode::Wa8ded && !dedHostMode_) {
    serviceDedTerminalOutput();
  }
  if (radioReady) {
    serviceBeacon(radioReady);
    serviceNetrom(radioReady);
    serviceRawTx(radioReady);
  }
}

// ---------------------------------------------------------------------------
// Radio TX / RX
// ---------------------------------------------------------------------------

bool Tnc::isChannelBusy() const {
  if (lastRxMs_ == 0 || kissParams_.fullDuplex != 0) return false;
  const uint32_t windowMs = static_cast<uint32_t>(kissParams_.slotTime) * 10;
  return !axlora::util::elapsed(axlora::util::nowMs(), lastRxMs_, windowMs < 100 ? 100 : windowMs);
}

bool Tnc::transmitRaw(const uint8_t* data, size_t len) {
  if (!dedTxEnabled_) {
    return false;
  }
  const uint32_t delayMs = (kissParams_.fullDuplex == 0)
      ? static_cast<uint32_t>(kissParams_.txDelay) * 10
      : 0;
  return enqueueRawTx(data, len, delayMs);
}

bool Tnc::enqueueRawTx(const uint8_t* data, size_t len, uint32_t delayMs) {
  if (data == nullptr || len == 0 || len > MAX_PACKET_BYTES) {
    ++rawTxQueueDrops_;
    return false;
  }

  PendingRawTx tx{};
  memcpy(tx.data, data, len);
  tx.len = len;
  tx.notBeforeMs = axlora::util::nowMs() + delayMs;

  if (!rawTxQueue_.push(tx)) {
    ++rawTxQueueDrops_;
    LOG_WARN("radio tx queue full");
    return false;
  }
  ++rawTxQueued_;
  return true;
}

void Tnc::serviceRawTx(bool radioReady) {
  if (!radioReady) return;
  const uint32_t now = axlora::util::nowMs();
  if (static_cast<int32_t>(now - nextRawTxAttemptMs_) < 0) return;

  PendingRawTx tx{};
  if (!rawTxQueue_.peek(tx)) return;
  if (static_cast<int32_t>(now - tx.notBeforeMs) < 0) {
    nextRawTxAttemptMs_ = tx.notBeforeMs;
    return;
  }

  if (kissParams_.fullDuplex == 0) {
    const uint32_t slotMs = static_cast<uint32_t>(kissParams_.slotTime) * 10;
    if (isChannelBusy() ||
        static_cast<uint8_t>(random(256)) > kissParams_.persistence) {
      ++rawTxDeferred_;
      nextRawTxAttemptMs_ = now + (slotMs < 10 ? 10 : slotMs);
      return;
    }
  }

  const radio::Result result = radio::driver().send(tx.data, tx.len);
  if (result == radio::Result::Ok) {
    rawTxQueue_.pop(tx);
    ++rawTx_;
    nextRawTxAttemptMs_ = axlora::util::nowMs();
    return;
  }
  if (result == radio::Result::Busy) {
    ++rawTxDeferred_;
    nextRawTxAttemptMs_ = axlora::util::nowMs() + 50;
    return;
  }
  LOG_WARN("radio tx failed: %s", radio::resultName(result));
  rawTxQueue_.pop(tx);
  ++rawTxQueueDrops_;
}

bool Tnc::radioTxCallback(const uint8_t* data, size_t len, void* ctx) {
  ChannelCtx* ch = static_cast<ChannelCtx*>(ctx);
  return ch->tnc->transmitRaw(data, len);
}

void Tnc::dataCallback(const uint8_t* data, size_t len, bool connected, void* ctx) {
  ChannelCtx* ch   = static_cast<ChannelCtx*>(ctx);
  Tnc*        self = ch->tnc;
  const uint8_t chIdx   = ch->chIdx;
  const uint8_t channel = chIdx + 1;   // 1-based for WA8DED

  if (self->serialMode_ == SerialMode::Wa8ded) {
    if (!self->dedHostMode_ && self->dedCtextMode_ == 2 && connected && len >= 3 &&
        data[0] == '/' && data[1] == '/' && data[2] == 'Q') {
      self->disconnect(chIdx);
      return;
    }
    self->enqueueDedEvent(channel, connected ? 7 : 6, data, len);
    return;
  }
  if (connected) {
    self->processNodeInput(chIdx, data, len);
    return;
  }
  if (axlora::util::logEnabled()) {
    Serial.printf("[AX25/UI ch%u] ", channel);
    for (size_t i = 0; i < len; ++i) Serial.write(data[i]);
    Serial.println();
  }
}

int Tnc::findChannelForIncoming(const ax25::Frame& frame) const {
  // UI frames are handled at the TNC level, not routed to a connected LinkLayer
  if (ax25::kind(frame.control) == ax25::FrameKind::U &&
      ax25::uType(frame.control) == ax25::UFrameType::UI) {
    return -1;
  }
  // Find channel with matching peer in a non-Disconnected state
  for (int i = 0; i < static_cast<int>(CHANNEL_COUNT); ++i) {
    if (channels_[i].link.state() != ax25::LinkState::Disconnected &&
        ax25::addressEquals(channels_[i].link.peer(), frame.source)) {
      return i;
    }
  }
  // New SABM: assign to first available (Disconnected) channel
  if (ax25::kind(frame.control) == ax25::FrameKind::U &&
      ax25::uType(frame.control) == ax25::UFrameType::SABM) {
    uint8_t active = 0;
    for (int i = 0; i < static_cast<int>(CHANNEL_COUNT); ++i) {
      if (channels_[i].link.state() != ax25::LinkState::Disconnected) ++active;
    }
    if (active >= dedMaxIncoming_) return -1;
    for (int i = 0; i < static_cast<int>(CHANNEL_COUNT); ++i) {
      if (channels_[i].link.state() == ax25::LinkState::Disconnected) {
        return i;
      }
    }
  }
  return -1;
}

void Tnc::serviceRadio() {
  radio::RxPacket rx{};
  if (radio::driver().receive(rx) != radio::Result::Ok) return;
  lastRxMs_ = axlora::util::nowMs();
  lastRssi_ = rx.rssi;
  lastSnr_  = rx.snr;
  ++rawRx_;
  if (rx.len >= 2 && ax25::checkFcs(rx.data, rx.len)) {
    emitKissData(rx.data, rx.len - 2);
    if (serialMode_ == SerialMode::Kiss) return;
    ax25::Frame frame{};
    if (ax25::decodeFrame(rx.data, rx.len, frame, true)) {
      // Detect APRS (UI, PID=0xF0) and log parsed content
      if (ax25::kind(frame.control) == ax25::FrameKind::U &&
          ax25::uType(frame.control) == ax25::UFrameType::UI &&
          frame.pid == ax25::PID_NO_LAYER3 && frame.infoLen > 0) {
        char aprsSum[80]{};
        if (aprs::parse(frame.info, frame.infoLen, aprsSum, sizeof(aprsSum))) {
          char src[12]{};
          ax25::formatAddress(frame.source, src, sizeof(src));
          LOG_INFO("APRS %s: %s", src, aprsSum);
        }
        // Auto-ACK APRS messages addressed to our callsign
        if (frame.infoLen > 0 && static_cast<char>(frame.info[0]) == ':') {
          aprs::AprsMessage msg{};
          if (aprs::parseMessage(frame.info, frame.infoLen, msg) &&
              !msg.isAck && !msg.isRej && msg.msgNum[0] != '\0') {
            char localStr[12]{};
            ax25::formatAddress(local_, localStr, sizeof(localStr));
            if (strncasecmp(msg.addressee, localStr, strlen(localStr)) == 0) {
              char ackInfo[32]{};
              char senderStr[12]{};
              ax25::formatAddress(frame.source, senderStr, sizeof(senderStr));
              if (aprs::encodeMessageAck(senderStr, msg.msgNum, ackInfo, sizeof(ackInfo))) {
                channels_[0].link.sendUi(frame.source,
                  reinterpret_cast<const uint8_t*>(ackInfo), strlen(ackInfo));
                LOG_INFO("APRS ACK sent to %s msgnum=%s", senderStr, msg.msgNum);
              }
            }
          }
        }
      }
      observeHeard(frame, rx.rssi, rx.snr);
      observeNetrom(frame);
      maybeDigipeat(frame);
      if (monitorEnabled_ && serialMode_ == SerialMode::Wa8ded) {
        // Apply dedMonitorMode_ frame-type filter (I=I-frames, U=UI-frames, S=supervisory).
        // A missing filter letter means that frame type is suppressed.
        const ax25::FrameKind fk = ax25::kind(frame.control);
        bool typeAllowed = false;
        if (fk == ax25::FrameKind::I) {
          typeAllowed = (strchr(dedMonitorMode_, 'I') != nullptr);
        } else if (fk == ax25::FrameKind::U) {
          typeAllowed = (strchr(dedMonitorMode_, 'U') != nullptr);
        } else if (fk == ax25::FrameKind::S) {
          typeAllowed = (strchr(dedMonitorMode_, 'S') != nullptr);
        }

        if (typeAllowed) {
          // Flood guard: reserve half the event queue for link-status/data events,
          // and limit monitor output to 8 events per second.
          constexpr size_t MONITOR_QUEUE_HEADROOM = 16;  // out of 32 slots
          constexpr uint32_t MONITOR_MIN_INTERVAL_MS = 125; // max 8/sec
          const uint32_t nowMs = axlora::util::nowMs();
          const bool queueOk = dedEvents_.size() <= (32 - MONITOR_QUEUE_HEADROOM);
          const bool rateOk  = axlora::util::elapsed(nowMs, monitorLastEnqueueMs_, MONITOR_MIN_INTERVAL_MS);
          if (queueOk && rateOk) {
            monitorLastEnqueueMs_ = nowMs;
            char header[160]{};
            const size_t headerLen = formatMonitorHeader(frame, rx.rssi, rx.snr, header, sizeof(header));
            if (frame.infoLen > 0) {
              enqueueDedEvent(0, 5, reinterpret_cast<const uint8_t*>(header), headerLen);
              enqueueDedEvent(0, 6, frame.info, frame.infoLen);
            } else {
              enqueueDedEvent(0, 4, reinterpret_cast<const uint8_t*>(header), headerLen);
            }
          }
        }
      }
      const int chIdx = findChannelForIncoming(frame);
      if (chIdx >= 0) {
        channels_[chIdx].link.receive(rx.data, rx.len);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Beacon / NET/ROM
// ---------------------------------------------------------------------------

void Tnc::serviceBeacon(bool radioReady) {
  if (!radioReady || !beacon_.enabled) return;
  const uint32_t now = axlora::util::nowMs();
  if (beacon_.lastTxMs != 0 && !axlora::util::elapsed(now, beacon_.lastTxMs, beacon_.intervalMs)) return;
  beacon_.lastTxMs = now;
  sendBeacon();
}

void Tnc::serviceNetrom(bool radioReady) {
  if (!radioReady || !netrom_.enabled) return;
  const uint32_t now = axlora::util::nowMs();
  // Expire routes not heard within 6 broadcast intervals (NET/ROM obsolescence)
  const uint32_t expiryMs = netrom_.broadcastIntervalMs * 6;
  for (NetromRoute& route : netromRoutes_) {
    if (route.active && axlora::util::elapsed(now, route.lastHeardMs, expiryMs)) {
      route.active = false;
    }
  }
  if (netrom_.lastBroadcastMs != 0 &&
      !axlora::util::elapsed(now, netrom_.lastBroadcastMs, netrom_.broadcastIntervalMs)) return;
  netrom_.lastBroadcastMs = now;
  sendNetromBroadcast();
}

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------

void Tnc::serviceSerial() {
  while (Serial.available() > 0) {
    const uint8_t b = static_cast<uint8_t>(Serial.read());
    if (serialMode_ == SerialMode::Wa8ded) {
      serviceWa8ded(b);
      handleQuietEscape(b);
      continue;
    }
    ax25::KissFrame frame{};
    if (b == ax25::KISS_FEND) kissActive_ = true;
    if (kissActive_) {
      if (kiss_.feed(b, frame)) {
        handleKiss(frame);
        kissActive_ = false;
      }
      continue;
    }
    if (serialMode_ == SerialMode::Kiss) {
      handleQuietEscape(b);
      continue;
    }
    if (b == '\r') continue;
    if (b == '\n') {
      line_[linePos_] = '\0';
      handleConsoleLine(line_);
      linePos_ = 0;
      continue;
    }
    if (linePos_ < sizeof(line_) - 1) line_[linePos_++] = static_cast<char>(b);
  }
}

void Tnc::handleKiss(const ax25::KissFrame& frame) {
  if (frame.command == ax25::KissCommand::Data) {
    uint8_t withFcs[MAX_PACKET_BYTES]{};
    size_t len = 0;
    if (ax25::appendFcs(frame.data, frame.len, withFcs, sizeof(withFcs), len)) {
      transmitRaw(withFcs, len);
    }
    return;
  }
  if (frame.len < 1) return;
  switch (frame.command) {
    case ax25::KissCommand::TxDelay:     kissParams_.txDelay     = frame.data[0]; break;
    case ax25::KissCommand::Persistence: kissParams_.persistence = frame.data[0]; break;
    case ax25::KissCommand::SlotTime:    kissParams_.slotTime    = frame.data[0]; break;
    case ax25::KissCommand::FullDuplex:  kissParams_.fullDuplex  = frame.data[0]; break;
    default: break;
  }
}

void Tnc::emitKissData(const uint8_t* frameNoFcs, size_t len) {
  ax25::KissFrame kissFrame{};
  kissFrame.command = ax25::KissCommand::Data;
  kissFrame.len = len > sizeof(kissFrame.data) ? sizeof(kissFrame.data) : len;
  memcpy(kissFrame.data, frameNoFcs, kissFrame.len);
  uint8_t encoded[(MAX_PACKET_BYTES * 2) + 4]{};
  size_t encodedLen = 0;
  if (ax25::encodeKiss(kissFrame, encoded, sizeof(encoded), encodedLen)) {
    Serial.write(encoded, encodedLen);
  }
}

// ---------------------------------------------------------------------------
// Console command handler
// ---------------------------------------------------------------------------

void Tnc::handleConsoleLine(char* line) {
  char* cmd = strtok(line, " ");
  if (cmd == nullptr) return;

  if (strcmp(cmd, "help") == 0) {
    Serial.println("Commands: help info callsign mode baud stats bbs");
    Serial.println("          radio [freq|corr|sf|bw|cr|power|reset] [val]");
    Serial.println("          duty [on|off|percent]");
    Serial.println("          profile [fast|normal]");
    Serial.println("          ax25 connect [ch] <CALL> disconnect [ch]");
    Serial.println("          send [ch] <msg> sendui <DEST> <msg>");
    Serial.println("          beacon [on|off|now|text|dest|interval|path]");
    Serial.println("          mheard [clear] digi [on|off|mode ui|all] digialias [CALL|off]");
    Serial.println("          node [on|off|alias|ident|interval|broadcast] nodes routes");

  } else if (strcmp(cmd, "info") == 0) {
    printInfo();

  } else if (strcmp(cmd, "mode") == 0) {
    char* mode = strtok(nullptr, " ");
    if (mode == nullptr) {
      Serial.printf("mode=%s\n", serialModeName());
    } else if (strcmp(mode, "kiss") == 0) {
      Serial.println("mode=kiss"); Serial.flush();
      setSerialMode(SerialMode::Kiss); saveSerialMode(SerialMode::Kiss);
    } else if (strcmp(mode, "ded") == 0 || strcmp(mode, "wa8ded") == 0) {
      Serial.println("mode=ded"); Serial.flush();
      setSerialMode(SerialMode::Wa8ded); saveSerialMode(SerialMode::Wa8ded);
    } else if (strcmp(mode, "console") == 0) {
      setSerialMode(SerialMode::Console); saveSerialMode(SerialMode::Console);
      Serial.println("mode=console");
    } else {
      Serial.println("usage: mode [console|kiss|ded]");
    }

  } else if (strcmp(cmd, "baud") == 0) {
    char* modeArg = strtok(nullptr, " ");
    char* baudArg = strtok(nullptr, " ");
    if (modeArg == nullptr) {
      Serial.printf("baud console=%lu (fixed)\n", static_cast<unsigned long>(variant::SERIAL_BAUD));
      Serial.printf("baud kiss=%lu\n",    static_cast<unsigned long>(baudKiss_));
      Serial.printf("baud ded=%lu\n",     static_cast<unsigned long>(baudDed_));
    } else if (baudArg == nullptr) {
      Serial.println("usage: baud <kiss|ded> <rate>");
      Serial.println("valid: 300 1200 2400 4800 9600 19200 38400 57600 115200 230400 460800 921600");
    } else {
      const uint32_t rate = static_cast<uint32_t>(atol(baudArg));
      if (!isValidBaud(rate)) {
        Serial.println("invalid baud rate");
      } else if (strcmp(modeArg, "kiss") == 0) {
        baudKiss_ = rate;
        Preferences prefs;
        if (prefs.begin("axloratnc", false)) { prefs.putULong("baud_kiss", baudKiss_); prefs.end(); }
        Serial.printf("baud kiss=%lu\n", static_cast<unsigned long>(baudKiss_));
        if (serialMode_ == SerialMode::Kiss) applySerialBaud();
      } else if (strcmp(modeArg, "ded") == 0 || strcmp(modeArg, "wa8ded") == 0) {
        baudDed_ = rate;
        Preferences prefs;
        if (prefs.begin("axloratnc", false)) { prefs.putULong("baud_ded", baudDed_); prefs.end(); }
        Serial.printf("baud ded=%lu\n", static_cast<unsigned long>(baudDed_));
        if (serialMode_ == SerialMode::Wa8ded) applySerialBaud();
      } else {
        Serial.println("usage: baud <kiss|ded> <rate>");
      }
    }

  } else if (strcmp(cmd, "callsign") == 0) {
    char* cs = strtok(nullptr, " ");
    char addrStr[12]{};
    if (cs == nullptr) {
      ax25::formatAddress(local_, addrStr, sizeof(addrStr));
      Serial.printf("callsign=%s\n", addrStr);
    } else {
      ax25::Address newAddr{};
      if (!ax25::parseAddress(cs, newAddr)) {
        Serial.println("invalid callsign");
      } else {
        local_ = newAddr;
        ax25::L2Config cfg{};
        cfg.local = local_;
        for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
          channels_[i].link.begin(cfg, radioTxCallback, dataCallback, &channelCtx_[i]);
        }
        ax25::formatAddress(local_, savedCallsign_, sizeof(savedCallsign_));
        Preferences prefs;
        if (prefs.begin("axloratnc", false)) {
          prefs.putString("callsign", savedCallsign_);
          prefs.end();
        }
        Serial.printf("callsign=%s\n", savedCallsign_);
      }
    }

  } else if (strcmp(cmd, "radio") == 0) {
    char* sub = strtok(nullptr, " ");
    if (sub == nullptr) {
      Serial.printf("freq=%.3f corr=%+.1f kHz bw=%.1f sf=%u cr=4/%u pwr=%d RSSI=%.1f SNR=%.1f\n",
                    static_cast<double>(radioConfig_.frequencyMHz),
                    static_cast<double>(radioConfig_.frequencyCorrectionMHz * 1000.0f),
                    static_cast<double>(radioConfig_.bandwidthKhz),
                    radioConfig_.spreadingFactor, radioConfig_.codingRate,
                    radioConfig_.powerDbm,
                    static_cast<double>(radio::driver().getRSSI()),
                    static_cast<double>(radio::driver().getSNR()));
    } else if (strcmp(sub, "reset") == 0) {
      radioConfig_.frequencyMHz    = variant::DEFAULT_FREQUENCY_MHZ;
      radioConfig_.frequencyCorrectionMHz = variant::DEFAULT_FREQUENCY_CORRECTION_MHZ;
      radioConfig_.bandwidthKhz    = variant::DEFAULT_BANDWIDTH_KHZ;
      radioConfig_.spreadingFactor = variant::DEFAULT_SPREADING_FACTOR;
      radioConfig_.codingRate      = variant::DEFAULT_CODING_RATE;
      radioConfig_.powerDbm        = variant::DEFAULT_TX_POWER_DBM;
      applyRadioConfig();
      radioConfigApplied_ = true;
      saveRadioConfig();
      Serial.printf("radio reset freq=%.3f corr=%+.1f kHz bw=%.1f sf=%u cr=4/%u pwr=%d\n",
                    static_cast<double>(radioConfig_.frequencyMHz),
                    static_cast<double>(radioConfig_.frequencyCorrectionMHz * 1000.0f),
                    static_cast<double>(radioConfig_.bandwidthKhz),
                    radioConfig_.spreadingFactor, radioConfig_.codingRate,
                    radioConfig_.powerDbm);
    } else if (strcmp(sub, "freq") == 0) {
      char* val = strtok(nullptr, " ");
      if (!val) { Serial.println("usage: radio freq <MHz>"); }
      else {
        radioConfig_.frequencyMHz = static_cast<float>(atof(val));
        radio::driver().setFrequency(radioConfig_.frequencyMHz);
        saveRadioConfig();
        Serial.printf("freq=%.3f MHz\n", static_cast<double>(radioConfig_.frequencyMHz));
      }
    } else if (strcmp(sub, "corr") == 0 || strcmp(sub, "correction") == 0 || strcmp(sub, "offset") == 0) {
      char* val = strtok(nullptr, " ");
      if (!val) {
        Serial.printf("corr=%+.1f kHz\n", static_cast<double>(radioConfig_.frequencyCorrectionMHz * 1000.0f));
      } else if constexpr (variant::RADIO_TYPE != variant::RadioType::SX1276) {
        Serial.println("radio corr is only used by SX1276 variants");
      } else {
        const float correctionKhz = static_cast<float>(atof(val));
        if (correctionKhz < -250.0f || correctionKhz > 250.0f) {
          Serial.println("usage: radio corr <kHz>  (-250..250)");
        } else {
          radioConfig_.frequencyCorrectionMHz = correctionKhz / 1000.0f;
          radio::driver().setFrequencyCorrection(radioConfig_.frequencyCorrectionMHz);
          saveRadioConfig();
          Serial.printf("corr=%+.1f kHz tuned=%.3f MHz\n",
                        static_cast<double>(correctionKhz),
                        static_cast<double>(radioConfig_.frequencyMHz + radioConfig_.frequencyCorrectionMHz));
        }
      }
    } else if (strcmp(sub, "sf") == 0) {
      char* val = strtok(nullptr, " ");
      const long v = val ? atol(val) : 0;
      if (v < 6 || v > 12) { Serial.println("usage: radio sf <6-12>"); }
      else {
        radioConfig_.spreadingFactor = static_cast<uint8_t>(v);
        radio::driver().setSpreadingFactor(radioConfig_.spreadingFactor);
        saveRadioConfig();
        Serial.printf("sf=%u\n", radioConfig_.spreadingFactor);
      }
    } else if (strcmp(sub, "bw") == 0) {
      char* val = strtok(nullptr, " ");
      if (!val) { Serial.println("usage: radio bw <kHz>"); }
      else {
        radioConfig_.bandwidthKhz = static_cast<float>(atof(val));
        radio::driver().setBandwidth(radioConfig_.bandwidthKhz);
        saveRadioConfig();
        Serial.printf("bw=%.1f kHz\n", static_cast<double>(radioConfig_.bandwidthKhz));
      }
    } else if (strcmp(sub, "cr") == 0) {
      char* val = strtok(nullptr, " ");
      const long v = val ? atol(val) : 0;
      if (v < 5 || v > 8) { Serial.println("usage: radio cr <5-8>  (means 4/5..4/8)"); }
      else {
        radioConfig_.codingRate = static_cast<uint8_t>(v);
        radio::driver().setCodingRate(radioConfig_.codingRate);
        saveRadioConfig();
        Serial.printf("cr=4/%u\n", radioConfig_.codingRate);
      }
    } else if (strcmp(sub, "power") == 0 || strcmp(sub, "pwr") == 0) {
      char* val = strtok(nullptr, " ");
      if (!val) { Serial.println("usage: radio power <dBm>"); }
      else {
        radioConfig_.powerDbm = static_cast<int8_t>(atol(val));
        radio::driver().setPower(radioConfig_.powerDbm);
        saveRadioConfig();
        Serial.printf("pwr=%d dBm\n", radioConfig_.powerDbm);
      }
    } else {
      Serial.println("usage: radio [freq|corr|sf|bw|cr|power|reset] [value]");
    }

  } else if (strcmp(cmd, "duty") == 0) {
    char* arg = strtok(nullptr, " ");
    if (arg == nullptr) {
      Serial.printf("duty=%s %.3f%% drops=%lu\n",
                    dutyCycleEnabled_ ? "on" : "off",
                    static_cast<double>(dutyCyclePpm_) / 10000.0,
                    static_cast<unsigned long>(radio::stats().dutyDrops));
    } else if (strcmp(arg, "on") == 0) {
      dutyCycleEnabled_ = true;
      radio::driver().setDutyCycle(dutyCycleEnabled_, dutyCyclePpm_);
      saveRadioConfig();
      Serial.printf("duty=on %.3f%%\n", static_cast<double>(dutyCyclePpm_) / 10000.0);
    } else if (strcmp(arg, "off") == 0) {
      dutyCycleEnabled_ = false;
      radio::driver().setDutyCycle(false, dutyCyclePpm_);
      saveRadioConfig();
      Serial.println("WARNING: duty-cycle guard is OFF. Use only on dummy load/lab setups.");
    } else {
      const float percent = static_cast<float>(atof(arg));
      if (percent <= 0.0f || percent > 100.0f) {
        Serial.println("usage: duty [on|off|percent]");
      } else {
        dutyCyclePpm_ = static_cast<uint32_t>(percent * 10000.0f);
        dutyCycleEnabled_ = true;
        radio::driver().setDutyCycle(true, dutyCyclePpm_);
        saveRadioConfig();
        Serial.printf("duty=on %.3f%%\n", static_cast<double>(dutyCyclePpm_) / 10000.0);
      }
    }

  } else if (strcmp(cmd, "profile") == 0) {
    char* arg = strtok(nullptr, " ");
    if (arg == nullptr) {
      Serial.printf("profile custom duty=%s txdelay=%u p=%u slot=%u fulldup=%u\n",
                    dutyCycleEnabled_ ? "on" : "off",
                    kissParams_.txDelay, kissParams_.persistence,
                    kissParams_.slotTime, kissParams_.fullDuplex);
    } else if (strcmp(arg, "fast") == 0) {
      kissParams_.txDelay    = 0;
      kissParams_.persistence = 255;
      kissParams_.slotTime   = 1;
      kissParams_.fullDuplex = 0;
      dutyCycleEnabled_ = false;
      radio::driver().setDutyCycle(false, dutyCyclePpm_);
      saveRadioConfig();
      Serial.println("profile=fast txdelay=0 p=255 slot=1 fulldup=0 duty=off");
      Serial.println("WARNING: fast profile disables duty-cycle guard. Use only on dummy load/lab setups.");
    } else if (strcmp(arg, "normal") == 0 || strcmp(arg, "default") == 0) {
      kissParams_.txDelay    = 30;
      kissParams_.persistence = 63;
      kissParams_.slotTime   = 10;
      kissParams_.fullDuplex = 0;
      dutyCycleEnabled_ = true;
      dutyCyclePpm_ = variant::DUTY_CYCLE_PPM;
      radio::driver().setDutyCycle(true, dutyCyclePpm_);
      saveRadioConfig();
      Serial.printf("profile=normal txdelay=30 p=63 slot=10 fulldup=0 duty=%.3f%%\n",
                    static_cast<double>(dutyCyclePpm_) / 10000.0);
    } else {
      Serial.println("usage: profile [fast|normal]");
    }

  } else if (strcmp(cmd, "ax25") == 0) {
    for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
      if (channels_[i].link.state() != ax25::LinkState::Disconnected) {
        Serial.printf("ch%u: ", static_cast<unsigned>(i + 1));
        channels_[i].link.printStatus();
      }
    }

  } else if (strcmp(cmd, "node") == 0) {
    char* sub = strtok(nullptr, " ");
    if (sub == nullptr) {
      printNetrom();
    } else if (strcmp(sub, "on") == 0) {
      netrom_.enabled = true; netrom_.lastBroadcastMs = 0;
      saveSettings(); printNetrom();
    } else if (strcmp(sub, "off") == 0) {
      netrom_.enabled = false; saveSettings(); printNetrom();
    } else if (strcmp(sub, "alias") == 0) {
      char* alias = strtok(nullptr, " ");
      if (alias == nullptr || strlen(alias) > 6) {
        Serial.println("usage: node alias <1-6 chars>");
      } else {
        memset(netrom_.alias, 0, sizeof(netrom_.alias));
        strncpy(netrom_.alias, alias, sizeof(netrom_.alias) - 1);
        for (size_t i = 0; netrom_.alias[i] != '\0'; ++i) {
          if (netrom_.alias[i] >= 'a' && netrom_.alias[i] <= 'z')
            netrom_.alias[i] = static_cast<char>(netrom_.alias[i] - 32);
        }
        saveSettings(); printNetrom();
      }
    } else if (strcmp(sub, "ident") == 0) {
      char* ident = strtok(nullptr, "");
      if (ident == nullptr) {
        Serial.println("usage: node ident <text>");
      } else {
        strncpy(netrom_.ident, ident, sizeof(netrom_.ident) - 1);
        netrom_.ident[sizeof(netrom_.ident) - 1] = '\0';
        saveSettings(); printNetrom();
      }
    } else if (strcmp(sub, "interval") == 0) {
      char* secs = strtok(nullptr, " ");
      const long val = secs ? atol(secs) : 0;
      if (val < 60 || val > 86400) {
        Serial.println("usage: node interval <60-86400>");
      } else {
        netrom_.broadcastIntervalMs = static_cast<uint32_t>(val) * 1000UL;
        saveSettings(); printNetrom();
      }
    } else if (strcmp(sub, "broadcast") == 0) {
      Serial.println(sendNetromBroadcast() ? "node broadcast sent" : "node broadcast failed");
    } else {
      Serial.println("usage: node [on|off|alias|ident|interval|broadcast]");
    }

  } else if (strcmp(cmd, "nodes") == 0 || strcmp(cmd, "routes") == 0) {
    printNetromRoutes();

  } else if (strcmp(cmd, "beacon") == 0) {
    char* sub = strtok(nullptr, " ");
    if (sub == nullptr) {
      printBeacon();
    } else if (strcmp(sub, "on") == 0) {
      beacon_.enabled = true; beacon_.lastTxMs = 0; saveSettings(); printBeacon();
    } else if (strcmp(sub, "off") == 0) {
      beacon_.enabled = false; saveSettings(); printBeacon();
    } else if (strcmp(sub, "now") == 0) {
      Serial.println(sendBeacon() ? "beacon sent" : "beacon failed");
    } else if (strcmp(sub, "text") == 0) {
      char* text = strtok(nullptr, "");
      if (!text) { Serial.println("usage: beacon text <text>"); }
      else {
        strncpy(beacon_.text, text, sizeof(beacon_.text) - 1);
        beacon_.text[sizeof(beacon_.text) - 1] = '\0';
        saveSettings(); printBeacon();
      }
    } else if (strcmp(sub, "dest") == 0) {
      char* dest = strtok(nullptr, " ");
      if (!dest || !ax25::parseAddress(dest, beacon_.destination)) {
        Serial.println("usage: beacon dest <CALLSIGN-SSID>");
      } else { saveSettings(); printBeacon(); }
    } else if (strcmp(sub, "interval") == 0) {
      char* secs = strtok(nullptr, " ");
      const long val = secs ? atol(secs) : 0;
      if (val < 10 || val > 86400) { Serial.println("usage: beacon interval <10-86400>"); }
      else {
        beacon_.intervalMs = static_cast<uint32_t>(val) * 1000UL;
        saveSettings(); printBeacon();
      }
    } else if (strcmp(sub, "path") == 0) {
      char* path = strtok(nullptr, "");
      if (!setBeaconPath(path)) { Serial.println("usage: beacon path <CALL1,CALL2|off>"); }
      else { saveSettings(); printBeacon(); }
    } else if (strcmp(sub, "aprs") == 0) {
      // beacon aprs <lat> <lon> [symbol] [comment]
      // Formats beacon text as APRS position and sets dest to APRS
      char* latStr = strtok(nullptr, " ");
      char* lonStr = strtok(nullptr, " ");
      if (!latStr || !lonStr) {
        Serial.println("usage: beacon aprs <lat> <lon> [sym2] [comment]");
        Serial.println("  e.g. beacon aprs 52.0167 4.7000 /> LoRa TNC");
      } else {
        const double lat = atof(latStr);
        const double lon = atof(lonStr);
        char* sym  = strtok(nullptr, " ");
        char* cmt  = strtok(nullptr, "");
        char symBuf[3] = {'/', '>'};
        if (sym && strlen(sym) >= 2) { symBuf[0] = sym[0]; symBuf[1] = sym[1]; }
        char aprsInfo[96]{};
        if (aprs::encodePosition(lat, lon, symBuf, cmt ? cmt : "", aprsInfo, sizeof(aprsInfo))) {
          ax25::parseAddress("APRS", beacon_.destination);
          strncpy(beacon_.text, aprsInfo, sizeof(beacon_.text) - 1);
          beacon_.text[sizeof(beacon_.text) - 1] = '\0';
          beacon_.enabled = true;
          saveSettings();
          printBeacon();
        } else {
          Serial.println("aprs encode failed");
        }
      }
    } else {
      Serial.println("usage: beacon [on|off|now|text|dest|interval|path|aprs]");
    }

  } else if (strcmp(cmd, "mheard") == 0) {
    char* sub = strtok(nullptr, " ");
    if (sub && strcmp(sub, "clear") == 0) { clearMheard(); Serial.println("mheard cleared"); }
    else printMheard();

  } else if (strcmp(cmd, "digi") == 0) {
    char* mode = strtok(nullptr, " ");
    if (!mode) { printDigipeater(); }
    else if (strcmp(mode, "on") == 0)  { digi_.enabled = true;  saveSettings(); printDigipeater(); }
    else if (strcmp(mode, "off") == 0) { digi_.enabled = false; saveSettings(); printDigipeater(); }
    else if (strcmp(mode, "mode") == 0) {
      char* value = strtok(nullptr, " ");
      if (value && strcmp(value, "ui") == 0) {
        digi_.allFrames = false; saveSettings(); printDigipeater();
      } else if (value && strcmp(value, "all") == 0) {
        digi_.allFrames = true; saveSettings(); printDigipeater();
      } else {
        Serial.println("usage: digi mode <ui|all>");
      }
    }
    else Serial.println("usage: digi [on|off|mode ui|all]");

  } else if (strcmp(cmd, "digialias") == 0) {
    char* value = strtok(nullptr, " ");
    if (!value) { printDigipeater(); }
    else if (strcmp(value, "off") == 0) { digi_.hasAlias = false; saveSettings(); printDigipeater(); }
    else if (ax25::parseAddress(value, digi_.alias)) { digi_.hasAlias = true; saveSettings(); printDigipeater(); }
    else Serial.println("usage: digialias <CALLSIGN-SSID|off>");

  } else if (strcmp(cmd, "connect") == 0) {
    // connect [ch] <CALL>
    char* arg1 = strtok(nullptr, " ");
    char* arg2 = strtok(nullptr, " ");
    uint8_t chIdx = 0;
    const char* dest = arg1;
    if (arg2 != nullptr) {
      const long ch = atol(arg1);
      if (ch >= 1 && ch <= static_cast<long>(CHANNEL_COUNT)) {
        chIdx = static_cast<uint8_t>(ch - 1);
        dest  = arg2;
      }
    }
    Serial.println(connect(chIdx, dest) ? "connecting" : "connect failed");

  } else if (strcmp(cmd, "disconnect") == 0) {
    // disconnect [ch]
    char* arg = strtok(nullptr, " ");
    uint8_t chIdx = 0;
    if (arg != nullptr) {
      const long ch = atol(arg);
      if (ch >= 1 && ch <= static_cast<long>(CHANNEL_COUNT)) chIdx = static_cast<uint8_t>(ch - 1);
    }
    Serial.println(disconnect(chIdx) ? "disconnecting" : "disconnect failed");

  } else if (strcmp(cmd, "sendui") == 0) {
    char* dest = strtok(nullptr, " ");
    char* msg  = strtok(nullptr, "");
    Serial.println(sendUi(dest, msg) ? "ui queued" : "sendui failed");

  } else if (strcmp(cmd, "send") == 0) {
    // send [ch] <msg>
    char* arg1 = strtok(nullptr, " ");
    char* arg2 = strtok(nullptr, "");
    uint8_t chIdx = 0;
    const char* msg = arg1;
    if (arg2 != nullptr) {
      const long ch = atol(arg1);
      if (ch >= 1 && ch <= static_cast<long>(CHANNEL_COUNT)) {
        chIdx = static_cast<uint8_t>(ch - 1);
        msg   = arg2;
      }
    }
    if (sendConnected(chIdx, msg)) {
      Serial.printf("i queued ch%u depth=%u\n",
                    static_cast<unsigned>(chIdx + 1),
                    static_cast<unsigned>(channels_[chIdx].link.connectedQueueSize()));
    } else {
      Serial.println("send failed");
    }

  } else if (strcmp(cmd, "bbs") == 0) {
    // Local BBS list for sysop inspection
    char buf[512]{};
    mailbox_.list(buf, sizeof(buf));
    Serial.print(buf);

  } else if (strcmp(cmd, "stats") == 0) {
    printStats();

  } else {
    Serial.println("unknown command");
  }
}

// ---------------------------------------------------------------------------
// Info / Stats
// ---------------------------------------------------------------------------

void Tnc::printInfo() const {
  char local[12]{};
  ax25::formatAddress(local_, local, sizeof(local));
  Serial.printf("AXLoRaTNC local=%s variant=%s radio=%s\n",
                local, variant::NAME,
                variant::RADIO_TYPE == variant::RadioType::SX1262 ? "SX1262" : "SX1276");
  Serial.printf("  freq=%.3f MHz bw=%.1f kHz sf=%u cr=4/%u pwr=%d dBm\n",
                static_cast<double>(radioConfig_.frequencyMHz),
                static_cast<double>(radioConfig_.bandwidthKhz),
                radioConfig_.spreadingFactor, radioConfig_.codingRate,
                radioConfig_.powerDbm);
  Serial.printf("  freq_corr=%+.1f kHz tuned=%.3f MHz\n",
                static_cast<double>(radioConfig_.frequencyCorrectionMHz * 1000.0f),
                static_cast<double>(radioConfig_.frequencyMHz + radioConfig_.frequencyCorrectionMHz));
  Serial.printf("  duty=%s %.3f%%\n", dutyCycleEnabled_ ? "on" : "off",
                static_cast<double>(dutyCyclePpm_) / 10000.0);
  Serial.printf("serial mode=%s channels=%u baud_kiss=%lu baud_ded=%lu\n",
                serialModeName(), static_cast<unsigned>(CHANNEL_COUNT),
                static_cast<unsigned long>(baudKiss_), static_cast<unsigned long>(baudDed_));
}

void Tnc::printStats() const {
  const radio::Stats& rs = radio::stats();
  Serial.printf("tnc raw_tx=%lu raw_rx=%lu txq=%u queued=%lu deferred=%lu qdrops=%lu kiss_txdelay=%u p=%u slot=%u fulldup=%u\n",
                static_cast<unsigned long>(rawTx_), static_cast<unsigned long>(rawRx_),
                static_cast<unsigned>(rawTxQueue_.size()),
                static_cast<unsigned long>(rawTxQueued_),
                static_cast<unsigned long>(rawTxDeferred_),
                static_cast<unsigned long>(rawTxQueueDrops_),
                kissParams_.txDelay, kissParams_.persistence, kissParams_.slotTime, kissParams_.fullDuplex);
  Serial.printf("digi enabled=%u mode=%s tx=%lu ui_tx=%lu conn_tx=%lu dupes=%lu drops=%lu\n",
                digi_.enabled, digi_.allFrames ? "all" : "ui",
                static_cast<unsigned long>(digiTx_),
                static_cast<unsigned long>(digiUiTx_),
                static_cast<unsigned long>(digiConnTx_),
                static_cast<unsigned long>(digiDupes_),
                static_cast<unsigned long>(digiDrops_));
  Serial.printf("beacon enabled=%u tx=%lu drops=%lu interval_s=%lu\n",
                beacon_.enabled,
                static_cast<unsigned long>(beaconTx_),
                static_cast<unsigned long>(beaconDrops_),
                static_cast<unsigned long>(beacon_.intervalMs / 1000));
  Serial.printf("netrom enabled=%u alias=%s broadcasts=%lu routes_heard=%lu\n",
                netrom_.enabled, netrom_.alias,
                static_cast<unsigned long>(netromBroadcasts_),
                static_cast<unsigned long>(netromRoutesHeard_));
  Serial.printf("radio tx_ok=%lu tx_fail=%lu rx_ok=%lu rx_fail=%lu duty_drops=%lu duty=%s %.3f%%\n",
                static_cast<unsigned long>(rs.txOk), static_cast<unsigned long>(rs.txFail),
                static_cast<unsigned long>(rs.rxOk), static_cast<unsigned long>(rs.rxFail),
                static_cast<unsigned long>(rs.dutyDrops),
                dutyCycleEnabled_ ? "on" : "off",
                static_cast<double>(dutyCyclePpm_) / 10000.0);
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    if (channels_[i].link.state() != ax25::LinkState::Disconnected) {
      Serial.printf("ch%u: ", static_cast<unsigned>(i + 1));
      channels_[i].link.printStats();
    }
  }
}

// ---------------------------------------------------------------------------
// Connect / disconnect / sendUi / sendConnected
// ---------------------------------------------------------------------------

bool Tnc::connect(uint8_t chIdx, const char* destination) {
  if (chIdx >= CHANNEL_COUNT || destination == nullptr) return false;
  ax25::Address dest{};
  return ax25::parseAddress(destination, dest) && channels_[chIdx].link.connectTo(dest);
}

bool Tnc::disconnect(uint8_t chIdx) {
  if (chIdx >= CHANNEL_COUNT) return false;
  return channels_[chIdx].link.disconnect();
}

bool Tnc::sendUi(const char* destination, const char* text) {
  ax25::Address dest{};
  if (!ax25::parseAddress(destination, dest) || text == nullptr) return false;
  return channels_[0].link.sendUi(dest, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

bool Tnc::sendConnected(uint8_t chIdx, const char* text) {
  if (chIdx >= CHANNEL_COUNT || text == nullptr) return false;
  sendConnectedChunked(chIdx, reinterpret_cast<const uint8_t*>(text), strlen(text));
  return true;
}

// ---------------------------------------------------------------------------
// Link status event handling (all channels)
// ---------------------------------------------------------------------------

void Tnc::checkLinkStatusEvents() {
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    const ax25::LinkState state = channels_[i].link.state();
    if (state == channels_[i].lastState) continue;
    channels_[i].lastState = state;

    if (state == ax25::LinkState::Disconnected || state == ax25::LinkState::Recovery) {
      channels_[i].nodeGreetingSent = false;
      channels_[i].shellMode        = ShellMode::Node;
      channels_[i].nodeLinePos      = 0;
      channels_[i].composeBodyPos   = 0;
    }

    const uint8_t channel = i + 1;
    if (serialMode_ == SerialMode::Console) {
      char peer[12]{};
      ax25::formatAddress(channels_[i].link.peer(), peer, sizeof(peer));
      switch (state) {
        case ax25::LinkState::Connecting:
          Serial.printf("*** ch%u Connecting to %s\n", channel, peer); break;
        case ax25::LinkState::Connected:
          Serial.printf("*** ch%u Connected to %s\n", channel, peer); break;
        case ax25::LinkState::Disconnecting:
          Serial.printf("*** ch%u Disconnecting from %s\n", channel, peer); break;
        case ax25::LinkState::Disconnected:
          Serial.printf("*** ch%u Disconnected from %s\n", channel, peer); break;
        case ax25::LinkState::Recovery:
          Serial.printf("*** ch%u Link failure with %s\n", channel, peer); break;
        default: break;
      }
    }

    const char* dedStatus = nullptr;
    switch (state) {
      case ax25::LinkState::Connecting:    dedStatus = "LINK RESET to station"; break;
      case ax25::LinkState::Connected:     dedStatus = "CONNECTED to";          break;
      case ax25::LinkState::Disconnecting: dedStatus = "DISCONNECTING fm";      break;
      case ax25::LinkState::Disconnected:  dedStatus = "DISCONNECTED fm";       break;
      case ax25::LinkState::Recovery:      dedStatus = "LINK FAILURE with";     break;
      default: break;
    }
    if (dedStatus != nullptr) {
      char peer[12]{};
      ax25::formatAddress(channels_[i].link.peer(), peer, sizeof(peer));
      char text[64]{};
      // No (channel) prefix — hostmode channel is in frame header; terminal adds prefix when displaying
      if (peer[0] != '\0') {
        snprintf(text, sizeof(text), "%s %s", dedStatus, peer);
      } else {
        snprintf(text, sizeof(text), "%s", dedStatus);
      }
      enqueueDedEvent(channel, 3, reinterpret_cast<const uint8_t*>(text), strlen(text));
    }
    if (serialMode_ == SerialMode::Wa8ded && !dedHostMode_ && dedUnattended_ &&
        state == ax25::LinkState::Connected && dedUnattendedText_[0] != '\0') {
      channels_[i].link.sendConnected(reinterpret_cast<const uint8_t*>(dedUnattendedText_),
                                      strlen(dedUnattendedText_));
    }
  }
}

// ---------------------------------------------------------------------------
// Node / BBS shell
// ---------------------------------------------------------------------------

void Tnc::processNodeInput(uint8_t chIdx, const uint8_t* data, size_t len) {
  ChannelState& cs = channels_[chIdx];
  if (!cs.nodeGreetingSent) {
    sendNodeText(chIdx, "*** AXLoRaTNC\rType ? for commands or BBS for mailbox\r");
    cs.nodeGreetingSent = true;
  }
  for (size_t i = 0; i < len; ++i) {
    const char c = static_cast<char>(data[i]);
    if (c == '\r' || c == '\n') {
      cs.nodeLine[cs.nodeLinePos] = '\0';
      switch (cs.shellMode) {
        case ShellMode::Node:       handleNodeLine(chIdx, cs.nodeLine);        break;
        case ShellMode::Bbs:        handleBbsLine(chIdx, cs.nodeLine);         break;
        case ShellMode::BbsCompose: handleBbsComposeLine(chIdx, cs.nodeLine);  break;
      }
      cs.nodeLinePos = 0;
      continue;
    }
    if (cs.nodeLinePos < sizeof(cs.nodeLine) - 1)
      cs.nodeLine[cs.nodeLinePos++] = c;
  }
}

void Tnc::handleNodeLine(uint8_t chIdx, const char* line) {
  // Build uppercase command token
  char cmd[16]{};
  size_t i = 0;
  while (line[i] && line[i] != ' ' && i < sizeof(cmd) - 1) {
    const char c = line[i];
    cmd[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
    ++i;
  }

  if (cmd[0] == '\0' || strcmp(cmd, "?") == 0) {
    sendNodeText(chIdx, "Commands: ?, INFO, NODES, ROUTES, MHEARD, BBS, BYE\r");
  } else if (strcmp(cmd, "INFO") == 0) {
    sendNodeText(chIdx, netrom_.ident);
    sendNodeText(chIdx, "\r");
  } else if (strcmp(cmd, "NODES") == 0 || strcmp(cmd, "ROUTES") == 0) {
    sendNodeText(chIdx, "Known nodes:\r");
    for (const NetromRoute& route : netromRoutes_) {
      if (!route.active) continue;
      char node[12]{}, via[12]{};
      ax25::formatAddress(route.node, node, sizeof(node));
      ax25::formatAddress(route.heardFrom, via, sizeof(via));
      char row[64]{};
      snprintf(row, sizeof(row), "%-6s %-10s via %-10s q=%u\r", route.alias, node, via, route.quality);
      sendNodeText(chIdx, row);
    }
  } else if (strcmp(cmd, "MHEARD") == 0) {
    sendNodeText(chIdx, "callsign   dest       first    last     fr  rssi  snr\r");
    for (const MheardEntry& entry : mheard_) {
      if (!entry.active) continue;
      char src[12]{}, dst[12]{}, first[10]{}, last[10]{};
      ax25::formatAddress(entry.source,      src, sizeof(src));
      ax25::formatAddress(entry.destination, dst, sizeof(dst));
      formatUptime(entry.firstHeardMs, first, sizeof(first));
      formatUptime(entry.lastHeardMs,  last,  sizeof(last));
      char row[80]{};
      snprintf(row, sizeof(row), "%-10s %-10s %s %s %3lu %5.1f %5.1f\r",
               src, dst, first, last,
               static_cast<unsigned long>(entry.frames),
               static_cast<double>(entry.lastRssi),
               static_cast<double>(entry.lastSnr));
      sendNodeText(chIdx, row);
    }
  } else if (strcmp(cmd, "BBS") == 0) {
    channels_[chIdx].shellMode = ShellMode::Bbs;
    sendNodeText(chIdx, "*** BBS - type ? for help\r");
  } else if (strcmp(cmd, "BYE") == 0 || strcmp(cmd, "B") == 0) {
    sendNodeText(chIdx, "73\r");
    disconnect(chIdx);
  } else {
    sendNodeText(chIdx, "Unknown command. Type ?\r");
  }
}

void Tnc::handleBbsLine(uint8_t chIdx, const char* line) {
  // Build uppercase command token
  char cmd[16]{};
  const char* args = line;
  size_t i = 0;
  while (args[i] && args[i] != ' ' && i < sizeof(cmd) - 1) {
    const char c = args[i];
    cmd[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
    ++i;
  }
  args = (args[i] == ' ') ? args + i + 1 : args + i;  // rest of line after command

  char peerCallsign[12]{};
  ax25::formatAddress(channels_[chIdx].link.peer(), peerCallsign, sizeof(peerCallsign));
  // strip SSID from peer for BBS from-field
  for (size_t j = 0; j < sizeof(peerCallsign); ++j) {
    if (peerCallsign[j] == '-') { peerCallsign[j] = '\0'; break; }
  }

  if (cmd[0] == '\0' || strcmp(cmd, "?") == 0 || strcmp(cmd, "H") == 0) {
    sendNodeText(chIdx, "BBS commands:\r L  - list all\r LT - list mine\r R n - read\r");
    sendNodeText(chIdx, " S CALL - send\r K n - kill\r X - exit BBS\r B - bye\r");

  } else if (strcmp(cmd, "L") == 0) {
    const char* filter = (args[0] == 'T' || args[0] == 't') ? peerCallsign : nullptr;
    char buf[512]{};
    mailbox_.list(buf, sizeof(buf), filter);
    sendNodeText(chIdx, buf);

  } else if (strcmp(cmd, "R") == 0) {
    const long idx = atol(args);
    const Mailbox::Message* m = mailbox_.get(static_cast<uint8_t>(idx - 1));
    if (m == nullptr) {
      sendNodeText(chIdx, "Message not found\r");
    } else {
      char hdr[64]{};
      snprintf(hdr, sizeof(hdr), "From: %-10s To: %s\r", m->from, m->to);
      sendNodeText(chIdx, hdr);
      sendNodeText(chIdx, m->body);
      sendNodeText(chIdx, "\r");
      mailbox_.markRead(static_cast<uint8_t>(idx - 1));
    }

  } else if (strcmp(cmd, "S") == 0) {
    if (args[0] == '\0') {
      sendNodeText(chIdx, "usage: S CALLSIGN\r");
    } else {
      ChannelState& cs = channels_[chIdx];
      strncpy(cs.composeTo, args, sizeof(cs.composeTo) - 1);
      cs.composeTo[sizeof(cs.composeTo) - 1] = '\0';
      cs.composeBodyPos = 0;
      cs.composeBody[0] = '\0';
      cs.shellMode = ShellMode::BbsCompose;
      sendNodeText(chIdx, "Enter message, empty line to send:\r");
    }

  } else if (strcmp(cmd, "K") == 0) {
    const long idx = atol(args);
    if (mailbox_.kill(static_cast<uint8_t>(idx - 1), peerCallsign)) {
      sendNodeText(chIdx, "Deleted\r");
    } else {
      sendNodeText(chIdx, "Cannot delete\r");
    }

  } else if (strcmp(cmd, "X") == 0 || strcmp(cmd, "EXIT") == 0) {
    channels_[chIdx].shellMode = ShellMode::Node;
    sendNodeText(chIdx, "Back to node shell. Type ?\r");

  } else if (strcmp(cmd, "B") == 0 || strcmp(cmd, "BYE") == 0) {
    sendNodeText(chIdx, "73\r");
    disconnect(chIdx);

  } else {
    sendNodeText(chIdx, "Unknown BBS command. Type ?\r");
  }
}

void Tnc::handleBbsComposeLine(uint8_t chIdx, const char* line) {
  ChannelState& cs = channels_[chIdx];
  if (line[0] == '\0') {
    // Empty line = end of message
    char peerCallsign[12]{};
    ax25::formatAddress(channels_[chIdx].link.peer(), peerCallsign, sizeof(peerCallsign));
    for (size_t j = 0; j < sizeof(peerCallsign); ++j) {
      if (peerCallsign[j] == '-') { peerCallsign[j] = '\0'; break; }
    }
    if (mailbox_.post(peerCallsign, cs.composeTo, cs.composeBody)) {
      sendNodeText(chIdx, "Message saved\r");
    } else {
      sendNodeText(chIdx, "Mailbox full\r");
    }
    cs.composeBodyPos = 0;
    cs.composeBody[0] = '\0';
    cs.shellMode = ShellMode::Bbs;
    return;
  }
  // Append line to body
  const size_t lineLen = strlen(line);
  const size_t remaining = Mailbox::MAX_BODY - cs.composeBodyPos;
  if (remaining < 2) {
    sendNodeText(chIdx, "Message too long, saved\r");
    // Force save
    char peerCallsign[12]{};
    ax25::formatAddress(channels_[chIdx].link.peer(), peerCallsign, sizeof(peerCallsign));
    for (size_t j = 0; j < sizeof(peerCallsign); ++j) {
      if (peerCallsign[j] == '-') { peerCallsign[j] = '\0'; break; }
    }
    mailbox_.post(peerCallsign, cs.composeTo, cs.composeBody);
    cs.composeBodyPos = 0;
    cs.composeBody[0] = '\0';
    cs.shellMode = ShellMode::Bbs;
    return;
  }
  const size_t copy = lineLen < remaining - 1 ? lineLen : remaining - 1;
  memcpy(cs.composeBody + cs.composeBodyPos, line, copy);
  cs.composeBodyPos += copy;
  if (cs.composeBodyPos < Mailbox::MAX_BODY) {
    cs.composeBody[cs.composeBodyPos++] = '\n';
  }
  cs.composeBody[cs.composeBodyPos] = '\0';
}

void Tnc::sendNodeText(uint8_t chIdx, const char* text) {
  if (text != nullptr) {
    sendConnectedChunked(chIdx, reinterpret_cast<const uint8_t*>(text), strlen(text));
  }
}

void Tnc::sendConnectedChunked(uint8_t chIdx, const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) return;
  const size_t paclen = (dedIPollFrameLength_ > 0)
      ? static_cast<size_t>(dedIPollFrameLength_)
      : ax25::MAX_INFO_LEN;
  size_t offset = 0;
  while (offset < len) {
    const size_t chunk = (len - offset) > paclen ? paclen : (len - offset);
    channels_[chIdx].link.sendConnected(data + offset, chunk);
    offset += chunk;
  }
}

// ---------------------------------------------------------------------------
// WA8DED hostmode
// ---------------------------------------------------------------------------

void Tnc::serviceWa8ded(uint8_t byte) {
  if (!dedHostMode_) {
    serviceWa8dedTerminal(byte);
    return;
  }
  serviceWa8dedHost(byte);
}

void Tnc::serviceWa8dedHost(uint8_t byte) {
  // XON/XOFF only safe to intercept at frame boundary (channel bytes 0x11/0x13 are out of range)
  if (dedXonXoff_ && dedHeaderPos_ == 0) {
    if (byte == 0x13) { dedOutputPaused_ = true;  return; }
    if (byte == 0x11) { dedOutputPaused_ = false; return; }
  }
  if (dedHeaderPos_ < sizeof(dedHeader_)) {
    dedHeader_[dedHeaderPos_++] = byte;
    if (dedHeaderPos_ == sizeof(dedHeader_)) {
      dedDataLen_ = static_cast<size_t>(dedHeader_[2]) + 1;
      dedDataPos_ = 0;
    }
    return;
  }
  if (dedDataPos_ < sizeof(dedData_)) dedData_[dedDataPos_++] = byte;
  if (dedDataPos_ >= dedDataLen_) {
    handleDedHostFrame(dedHeader_[0], dedHeader_[1], dedData_, dedDataLen_);
    dedHeaderPos_ = 0; dedDataPos_ = 0; dedDataLen_ = 0;
  }
}

void Tnc::serviceWa8dedTerminal(uint8_t byte) {
  if (dedXonXoff_ && byte == 0x13) { dedOutputPaused_ = true; return; }
  if (dedXonXoff_ && byte == 0x11) { dedOutputPaused_ = false; return; }

  if (byte == 0x1B && linePos_ == 0) {
    dedTerminalCommand_ = true;
    if (dedEcho_) Serial.print("* ");
    return;
  }
  if (byte == '\n') return;
  if (byte == '\r') {
    line_[linePos_] = '\0';
    if (dedEcho_) Serial.print(dedAutoLf_ ? "\r\n" : "\r");
    handleDedTerminalLine(line_, dedTerminalCommand_);
    linePos_ = 0;
    dedTerminalCommand_ = false;
    return;
  }
  if (byte == 0x08 || byte == 0x7F) {
    if (linePos_ > 0) {
      --linePos_;
      if (dedEcho_) Serial.print("\b \b");
    }
    return;
  }
  if (byte == 0x15 || byte == 0x18) {
    while (linePos_ > 0) {
      --linePos_;
      if (dedEcho_) Serial.print("\b \b");
    }
    return;
  }
  if (linePos_ >= sizeof(line_) - 1) {
    Serial.write(static_cast<uint8_t>(0x07));
    return;
  }
  line_[linePos_++] = static_cast<char>(byte);
  if (dedEcho_) Serial.write(byte);
}

void Tnc::handleDedTerminalLine(const char* line, bool command) {
  if (line == nullptr) return;
  if (!command) {
    if (line[0] == '\0') return;
    const size_t len = strlen(line);
    if (dedSelectedChannel_ == 0) {
      if (!channels_[0].link.sendUi(dedUnprotoDestination_,
                                    reinterpret_cast<const uint8_t*>(line), len)) {
        Serial.print("TNC BUSY - LINE IGNORED\r\n");
      }
      return;
    }
    const uint8_t chIdx = dedSelectedChannel_ - 1;
    bool ok = false;
    if (channels_[chIdx].link.state() == ax25::LinkState::Connected) {
      ok = channels_[chIdx].link.sendConnected(reinterpret_cast<const uint8_t*>(line), len);
    } else {
      ok = channels_[chIdx].link.sendUi(dedUnprotoDestination_,
                                        reinterpret_cast<const uint8_t*>(line), len);
    }
    if (!ok) Serial.print("TNC BUSY - LINE IGNORED\r\n");
    return;
  }

  char cmd[256]{};
  strncpy(cmd, line, sizeof(cmd) - 1);
  for (size_t i = 0; cmd[i] != '\0'; ++i) {
    if (cmd[i] >= 'a' && cmd[i] <= 'z') cmd[i] = static_cast<char>(cmd[i] - 32);
  }
  const char c = cmd[0];
  const char* arg = skipSpaces(cmd + 1);
  auto printByteParam = [&](char name, unsigned value) {
    Serial.printf("%c %u\r\n", name, value);
  };
  auto setByteParam = [&](char name, uint8_t& value, uint8_t minValue, uint8_t maxValue) {
    if (*arg == '\0') { printByteParam(name, value); return; }
    const long parsed = atol(arg);
    if (parsed < minValue || parsed > maxValue) {
      Serial.print("? INVALID PARAMETER\r\n");
      return;
    }
    value = static_cast<uint8_t>(parsed);
  };
  auto printBoolParam = [&](char name, bool value) {
    Serial.printf("%c %u\r\n", name, value ? 1U : 0U);
  };
  auto setBoolParam = [&](char name, bool& value) {
    if (*arg == '\0') { printBoolParam(name, value); return; }
    value = (*arg != '0');
  };

  if (c == '\0') return;
  if (c == 'A') {
    if (*arg == '\0') { Serial.printf("A %u\r\n", dedAutoLf_ ? 1U : 0U); return; }
    dedAutoLf_ = (*arg != '0');
    saveDedConfig();
    return;
  }
  if (c == 'B') {
    if (*arg == '\0') Serial.printf("B %u (%u)\r\n", dedDamaTimeout_, dedDamaTimeout_);
    else { dedDamaTimeout_ = static_cast<uint16_t>(atoi(arg)); saveDedConfig(); }
    return;
  }
  if (c == 'E') {
    if (*arg == '\0') { Serial.printf("E %u\r\n", dedEcho_ ? 1U : 0U); return; }
    dedEcho_ = (*arg != '0');
    saveDedConfig();
    return;
  }
  if (c == 'F') {
    if (*arg == '\0') { Serial.printf("F %u\r\n", dedFrack_); return; }
    dedFrack_ = static_cast<uint16_t>(atoi(arg));
    applyDedLinkConfig();
    saveDedConfig();
    return;
  }
  if (c == 'G') return;
  if (c == 'H') {
    if (*arg == '\0') { printMheard(); return; }
    const long mode = atol(arg);
    if (mode == 2) clearMheard();
    else if (mode >= 0 && mode <= 127) { dedHeardMode_ = static_cast<uint8_t>(mode); saveDedConfig(); }
    else Serial.print("? INVALID PARAMETER\r\n");
    return;
  }
  if (c == 'K') {
    if (*arg == '\0') {
      Serial.printf("K %u\r\n", dedTimestamp_ ? 1U : 0U);
      return;
    }
    dedTimestamp_ = (*arg != '0');
    saveDedConfig();
    return;
  }

  if (strncmp(cmd, "JHOST", 5) == 0) {
    const char* value = skipSpaces(cmd + 5);
    if (*value == '\0') {
      Serial.printf("JHOST %u\r\n", dedHostMode_ ? 1U : 0U);
      return;
    }
    dedHostMode_ = (*value != '0');
    dedHeaderPos_ = 0; dedDataPos_ = 0; dedDataLen_ = 0;
    return;
  }

  if (strncmp(cmd, "CONSOLE", 7) == 0) {
    Serial.print("mode=console\r\n");
    Serial.flush();
    setSerialMode(SerialMode::Console);
    saveSerialMode(SerialMode::Console);
    return;
  }

  if (c == 'C') {
    if (*arg == '\0') {
      char dest[12]{};
      if (dedSelectedChannel_ == 0) ax25::formatAddress(dedUnprotoDestination_, dest, sizeof(dest));
      else ax25::formatAddress(channels_[dedSelectedChannel_ - 1].link.peer(), dest, sizeof(dest));
      Serial.printf("C %s\r\n", dest);
      return;
    }
    char first[16]{};
    size_t i = 0;
    while (arg[i] != '\0' && arg[i] != ' ' && i < sizeof(first) - 1) {
      first[i] = arg[i];
      ++i;
    }
    ax25::Address dest{};
    if (!ax25::parseAddress(first, dest)) {
      Serial.print("? INVALID CALLSIGN\r\n");
      return;
    }
    if (dedSelectedChannel_ == 0) {
      dedUnprotoDestination_ = dest;
    } else if (!connect(dedSelectedChannel_ - 1, first)) {
      Serial.print("? CONNECT FAILED\r\n");
    }
    return;
  }

  if (c == 'D') {
    if (dedSelectedChannel_ > 0 && !disconnect(dedSelectedChannel_ - 1)) {
      Serial.print("? DISCONNECT FAILED\r\n");
    }
    return;
  }

  if (c == 'I') {
    if (*arg == '\0') {
      char local[12]{};
      ax25::formatAddress(local_, local, sizeof(local));
      Serial.printf("I %s\r\n", local);
      return;
    }
    ax25::Address newAddr{};
    if (!ax25::parseAddress(arg, newAddr)) {
      Serial.print("? INVALID CALLSIGN\r\n");
      return;
    }
    local_ = newAddr;
    ax25::L2Config cfg{};
    cfg.local = local_;
    for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
      channels_[i].link.begin(cfg, radioTxCallback, dataCallback, &channelCtx_[i]);
    }
    ax25::formatAddress(local_, savedCallsign_, sizeof(savedCallsign_));
    Preferences prefs;
    if (prefs.begin("axloratnc", false)) {
      prefs.putString("callsign", savedCallsign_);
      prefs.end();
    }
    return;
  }

  if (c == 'L') {
    if (*arg == '\0') {
      printDedTerminalStatus(-1);
    } else {
      printDedTerminalStatus(atoi(arg));
    }
    return;
  }

  if (c == 'M') {
    if (*arg == '\0') {
      Serial.printf("M %s\r\n", monitorEnabled_ ? dedMonitorMode_ : "N");
      return;
    }
    strncpy(dedMonitorMode_, arg, sizeof(dedMonitorMode_) - 1);
    dedMonitorMode_[sizeof(dedMonitorMode_) - 1] = '\0';
    monitorEnabled_ = (strchr(dedMonitorMode_, 'N') == nullptr);
    saveDedConfig();
    return;
  }

  if (c == 'N') {
    setByteParam('N', dedRetryLimit_, 0, 127);
    applyDedLinkConfig();
    if (*arg != '\0') saveDedConfig();
    return;
  }

  if (c == 'O') {
    setByteParam('O', dedMaxFrame_, 1, 7);
    applyDedLinkConfig();
    if (*arg != '\0') saveDedConfig();
    return;
  }

  if (c == 'P') {
    setByteParam('P', kissParams_.persistence, 0, 255);
    if (*arg != '\0') saveDedConfig();
    return;
  }

  if (strncmp(cmd, "QRES", 4) == 0) {
    dedAutoLf_ = true;
    dedEcho_ = true;
    dedTimestamp_ = false;
    dedDamaTimeout_ = 120;
    dedFrack_ = 250;
    dedHeardMode_ = 0;
    dedRetryLimit_ = 10;
    dedMaxFrame_ = 2;
    kissParams_.persistence = 32;
    kissParams_.txDelay = 25;
    kissParams_.slotTime = 10;
    kissParams_.fullDuplex = 0;
    dedTxEnabled_ = true;
    dedMaxIncoming_ = 4;
    dedFlow_ = true;
    dedXonXoff_ = true;
    dedCtextMode_ = 0;
    dedUnattended_ = false;
    dedT2_ = 150;
    dedT3_ = 18000;
    dedIPollFrameLength_ = 60;
    strncpy(dedMonitorMode_, "IU", sizeof(dedMonitorMode_) - 1);
    monitorEnabled_ = true;
    applyDedLinkConfig();
    saveDedConfig();
    return;
  }

  if (c == 'R') {
    if (*arg == '\0') { printBoolParam('R', digi_.enabled); return; }
    digi_.enabled = (*arg != '0');
    saveSettings();
    return;
  }

  if (c == 'S') {
    if (*arg == '\0') {
      Serial.printf("S %u\r\n", static_cast<unsigned>(dedSelectedChannel_));
      return;
    }
    const long selected = atol(arg);
    if (selected < 0 || selected > CHANNEL_COUNT) {
      Serial.print("? INVALID CHANNEL\r\n");
      return;
    }
    dedSelectedChannel_ = static_cast<uint8_t>(selected);
    serviceDedTerminalOutput();
    return;
  }

  if (c == 'T') {
    setByteParam('T', kissParams_.txDelay, 0, 127);
    return;
  }

  if (c == 'U') {
    if (*arg == '\0') {
      Serial.printf("U %u %s\r\n", static_cast<unsigned>(dedCtextMode_), dedUnattendedText_);
      return;
    }
    const long mode = atol(arg);
    if (mode < 0 || mode > 2) {
      Serial.print("? INVALID PARAMETER\r\n");
      return;
    }
    dedCtextMode_ = static_cast<uint8_t>(mode);
    dedUnattended_ = dedCtextMode_ != 0;
    const char* text = skipSpaces(arg + 1);
    if (*text != '\0') {
      strncpy(dedUnattendedText_, text, sizeof(dedUnattendedText_) - 1);
      dedUnattendedText_[sizeof(dedUnattendedText_) - 1] = '\0';
    }
    saveDedConfig();
    return;
  }

  if (c == 'V') { Serial.print("AXLoRaTNC WA8DED\r\n"); return; }

  if (c == 'W') {
    setByteParam('W', kissParams_.slotTime, 0, 127);
    if (*arg != '\0') saveDedConfig();
    return;
  }

  if (c == 'X') {
    if (*arg == '\0') { Serial.printf("X %u\r\n", dedTxEnabled_ ? 1U : 0U); return; }
    dedTxEnabled_ = (*arg != '0');
    saveDedConfig();
    return;
  }

  if (c == 'Y') {
    if (*arg == '\0') {
      uint8_t used = 0;
      for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
        if (channels_[i].link.state() != ax25::LinkState::Disconnected) ++used;
      }
      Serial.printf("Y %u (%u)\r\n", static_cast<unsigned>(dedMaxIncoming_),
                    static_cast<unsigned>(used));
      return;
    }
    const long maxConn = atol(arg);
    if (maxConn < 0 || maxConn > CHANNEL_COUNT) {
      Serial.print("? INVALID PARAMETER\r\n");
      return;
    }
    dedMaxIncoming_ = static_cast<uint8_t>(maxConn);
    saveDedConfig();
    return;
  }

  if (c == 'Z') {
    if (*arg == '\0') {
      Serial.printf("Z %u\r\n", static_cast<unsigned>((dedFlow_ ? 1 : 0) + (dedXonXoff_ ? 2 : 0)));
      return;
    }
    const long mode = atol(arg);
    if (mode < 0 || mode > 3) {
      Serial.print("? INVALID PARAMETER\r\n");
      return;
    }
    dedFlow_ = (mode & 1) != 0;
    dedXonXoff_ = (mode & 2) != 0;
    saveDedConfig();
    return;
  }

  if (c == '@') {
    handleDedAtCommand(cmd);
    return;
  }

  // --- Extended classic TNC commands ---

  // MYCALL [callsign]  — alias for I
  if (strncmp(cmd, "MYCALL", 6) == 0) {
    const char* cs = skipSpaces(cmd + 6);
    if (*cs == '\0') {
      char local[12]{};
      ax25::formatAddress(local_, local, sizeof(local));
      Serial.printf("MYCALL %s\r\n", local);
      return;
    }
    ax25::Address newAddr{};
    if (!ax25::parseAddress(cs, newAddr)) { Serial.print("? INVALID CALLSIGN\r\n"); return; }
    local_ = newAddr;
    ax25::L2Config cfg{};
    cfg.local = local_;
    for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
      channels_[i].link.begin(cfg, radioTxCallback, dataCallback, &channelCtx_[i]);
    }
    ax25::formatAddress(local_, savedCallsign_, sizeof(savedCallsign_));
    Preferences prefs;
    if (prefs.begin("axloratnc", false)) { prefs.putString("callsign", savedCallsign_); prefs.end(); }
    return;
  }

  // UNPROTO [dest [VIA path]]  — set unproto destination + optional via path for UI frames
  if (strncmp(cmd, "UNPROTO", 7) == 0) {
    const char* rest = skipSpaces(cmd + 7);
    if (*rest == '\0') {
      char dest[12]{};
      ax25::formatAddress(dedUnprotoDestination_, dest, sizeof(dest));
      Serial.printf("UNPROTO %s\r\n", dest);
      return;
    }
    // Parse destination (first token before space or "VIA")
    char destStr[16]{};
    size_t di = 0;
    while (rest[di] != '\0' && rest[di] != ' ' && di < sizeof(destStr) - 1) { destStr[di] = rest[di]; ++di; }
    ax25::Address newDest{};
    if (!ax25::parseAddress(destStr, newDest)) { Serial.print("? INVALID CALLSIGN\r\n"); return; }
    dedUnprotoDestination_ = newDest;
    saveDedConfig();
    return;
  }

  // BTEXT [text]  — get/set beacon text
  if (strncmp(cmd, "BTEXT", 5) == 0) {
    const char* rest = skipSpaces(cmd + 5);
    if (*rest == '\0') {
      Serial.printf("BTEXT %s\r\n", beacon_.text);
      return;
    }
    strncpy(beacon_.text, rest, sizeof(beacon_.text) - 1);
    beacon_.text[sizeof(beacon_.text) - 1] = '\0';
    saveSettings();
    return;
  }

  Serial.print("? INVALID COMMAND\r\n");
}

void Tnc::serviceDedTerminalOutput() {
  if (dedOutputPaused_) return;
  const size_t count = dedEvents_.size();
  for (size_t i = 0; i < count && !dedOutputPaused_; ++i) {
    DedEvent event{};
    if (!dedEvents_.pop(event)) return;
    if (event.channel != dedSelectedChannel_) {
      dedEvents_.push(event);
      continue;
    }
    if (event.code == 3) {
      event.data[event.len < sizeof(event.data) ? event.len : sizeof(event.data) - 1] = '\0';
      Serial.printf("*** (%u) %s\r\n",
                    static_cast<unsigned>(event.channel),
                    reinterpret_cast<const char*>(event.data));
    } else if (event.code == 4 || event.code == 5) {
      event.data[event.len < sizeof(event.data) ? event.len : sizeof(event.data) - 1] = '\0';
      Serial.print(reinterpret_cast<const char*>(event.data));
      Serial.print("\r\n");
    } else if (event.code == 6 || event.code == 7) {
      Serial.write(event.data, event.len);
      Serial.print("\r\n");
    }
  }
}

void Tnc::handleDedAtCommand(const char* cmd) {
  if (cmd == nullptr || cmd[0] != '@') return;
  const char* name = cmd + 1;
  const char* arg = name;
  while (*arg != '\0' && *arg != ' ') ++arg;
  char token[5]{};
  const size_t tokenLen = static_cast<size_t>(arg - name);
  memcpy(token, name, tokenLen < sizeof(token) - 1 ? tokenLen : sizeof(token) - 1);
  arg = skipSpaces(arg);

  auto setU8 = [&](const char* label, uint8_t& value, uint8_t minValue, uint8_t maxValue) {
    if (*arg == '\0') {
      Serial.printf("@%s %u\r\n", label, static_cast<unsigned>(value));
      return;
    }
    const long parsed = atol(arg);
    if (parsed < minValue || parsed > maxValue) {
      Serial.print("? INVALID PARAMETER\r\n");
      return;
    }
    value = static_cast<uint8_t>(parsed);
  };
  auto setU16 = [&](const char* label, uint16_t& value, uint16_t minValue, uint16_t maxValue) {
    if (*arg == '\0') {
      Serial.printf("@%s %u\r\n", label, static_cast<unsigned>(value));
      return;
    }
    const long parsed = atol(arg);
    if (parsed < minValue || parsed > maxValue) {
      Serial.print("? INVALID PARAMETER\r\n");
      return;
    }
    value = static_cast<uint16_t>(parsed);
  };

  if (strcmp(token, "A1") == 0) { setU8("A1", dedSrttA1_, 0, 255); return; }
  if (strcmp(token, "A2") == 0) { setU8("A2", dedSrttA2_, 0, 255); return; }
  if (strcmp(token, "A3") == 0) { setU8("A3", dedSrttA3_, 1, 255); return; }
  if (strcmp(token, "B") == 0) {
    // Report free TX-queue capacity in bytes for the selected channel.
    // FBB/TFPCX use this to throttle how much data they inject at once.
    const uint8_t chIdx = (dedSelectedChannel_ > 0 && dedSelectedChannel_ <= CHANNEL_COUNT)
                          ? dedSelectedChannel_ - 1 : 0;
    const size_t freeSlots = channels_[chIdx].link.connectedQueueFree();
    const uint32_t freeBytes = freeSlots * static_cast<uint32_t>(
        dedIPollFrameLength_ > 0 ? dedIPollFrameLength_ : ax25::MAX_INFO_LEN);
    // Cap at 255 — classic WA8DED TNCs return a single byte value.
    const unsigned reported = freeBytes > 255 ? 255 : static_cast<unsigned>(freeBytes);
    Serial.printf("@B %u\r\n", reported);
    return;
  }
  if (strcmp(token, "D") == 0) {
    if (*arg == '\0') {
      Serial.printf("@D %u\r\n", kissParams_.fullDuplex ? 1U : 0U);
      return;
    }
    kissParams_.fullDuplex = (*arg != '0') ? 1 : 0;
    saveDedConfig();
    return;
  }
  if (strcmp(token, "I") == 0) {
    setU8("I", dedIPollFrameLength_, 1, 255);
    if (*arg != '\0') saveDedConfig();
    return;
  }
  if (strcmp(token, "K") == 0) {
    Serial.print("@K\r\n");
    Serial.flush();
    setSerialMode(SerialMode::Kiss);
    saveSerialMode(SerialMode::Kiss);
    return;
  }
  if (strcmp(token, "M") == 0) {
    if (*arg == '\0') {
      Serial.printf("@M %u\r\n", dedEightBitTerminal_ ? 1U : 0U);
      return;
    }
    dedEightBitTerminal_ = (*arg != '0');
    saveDedConfig();
    return;
  }
  if (strcmp(token, "T2") == 0) {
    setU16("T2", dedT2_, 0, 65535);
    applyDedLinkConfig();
    if (*arg != '\0') saveDedConfig();
    return;
  }
  if (strcmp(token, "T3") == 0) {
    setU16("T3", dedT3_, 0, 65535);
    applyDedLinkConfig();
    if (*arg != '\0') saveDedConfig();
    return;
  }
  if (strcmp(token, "V") == 0) {
    if (*arg == '\0') {
      Serial.printf("@V %u\r\n", dedValidateCallsign_ ? 1U : 0U);
      return;
    }
    dedValidateCallsign_ = (*arg != '0');
    saveDedConfig();
    return;
  }
  Serial.print("? INVALID COMMAND\r\n");
}

void Tnc::applyDedLinkConfig() {
  const uint32_t t1Ms = static_cast<uint32_t>(dedFrack_) * 10UL;
  const uint32_t t2Ms = static_cast<uint32_t>(dedT2_) * 10UL;
  const uint32_t t3Ms = static_cast<uint32_t>(dedT3_) * 10UL;
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    channels_[i].link.setTimers(t1Ms, t2Ms, t3Ms);
    channels_[i].link.setRetryLimit(dedRetryLimit_);
    channels_[i].link.setMaxFrame(dedMaxFrame_);
  }
}

void Tnc::printDedTerminalStatus(int channel) const {
  const int first = channel < 0 ? 0 : channel;
  const int last = channel < 0 ? CHANNEL_COUNT : channel;
  if (first < 0 || last > CHANNEL_COUNT) {
    Serial.print("? INVALID CHANNEL\r\n");
    return;
  }
  for (int ch = first; ch <= last; ++ch) {
    const char marker = ch == dedSelectedChannel_ ? '+' : ' ';
    if (ch == 0) {
      char dest[12]{};
      ax25::formatAddress(dedUnprotoDestination_, dest, sizeof(dest));
      Serial.printf("%c0 %s 0 0 0 0\r\n", marker, dest);
      continue;
    }
    const uint8_t chIdx = static_cast<uint8_t>(ch - 1);
    char peer[12]{};
    ax25::formatAddress(channels_[chIdx].link.peer(), peer, sizeof(peer));
    const bool connected = channels_[chIdx].link.state() == ax25::LinkState::Connected;
    Serial.printf("%c%d %s %u %u %u %u\r\n",
                  marker, ch, connected ? peer : "-",
                  connected ? 1U : 0U,
                  static_cast<unsigned>(channels_[chIdx].link.connectedQueueSize()),
                  0U, 0U);
  }
}

void Tnc::handleDedHostFrame(uint8_t channel, uint8_t infoCmd,
                              const uint8_t* data, size_t len) {
  if (infoCmd == 0) {
    // Data frame
    if (channel == 0) {
      ax25::Address dest{};
      ax25::parseAddress("CQ", dest);
      if (channels_[0].link.sendUi(dest, data, len)) sendDedShort(channel, 0);
      else sendDedText(channel, 2, "TNC BUSY - LINE IGNORED");
      return;
    }
    if (channel >= 1 && channel <= CHANNEL_COUNT) {
      const uint8_t chIdx = channel - 1;
      if (channels_[chIdx].link.state() == ax25::LinkState::Connected) {
        sendConnectedChunked(chIdx, data, len);
        sendDedShort(channel, 0);
      } else {
        sendDedText(channel, 2, "TNC BUSY - LINE IGNORED");
      }
      return;
    }
    sendDedText(channel, 2, "INVALID CHANNEL");
    return;
  }
  if (infoCmd == 1) {
    handleDedCommand(channel, reinterpret_cast<const char*>(data), len);
    return;
  }
  sendDedText(channel, 2, "INVALID COMMAND");
}

void Tnc::handleDedCommand(uint8_t channel, const char* command, size_t len) {
  char cmd[80]{};
  const size_t copyLen = len < sizeof(cmd) ? len : sizeof(cmd) - 1;
  memcpy(cmd, command, copyLen);
  for (size_t i = 0; i < copyLen; ++i) {
    if (cmd[i] >= 'a' && cmd[i] <= 'z') cmd[i] = static_cast<char>(cmd[i] - 32);
  }

  if (cmd[0] == 'G') {
    // G / G0 / G1 polling; honour XON/XOFF pause for monitor/data events
    const uint8_t wanted = cmd[1] == '0' ? 7 : (cmd[1] == '1' ? 3 : 0);
    // While paused only deliver high-priority link-status events (code 3), not data/monitor
    const bool pausedForData = dedOutputPaused_ && wanted != 3;
    DedEvent event{};
    if (!pausedForData && popDedEvent(channel, wanted, event)) {
      if (event.code == 6 || event.code == 7) {
        sendDedCounted(event.channel, event.code, event.data, event.len);
      } else if (event.code >= 1 && event.code <= 5) {
        event.data[event.len < sizeof(event.data) ? event.len : sizeof(event.data) - 1] = '\0';
        sendDedText(event.channel, event.code, reinterpret_cast<const char*>(event.data));
      } else {
        sendDedShort(channel, event.code);
      }
    } else {
      sendDedShort(channel, 0);
    }
    return;
  }

  if (strncmp(cmd, "JHOST", 5) == 0) {
    if (cmd[5] == '0') {
      dedHostMode_ = false;
      dedHeaderPos_ = 0; dedDataPos_ = 0; dedDataLen_ = 0;
      sendDedShort(channel, 0);
      return;
    }
    if (cmd[5] == '1' || cmd[5] == '\0') {
      dedHostMode_ = true;
      saveSerialMode(SerialMode::Wa8ded);
      sendDedShort(channel, 0);
      return;
    }
    sendDedText(channel, 2, "INVALID JHOST");
    return;
  }

  if (cmd[0] == 'C') {
    const char* dest = skipSpaces(cmd + 1);
    if (channel >= 1 && channel <= CHANNEL_COUNT && connect(channel - 1, dest)) {
      sendDedShort(channel, 0);
    } else {
      sendDedText(channel, 2, "CONNECT FAILED");
    }
    return;
  }

  if (cmd[0] == 'D') {
    if (channel >= 1 && channel <= CHANNEL_COUNT && disconnect(channel - 1)) {
      sendDedShort(channel, 0);
    } else {
      sendDedText(channel, 2, "DISCONNECT FAILED");
    }
    return;
  }

  if (cmd[0] == 'I') {
    const char* arg = skipSpaces(cmd + 1);
    if (*arg == '\0') {
      char callsign[12]{};
      ax25::formatAddress(local_, callsign, sizeof(callsign));
      sendDedText(channel, 1, callsign);
      return;
    }
    ax25::Address newAddr{};
    if (!ax25::parseAddress(arg, newAddr)) {
      sendDedText(channel, 2, "INVALID CALLSIGN");
      return;
    }
    local_ = newAddr;
    ax25::L2Config cfg{};
    cfg.local = local_;
    for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
      channels_[i].link.begin(cfg, radioTxCallback, dataCallback, &channelCtx_[i]);
    }
    ax25::formatAddress(local_, savedCallsign_, sizeof(savedCallsign_));
    Preferences prefs;
    if (prefs.begin("axloratnc", false)) {
      prefs.putString("callsign", savedCallsign_);
      prefs.end();
    }
    sendDedShort(channel, 0);
    return;
  }

  if (cmd[0] == 'L') {
    const uint8_t chIdx = (channel >= 1 && channel <= CHANNEL_COUNT) ? channel - 1 : 0;
    const bool connected = channels_[chIdx].link.state() == ax25::LinkState::Connected;
    char peer[12]{};
    ax25::formatAddress(channels_[chIdx].link.peer(), peer, sizeof(peer));
    char status[64]{};
    // Format: connected(0/1) queueSize retryCount outstanding peer
    snprintf(status, sizeof(status), "%u %u %u %u %s",
             connected ? 1U : 0U,
             static_cast<unsigned>(channels_[chIdx].link.connectedQueueSize()),
             static_cast<unsigned>(channels_[chIdx].link.retryCount()),
             static_cast<unsigned>(channels_[chIdx].link.outstandingFrameCount()),
             connected ? peer : "-");
    sendDedText(channel, 1, status);
    return;
  }

  if (cmd[0] == 'M') {
    const char* arg = skipSpaces(cmd + 1);
    if (*arg == '\0') {
      char mtext[24]{};
      snprintf(mtext, sizeof(mtext), "M %s", monitorEnabled_ ? dedMonitorMode_ : "N");
      sendDedText(channel, 1, mtext);
      return;
    }
    strncpy(dedMonitorMode_, arg, sizeof(dedMonitorMode_) - 1);
    dedMonitorMode_[sizeof(dedMonitorMode_) - 1] = '\0';
    monitorEnabled_ = (*arg != '0' && *arg != 'N');
    saveDedConfig();
    sendDedShort(channel, 0);
    return;
  }

  if (cmd[0] == 'V') {
    sendDedText(channel, 1, "AXLoRaTNC WA8DED");
    return;
  }

  if (cmd[0] == 'A' || cmd[0] == 'B' || cmd[0] == 'E' || cmd[0] == 'F' ||
      cmd[0] == 'K' || cmd[0] == 'N' || cmd[0] == 'O' ||
      cmd[0] == 'P' || cmd[0] == 'R' || cmd[0] == 'T' || cmd[0] == 'W' ||
      cmd[0] == 'X' || cmd[0] == 'Y' || cmd[0] == 'Z') {
    const char* arg = skipSpaces(cmd + 1);
    auto reply = [&](unsigned value) {
      char text[16]{};
      snprintf(text, sizeof(text), "%c %u", cmd[0], value);
      sendDedText(channel, 1, text);
    };
    if (*arg == '\0') {
      switch (cmd[0]) {
        case 'A': reply(dedAutoLf_ ? 1U : 0U); return;
        case 'B': reply(dedDamaTimeout_); return;
        case 'E': reply(dedEcho_ ? 1U : 0U); return;
        case 'F': reply(dedFrack_); return;
        case 'K': reply(dedTimestamp_ ? 1U : 0U); return;
        case 'N': reply(dedRetryLimit_); return;
        case 'O': reply(dedMaxFrame_); return;
        case 'P': reply(kissParams_.persistence); return;
        case 'R': reply(digi_.enabled ? 1U : 0U); return;
        case 'T': reply(kissParams_.txDelay); return;
        case 'W': reply(kissParams_.slotTime); return;
        case 'X': reply(dedTxEnabled_ ? 1U : 0U); return;
        case 'Y': reply(dedMaxIncoming_); return;
        case 'Z': reply(static_cast<unsigned>((dedFlow_ ? 1 : 0) + (dedXonXoff_ ? 2 : 0))); return;
      }
    }
    uint8_t value = 0;
    if (!parseByteValue(arg, value)) {
      sendDedText(channel, 2, "INVALID PARAMETER");
      return;
    }
    switch (cmd[0]) {
      case 'A': dedAutoLf_ = value != 0; break;
      case 'B': dedDamaTimeout_ = value; break;
      case 'E': dedEcho_ = value != 0; break;
      case 'F': dedFrack_ = value; applyDedLinkConfig(); break;
      case 'K': dedTimestamp_ = value != 0; break;
      case 'N': if (value > 127) { sendDedText(channel, 2, "INVALID PARAMETER"); return; }
                dedRetryLimit_ = value; applyDedLinkConfig(); break;
      case 'O': if (value < 1 || value > 7) { sendDedText(channel, 2, "INVALID PARAMETER"); return; }
                dedMaxFrame_ = value; applyDedLinkConfig(); break;
      case 'P': kissParams_.persistence = value; break;
      case 'R': digi_.enabled = value != 0; saveSettings(); break;
      case 'T': if (value > 127) { sendDedText(channel, 2, "INVALID PARAMETER"); return; }
                kissParams_.txDelay = value; break;
      case 'W': if (value > 127) { sendDedText(channel, 2, "INVALID PARAMETER"); return; }
                kissParams_.slotTime = value; break;
      case 'X': dedTxEnabled_ = value != 0; break;
      case 'Y': if (value > CHANNEL_COUNT) { sendDedText(channel, 2, "INVALID PARAMETER"); return; }
                dedMaxIncoming_ = value; break;
      case 'Z': if (value > 3) { sendDedText(channel, 2, "INVALID PARAMETER"); return; }
                dedFlow_ = (value & 1) != 0; dedXonXoff_ = (value & 2) != 0; break;
      default:
        sendDedText(channel, 2, "INVALID PARAMETER");
        return;
    }
    saveDedConfig();
    sendDedShort(channel, 0);
    return;
  }

  if (cmd[0] == 'S') {
    const char* arg = skipSpaces(cmd + 1);
    if (*arg == '\0') {
      char text[12]{};
      formatByteParam('S', dedSelectedChannel_, text, sizeof(text));
      sendDedText(channel, 1, text);
      return;
    }
    uint8_t value = 0;
    if (!parseByteValue(arg, value) || value > CHANNEL_COUNT) {
      sendDedText(channel, 2, "INVALID PARAMETER");
      return;
    }
    dedSelectedChannel_ = value;
    sendDedShort(channel, 0);
    return;
  }

  if (cmd[0] == 'U') {
    const char* arg = skipSpaces(cmd + 1);
    if (*arg == '\0') {
      char text[96]{};
      snprintf(text, sizeof(text), "U %u %s", static_cast<unsigned>(dedCtextMode_), dedUnattendedText_);
      sendDedText(channel, 1, text);
      return;
    }
    uint8_t value = 0;
    if (!parseByteValue(arg, value) || value > 2) {
      sendDedText(channel, 2, "INVALID PARAMETER");
      return;
    }
    dedCtextMode_ = value;
    dedUnattended_ = value != 0;
    saveDedConfig();
    sendDedShort(channel, 0);
    return;
  }

  sendDedText(channel, 2, "INVALID COMMAND");
}

void Tnc::sendDedShort(uint8_t channel, uint8_t code) {
  uint8_t out[2] = {channel, code};
  Serial.write(out, sizeof(out));
}

void Tnc::sendDedText(uint8_t channel, uint8_t code, const char* text) {
  Serial.write(channel);
  Serial.write(code);
  if (text != nullptr) Serial.write(reinterpret_cast<const uint8_t*>(text), strlen(text));
  Serial.write(static_cast<uint8_t>(0));
}

void Tnc::sendDedCounted(uint8_t channel, uint8_t code, const uint8_t* data, size_t len) {
  const size_t capped = len > 256 ? 256 : len;
  Serial.write(channel);
  Serial.write(code);
  Serial.write(static_cast<uint8_t>(capped == 0 ? 0 : capped - 1));
  if (capped > 0) Serial.write(data, capped);
}

bool Tnc::enqueueDedEvent(uint8_t channel, uint8_t code, const uint8_t* data, size_t len) {
  DedEvent event{};
  event.channel = channel;
  event.code    = code;
  event.len     = len > sizeof(event.data) ? sizeof(event.data) : len;
  if (data != nullptr && event.len > 0) memcpy(event.data, data, event.len);
  return dedEvents_.push(event);
}

bool Tnc::popDedEvent(uint8_t channel, uint8_t wanted, DedEvent& out) {
  const size_t count = dedEvents_.size();
  for (size_t i = 0; i < count; ++i) {
    DedEvent event{};
    if (!dedEvents_.pop(event)) return false;
    // channel==0 means "any channel"
    const bool channelOk = (channel == 0) || (event.channel == channel);
    const bool typeOk    = (wanted == 0)  || (event.code == wanted);
    if (channelOk && typeOk) { out = event; return true; }
    dedEvents_.push(event);
  }
  return false;
}

void Tnc::handleQuietEscape(uint8_t byte) {
  if (byte == '\r' || byte == '\n') {
    escapeLine_[escapePos_] = '\0';
    if (strcmp(escapeLine_, "console") == 0) {
      setSerialMode(SerialMode::Console); saveSerialMode(SerialMode::Console);
      Serial.println("mode=console");
    }
    escapePos_ = 0;
    return;
  }
  if (escapePos_ < sizeof(escapeLine_) - 1) escapeLine_[escapePos_++] = static_cast<char>(byte);
  else escapePos_ = 0;
}

// ---------------------------------------------------------------------------
// Digipeater
// ---------------------------------------------------------------------------

bool Tnc::maybeDigipeat(const ax25::Frame& frame) {
  if (!digi_.enabled) return false;
  const bool uiFrame = isUiFrame(frame);
  if (!uiFrame && !digi_.allFrames) return false;
  if (ax25::addressEquals(frame.source, local_)) return false;
  const int repeaterIndex = findNextRepeater(frame);
  if (repeaterIndex < 0) return false;

  if (uiFrame) {
    const uint16_t infoCrc = axlora::util::crc16Ccitt(frame.info, frame.infoLen);
    const uint16_t pathCrc = digiPathCrc(frame);
    if (digiSeen(frame, infoCrc, pathCrc)) { ++digiDupes_; return false; }
    rememberDigi(frame, infoCrc, pathCrc);
  }

  ax25::Frame repeated = frame;
  repeated.repeaters[repeaterIndex].repeated = true;
  uint8_t bytes[MAX_PACKET_BYTES]{};
  size_t len = 0;
  if (!ax25::encodeFrame(repeated, bytes, sizeof(bytes), len, true)) { ++digiDrops_; return false; }
  if (!transmitRaw(bytes, len)) { ++digiDrops_; return false; }
  ++digiTx_;
  if (uiFrame) ++digiUiTx_;
  else ++digiConnTx_;
  return true;
}

int Tnc::findNextRepeater(const ax25::Frame& frame) const {
  for (uint8_t i = 0; i < frame.repeaterCount; ++i) {
    if (!frame.repeaters[i].repeated) {
      return matchesDigiAddress(frame.repeaters[i]) ? static_cast<int>(i) : -1;
    }
  }
  return -1;
}

bool Tnc::matchesDigiAddress(const ax25::Address& address) const {
  return ax25::addressEquals(address, local_) ||
         (digi_.hasAlias && ax25::addressEquals(address, digi_.alias));
}

uint16_t Tnc::digiPathCrc(const ax25::Frame& frame) const {
  uint8_t raw[ax25::MAX_REPEATERS * 8]{};
  size_t p = 0;
  for (uint8_t i = 0; i < frame.repeaterCount && p + 8 <= sizeof(raw); ++i) {
    if (!ax25::encodeAddress(frame.repeaters[i], i == frame.repeaterCount - 1, &raw[p])) break;
    p += 7;
    raw[p++] = frame.repeaters[i].repeated ? 1 : 0;
  }
  return axlora::util::crc16Ccitt(raw, p);
}

bool Tnc::digiSeen(const ax25::Frame& frame, uint16_t infoCrc, uint16_t pathCrc) const {
  const uint32_t now = axlora::util::nowMs();
  for (const DigiCacheEntry& entry : digiCache_) {
    if (entry.active && !axlora::util::elapsed(now, entry.seenMs, 30000) &&
        entry.control == frame.control && entry.infoCrc == infoCrc &&
        entry.pathCrc == pathCrc && entry.repeaterCount == frame.repeaterCount &&
        ax25::addressEquals(entry.source, frame.source) &&
        ax25::addressEquals(entry.destination, frame.destination)) {
      return true;
    }
  }
  return false;
}

void Tnc::rememberDigi(const ax25::Frame& frame, uint16_t infoCrc, uint16_t pathCrc) {
  const uint32_t now = axlora::util::nowMs();
  DigiCacheEntry* slot = nullptr;
  for (DigiCacheEntry& entry : digiCache_) {
    if (!entry.active || axlora::util::elapsed(now, entry.seenMs, 30000)) {
      slot = &entry; break;
    }
  }
  if (slot == nullptr) slot = &digiCache_[0];
  slot->active      = true;
  slot->source      = frame.source;
  slot->destination = frame.destination;
  slot->control     = frame.control;
  slot->infoCrc     = infoCrc;
  slot->pathCrc     = pathCrc;
  slot->repeaterCount = frame.repeaterCount;
  slot->seenMs      = now;
}

void Tnc::printDigipeater() const {
  char alias[12]{};
  if (digi_.hasAlias) ax25::formatAddress(digi_.alias, alias, sizeof(alias));
  else strcpy(alias, "off");
  Serial.printf("digipeat=%s mode=%s alias=%s tx=%lu ui_tx=%lu conn_tx=%lu dupes=%lu drops=%lu\n",
                digi_.enabled ? "on" : "off", digi_.allFrames ? "all" : "ui", alias,
                static_cast<unsigned long>(digiTx_),
                static_cast<unsigned long>(digiUiTx_),
                static_cast<unsigned long>(digiConnTx_),
                static_cast<unsigned long>(digiDupes_),
                static_cast<unsigned long>(digiDrops_));
}

// ---------------------------------------------------------------------------
// Mheard
// ---------------------------------------------------------------------------

void Tnc::observeHeard(const ax25::Frame& frame, float rssi, float snr) {
  if (ax25::addressEquals(frame.source, local_)) return;
  const uint32_t now = axlora::util::nowMs();
  MheardEntry* slot = nullptr;
  for (MheardEntry& entry : mheard_) {
    if (entry.active && ax25::addressEquals(entry.source, frame.source)) { slot = &entry; break; }
  }
  if (slot == nullptr) {
    for (MheardEntry& entry : mheard_) {
      if (!entry.active) { slot = &entry; break; }
    }
  }
  if (slot == nullptr) {
    slot = &mheard_[0];
    for (MheardEntry& entry : mheard_) {
      if (entry.lastHeardMs < slot->lastHeardMs) slot = &entry;
    }
  }
  if (!slot->active) {
    *slot = MheardEntry{};
    slot->active       = true;
    slot->source       = frame.source;
    slot->firstHeardMs = now;
  }
  slot->destination       = frame.destination;
  slot->lastHeardMs       = now;
  ++slot->frames;
  slot->lastRssi          = rssi;
  slot->lastSnr           = snr;
  slot->lastRepeaterCount = frame.repeaterCount;
  slot->viaDigipeater     = false;
  for (uint8_t i = 0; i < frame.repeaterCount; ++i) {
    if (frame.repeaters[i].repeated) { slot->viaDigipeater = true; break; }
  }
}

void Tnc::printMheard() const {
  Serial.println("callsign   dest       first    last     frames  rssi   snr  path");
  for (const MheardEntry& entry : mheard_) {
    if (!entry.active) continue;
    char source[12]{}, destination[12]{};
    char first[12]{}, last[12]{};
    ax25::formatAddress(entry.source,      source,      sizeof(source));
    ax25::formatAddress(entry.destination, destination, sizeof(destination));
    formatUptime(entry.firstHeardMs, first, sizeof(first));
    formatUptime(entry.lastHeardMs,  last,  sizeof(last));
    Serial.printf("%-10s %-10s %s %s %6lu %6.1f %5.1f  %s/%u\n",
                  source, destination, first, last,
                  static_cast<unsigned long>(entry.frames),
                  static_cast<double>(entry.lastRssi),
                  static_cast<double>(entry.lastSnr),
                  entry.viaDigipeater ? "via" : "direct",
                  entry.lastRepeaterCount);
  }
}

void Tnc::clearMheard() {
  memset(mheard_, 0, sizeof(mheard_));
}

// ---------------------------------------------------------------------------
// Beacon
// ---------------------------------------------------------------------------

bool Tnc::sendBeacon() {
  ax25::Frame frame{};
  frame.destination  = beacon_.destination;
  frame.source       = local_;
  frame.repeaterCount = beacon_.pathCount;
  for (uint8_t i = 0; i < beacon_.pathCount; ++i) frame.repeaters[i] = beacon_.path[i];
  frame.control  = ax25::CTRL_UI;
  frame.pid      = ax25::PID_NO_LAYER3;
  frame.infoLen  = strnlen(beacon_.text, sizeof(beacon_.text));
  memcpy(frame.info, beacon_.text, frame.infoLen);
  uint8_t bytes[MAX_PACKET_BYTES]{};
  size_t len = 0;
  if (!ax25::encodeFrame(frame, bytes, sizeof(bytes), len, true)) { ++beaconDrops_; return false; }
  if (!transmitRaw(bytes, len)) { ++beaconDrops_; return false; }
  ++beaconTx_;
  return true;
}

void Tnc::printBeacon() const {
  char destination[12]{};
  ax25::formatAddress(beacon_.destination, destination, sizeof(destination));
  Serial.printf("beacon=%s dest=%s interval_s=%lu tx=%lu drops=%lu text=\"%s\"\n",
                beacon_.enabled ? "on" : "off", destination,
                static_cast<unsigned long>(beacon_.intervalMs / 1000),
                static_cast<unsigned long>(beaconTx_),
                static_cast<unsigned long>(beaconDrops_),
                beacon_.text);
  Serial.print("beacon path=");
  if (beacon_.pathCount == 0) { Serial.println("off"); return; }
  for (uint8_t i = 0; i < beacon_.pathCount; ++i) {
    char path[12]{};
    ax25::formatAddress(beacon_.path[i], path, sizeof(path));
    if (i > 0) Serial.print(",");
    Serial.print(path);
  }
  Serial.println();
}

bool Tnc::setBeaconPath(const char* path) {
  beacon_.pathCount = 0;
  if (path == nullptr || strcmp(path, "off") == 0 || path[0] == '\0') return true;
  char buffer[80]{};
  strncpy(buffer, path, sizeof(buffer) - 1);
  char* token = strtok(buffer, ",");
  while (token != nullptr && beacon_.pathCount < ax25::MAX_REPEATERS) {
    while (*token == ' ') ++token;
    if (!ax25::parseAddress(token, beacon_.path[beacon_.pathCount])) {
      beacon_.pathCount = 0; return false;
    }
    ++beacon_.pathCount;
    token = strtok(nullptr, ",");
  }
  return token == nullptr;
}

// ---------------------------------------------------------------------------
// NET/ROM
// ---------------------------------------------------------------------------

void Tnc::observeNetrom(const ax25::Frame& frame) {
  if (ax25::kind(frame.control) != ax25::FrameKind::U ||
      ax25::uType(frame.control) != ax25::UFrameType::UI ||
      frame.pid != 0xCF || frame.infoLen < 15 || frame.info[0] != 0xFF) return;
  size_t p = 1;
  while (p + 14 <= frame.infoLen) {
    NetromRoute route{};
    route.active = true;
    memcpy(route.alias, &frame.info[p], 6);
    route.alias[6] = '\0';
    for (int i = 5; i >= 0; --i) {
      if (route.alias[i] == ' ') route.alias[i] = '\0'; else break;
    }
    p += 6;
    bool last = false;
    if (!ax25::decodeAddress(&frame.info[p], route.node, last)) return;
    p += 7;
    route.quality    = frame.info[p++];
    route.heardFrom  = frame.source;
    route.lastHeardMs = axlora::util::nowMs();

    NetromRoute* slot = nullptr;
    for (NetromRoute& existing : netromRoutes_) {
      if (existing.active && ax25::addressEquals(existing.node, route.node)) { slot = &existing; break; }
    }
    if (slot == nullptr) {
      for (NetromRoute& existing : netromRoutes_) {
        if (!existing.active) { slot = &existing; break; }
      }
    }
    if (slot == nullptr) slot = &netromRoutes_[0];
    *slot = route;
    ++netromRoutesHeard_;
  }
}

bool Tnc::sendNetromBroadcast() {
  ax25::Frame frame{};
  ax25::parseAddress("NODES", frame.destination);
  frame.source  = local_;
  frame.control = ax25::CTRL_UI;
  frame.pid     = 0xCF;
  frame.info[0] = 0xFF;
  memset(&frame.info[1], ' ', 6);
  const size_t aliasLen = strnlen(netrom_.alias, sizeof(netrom_.alias));
  memcpy(&frame.info[1], netrom_.alias, aliasLen > 6 ? 6 : aliasLen);
  ax25::encodeAddress(local_, true, &frame.info[7]);
  frame.info[14] = 200;
  frame.infoLen  = 15;
  uint8_t bytes[MAX_PACKET_BYTES]{};
  size_t len = 0;
  if (!ax25::encodeFrame(frame, bytes, sizeof(bytes), len, true)) return false;
  if (!transmitRaw(bytes, len)) return false;
  ++netromBroadcasts_;
  return true;
}

void Tnc::printNetrom() const {
  Serial.printf("netrom=%s alias=%s ident=\"%s\" interval_s=%lu broadcasts=%lu routes_heard=%lu\n",
                netrom_.enabled ? "on" : "off", netrom_.alias, netrom_.ident,
                static_cast<unsigned long>(netrom_.broadcastIntervalMs / 1000),
                static_cast<unsigned long>(netromBroadcasts_),
                static_cast<unsigned long>(netromRoutesHeard_));
}

void Tnc::printNetromRoutes() const {
  const uint32_t now = axlora::util::nowMs();
  Serial.println("alias node via qual age_s obs");
  for (const NetromRoute& route : netromRoutes_) {
    if (!route.active) continue;
    char node[12]{}, via[12]{};
    ax25::formatAddress(route.node,      node, sizeof(node));
    ax25::formatAddress(route.heardFrom, via,  sizeof(via));
    Serial.printf("%s %s %s %u %lu %u\n",
                  route.alias, node, via, route.quality,
                  static_cast<unsigned long>((now - route.lastHeardMs) / 1000),
                  route.obsolescence);
  }
}

// ---------------------------------------------------------------------------
// Display info
// ---------------------------------------------------------------------------

void Tnc::fillDisplayInfo(display::DisplayInfo& out) const {
  ax25::formatAddress(local_, out.callsign, sizeof(out.callsign));

  const char* mname = serialModeName();
  size_t mi = 0;
  for (; mname[mi] && mi < sizeof(out.mode) - 1; ++mi) {
    const char c = mname[mi];
    out.mode[mi] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
  }
  out.mode[mi] = '\0';

  out.freqMHz  = radioConfig_.frequencyMHz;
  out.sf       = radioConfig_.spreadingFactor;
  out.powerDbm = radioConfig_.powerDbm;

  const radio::Stats& rs = radio::stats();
  out.txCount  = rs.txOk;
  out.rxCount  = rs.rxOk;
  out.lastRssi = lastRssi_;
  out.lastSnr  = lastSnr_;

  out.anyConnected = false;
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    const ax25::LinkState s = channels_[i].link.state();
    if (s == ax25::LinkState::Disconnected) continue;
    out.anyConnected = true;
    out.connChannel  = i + 1;
    ax25::formatAddress(channels_[i].link.peer(), out.connPeer, sizeof(out.connPeer));
    switch (s) {
      case ax25::LinkState::Connecting:    strncpy(out.connState, "CON", 4); break;
      case ax25::LinkState::Connected:     strncpy(out.connState, "OK",  4); break;
      case ax25::LinkState::Disconnecting: strncpy(out.connState, "DIS", 4); break;
      case ax25::LinkState::Recovery:      strncpy(out.connState, "REC", 4); break;
      default:                             strncpy(out.connState, "?",   4); break;
    }
    break;
  }
}

// ---------------------------------------------------------------------------
// Settings (NVS)
// ---------------------------------------------------------------------------

void Tnc::loadSettings() {
  Preferences prefs;
  if (prefs.begin("axloratnc", true)) {
    prefs.getString("callsign", savedCallsign_, sizeof(savedCallsign_));
    radioConfig_.frequencyMHz    = prefs.getFloat("r_freq", variant::DEFAULT_FREQUENCY_MHZ);
    radioConfig_.frequencyCorrectionMHz = prefs.getFloat("r_fcorr", variant::DEFAULT_FREQUENCY_CORRECTION_MHZ);
    radioConfig_.bandwidthKhz    = prefs.getFloat("r_bw",   variant::DEFAULT_BANDWIDTH_KHZ);
    radioConfig_.spreadingFactor = prefs.getUChar("r_sf",   variant::DEFAULT_SPREADING_FACTOR);
    radioConfig_.codingRate      = prefs.getUChar("r_cr",   variant::DEFAULT_CODING_RATE);
    radioConfig_.powerDbm        = static_cast<int8_t>(prefs.getChar("r_pwr", variant::DEFAULT_TX_POWER_DBM));
    dutyCycleEnabled_            = prefs.getBool("duty_en", true);
    dutyCyclePpm_                = prefs.getULong("duty_ppm", variant::DUTY_CYCLE_PPM);
    serialMode_ = static_cast<SerialMode>(
        prefs.getUChar("mode", static_cast<uint8_t>(SerialMode::Console)));
    digi_.enabled  = prefs.getBool("digi_en",       digi_.enabled);
    digi_.hasAlias = prefs.getBool("digi_alias_en",  digi_.hasAlias);
    digi_.allFrames = prefs.getBool("digi_all",      digi_.allFrames);
    char addr[16]{};
    prefs.getString("digi_alias", addr, sizeof(addr));
    textToAddress(addr, digi_.alias);
    beacon_.enabled    = prefs.getBool("bc_en",  beacon_.enabled);
    beacon_.intervalMs = prefs.getULong("bc_int", beacon_.intervalMs);
    prefs.getString("bc_text", beacon_.text, sizeof(beacon_.text));
    prefs.getString("bc_dest", addr, sizeof(addr));
    textToAddress(addr, beacon_.destination);
    beacon_.pathCount = prefs.getUChar("bc_path_n", 0);
    if (beacon_.pathCount > ax25::MAX_REPEATERS) beacon_.pathCount = 0;
    for (uint8_t i = 0; i < beacon_.pathCount; ++i) {
      char key[12]{};
      snprintf(key, sizeof(key), "bc_p%u", static_cast<unsigned>(i));
      prefs.getString(key, addr, sizeof(addr));
      if (!textToAddress(addr, beacon_.path[i])) { beacon_.pathCount = 0; break; }
    }
    netrom_.enabled             = prefs.getBool("nr_en",   netrom_.enabled);
    netrom_.broadcastIntervalMs = prefs.getULong("nr_int", netrom_.broadcastIntervalMs);
    prefs.getString("nr_alias", netrom_.alias, sizeof(netrom_.alias));
    prefs.getString("nr_ident", netrom_.ident, sizeof(netrom_.ident));
    // WA8DED / link parameters
    dedFrack_            = prefs.getUShort("d_frack",  dedFrack_);
    dedRetryLimit_       = prefs.getUChar ("d_n2",     dedRetryLimit_);
    dedMaxFrame_         = prefs.getUChar ("d_maxfr",  dedMaxFrame_);
    kissParams_.txDelay      = prefs.getUChar("d_txdel", kissParams_.txDelay);
    kissParams_.slotTime     = prefs.getUChar("d_slot",  kissParams_.slotTime);
    kissParams_.persistence  = prefs.getUChar("d_pers",  kissParams_.persistence);
    kissParams_.fullDuplex   = prefs.getUChar("d_fdup",  kissParams_.fullDuplex);
    dedTxEnabled_        = prefs.getBool  ("d_txen",   dedTxEnabled_);
    dedMaxIncoming_      = prefs.getUChar ("d_maxin",  dedMaxIncoming_);
    dedFlow_             = prefs.getBool  ("d_flow",   dedFlow_);
    dedXonXoff_          = prefs.getBool  ("d_xon",    dedXonXoff_);
    dedT2_               = prefs.getUShort("d_t2",     dedT2_);
    dedT3_               = prefs.getUShort("d_t3",     dedT3_);
    dedIPollFrameLength_ = prefs.getUChar ("d_ipoll",  dedIPollFrameLength_);
    dedDamaTimeout_      = prefs.getUShort("d_dama",   dedDamaTimeout_);
    dedHeardMode_        = prefs.getUChar ("d_heard",  dedHeardMode_);
    dedEcho_             = prefs.getBool  ("d_echo",   dedEcho_);
    dedAutoLf_           = prefs.getBool  ("d_autolf", dedAutoLf_);
    dedTimestamp_        = prefs.getBool  ("d_ts",     dedTimestamp_);
    dedEightBitTerminal_ = prefs.getBool  ("d_8bit",   dedEightBitTerminal_);
    dedValidateCallsign_ = prefs.getBool  ("d_valcs",  dedValidateCallsign_);
    dedCtextMode_        = prefs.getUChar ("d_ctext",  dedCtextMode_);
    dedUnattended_       = dedCtextMode_ != 0;
    prefs.getString("d_utext",  dedUnattendedText_,  sizeof(dedUnattendedText_));
    prefs.getString("d_mon",    dedMonitorMode_,      sizeof(dedMonitorMode_));
    monitorEnabled_ = (strchr(dedMonitorMode_, 'N') == nullptr);
    char uproto[16]{};
    prefs.getString("d_uproto", uproto, sizeof(uproto));
    if (uproto[0] != '\0') textToAddress(uproto, dedUnprotoDestination_);
    baudKiss_ = prefs.getULong("baud_kiss", 9600U);
    baudDed_  = prefs.getULong("baud_ded",  9600U);
    if (!isValidBaud(baudKiss_)) baudKiss_ = 9600U;
    if (!isValidBaud(baudDed_))  baudDed_  = 9600U;
    prefs.end();
  }
  if (serialMode_ != SerialMode::Console &&
      serialMode_ != SerialMode::Kiss &&
      serialMode_ != SerialMode::Wa8ded) {
    serialMode_ = SerialMode::Console;
  }
  axlora::util::setLogEnabled(serialMode_ == SerialMode::Console);
}

void Tnc::saveSettings() {
  Preferences prefs;
  if (!prefs.begin("axloratnc", false)) return;
  prefs.putBool("digi_en",      digi_.enabled);
  prefs.putBool("digi_alias_en", digi_.hasAlias);
  prefs.putBool("digi_all",      digi_.allFrames);
  char text[16]{};
  addressToText(digi_.alias, text, sizeof(text));
  prefs.putString("digi_alias", text);
  prefs.putBool("bc_en",   beacon_.enabled);
  prefs.putULong("bc_int", beacon_.intervalMs);
  prefs.putString("bc_text", beacon_.text);
  addressToText(beacon_.destination, text, sizeof(text));
  prefs.putString("bc_dest", text);
  prefs.putUChar("bc_path_n", beacon_.pathCount);
  for (uint8_t i = 0; i < beacon_.pathCount; ++i) {
    char key[12]{};
    snprintf(key, sizeof(key), "bc_p%u", static_cast<unsigned>(i));
    addressToText(beacon_.path[i], text, sizeof(text));
    prefs.putString(key, text);
  }
  prefs.putBool("nr_en",   netrom_.enabled);
  prefs.putULong("nr_int", netrom_.broadcastIntervalMs);
  prefs.putString("nr_alias", netrom_.alias);
  prefs.putString("nr_ident", netrom_.ident);
  prefs.end();
}

void Tnc::saveDedConfig() {
  Preferences prefs;
  if (!prefs.begin("axloratnc", false)) return;
  prefs.putUShort("d_frack",  dedFrack_);
  prefs.putUChar ("d_n2",     dedRetryLimit_);
  prefs.putUChar ("d_maxfr",  dedMaxFrame_);
  prefs.putUChar ("d_txdel",  kissParams_.txDelay);
  prefs.putUChar ("d_slot",   kissParams_.slotTime);
  prefs.putUChar ("d_pers",   kissParams_.persistence);
  prefs.putUChar ("d_fdup",   kissParams_.fullDuplex);
  prefs.putBool  ("d_txen",   dedTxEnabled_);
  prefs.putUChar ("d_maxin",  dedMaxIncoming_);
  prefs.putBool  ("d_flow",   dedFlow_);
  prefs.putBool  ("d_xon",    dedXonXoff_);
  prefs.putUShort("d_t2",     dedT2_);
  prefs.putUShort("d_t3",     dedT3_);
  prefs.putUChar ("d_ipoll",  dedIPollFrameLength_);
  prefs.putUShort("d_dama",   dedDamaTimeout_);
  prefs.putUChar ("d_heard",  dedHeardMode_);
  prefs.putBool  ("d_echo",   dedEcho_);
  prefs.putBool  ("d_autolf", dedAutoLf_);
  prefs.putBool  ("d_ts",     dedTimestamp_);
  prefs.putBool  ("d_8bit",   dedEightBitTerminal_);
  prefs.putBool  ("d_valcs",  dedValidateCallsign_);
  prefs.putUChar ("d_ctext",  dedCtextMode_);
  prefs.putString("d_utext",  dedUnattendedText_);
  prefs.putString("d_mon",    dedMonitorMode_);
  char uproto[16]{};
  addressToText(dedUnprotoDestination_, uproto, sizeof(uproto));
  prefs.putString("d_uproto", uproto);
  prefs.end();
}

void Tnc::saveRadioConfig() {
  Preferences prefs;
  if (!prefs.begin("axloratnc", false)) return;
  prefs.putFloat("r_freq", radioConfig_.frequencyMHz);
  prefs.putFloat("r_fcorr", radioConfig_.frequencyCorrectionMHz);
  prefs.putFloat("r_bw",   radioConfig_.bandwidthKhz);
  prefs.putUChar("r_sf",   radioConfig_.spreadingFactor);
  prefs.putUChar("r_cr",   radioConfig_.codingRate);
  prefs.putChar("r_pwr",   radioConfig_.powerDbm);
  prefs.putBool("duty_en", dutyCycleEnabled_);
  prefs.putULong("duty_ppm", dutyCyclePpm_);
  prefs.end();
}

void Tnc::applyRadioConfig() {
  auto& drv = radio::driver();
  drv.setDutyCycle(dutyCycleEnabled_, dutyCyclePpm_);
  drv.setFrequencyCorrection(radioConfig_.frequencyCorrectionMHz);
  drv.setFrequency(radioConfig_.frequencyMHz);
  drv.setSpreadingFactor(radioConfig_.spreadingFactor);
  drv.setBandwidth(radioConfig_.bandwidthKhz);
  drv.setCodingRate(radioConfig_.codingRate);
  drv.setPower(radioConfig_.powerDbm);
  LOG_RADIO("radio config applied: freq=%.3f corr=%+.1fkHz sf=%u bw=%.1f cr=%u pwr=%d",
            static_cast<double>(radioConfig_.frequencyMHz),
            static_cast<double>(radioConfig_.frequencyCorrectionMHz * 1000.0f),
            radioConfig_.spreadingFactor,
            static_cast<double>(radioConfig_.bandwidthKhz),
            radioConfig_.codingRate,
            radioConfig_.powerDbm);
}

void Tnc::saveSerialMode(SerialMode mode) {
  Preferences prefs;
  if (prefs.begin("axloratnc", false)) {
    prefs.putUChar("mode", static_cast<uint8_t>(mode));
    prefs.end();
  }
}

void Tnc::setSerialMode(SerialMode mode) {
  serialMode_   = mode;
  dedHostMode_  = false;
  if (mode == SerialMode::Wa8ded) {
    kissParams_.txDelay = 25;
    kissParams_.persistence = 32;
    kissParams_.slotTime = 10;
  }
  kissActive_   = false;
  linePos_      = 0;
  escapePos_    = 0;
  dedTerminalCommand_ = false;
  dedOutputPaused_ = false;
  dedHeaderPos_ = 0;
  dedDataPos_   = 0;
  dedDataLen_   = 0;
  applySerialBaud();
  axlora::util::setLogEnabled(serialMode_ == SerialMode::Console);
}

void Tnc::applySerialBaud() {
  uint32_t baud = variant::SERIAL_BAUD;
  if (serialMode_ == SerialMode::Kiss)   baud = baudKiss_;
  if (serialMode_ == SerialMode::Wa8ded) baud = baudDed_;
  Serial.updateBaudRate(baud);
  // Hardware RTS/CTS flow control: only active in non-console modes where the
  // host may be an old DOS application that drives flow-control lines.
  // Disabled at compile time when PIN_UART_RTS/CTS are -1 (all current variants).
  if constexpr (variant::PIN_UART_RTS >= 0 && variant::PIN_UART_CTS >= 0) {
    if (serialMode_ == SerialMode::Wa8ded || serialMode_ == SerialMode::Kiss) {
      // Re-assign UART0 pins so the hardware CTS/RTS lines are connected.
      Serial.setPins(3 /*RX*/, 1 /*TX*/,
                     variant::PIN_UART_CTS, variant::PIN_UART_RTS);
      Serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS_RTS, 64);
    } else {
      Serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE, 0);
    }
  }
}

const char* Tnc::serialModeName() const {
  switch (serialMode_) {
    case SerialMode::Console: return "console";
    case SerialMode::Kiss:    return "kiss";
    case SerialMode::Wa8ded:  return "ded";
    default:                  return "?";
  }
}

}  // namespace axlora::tnc
