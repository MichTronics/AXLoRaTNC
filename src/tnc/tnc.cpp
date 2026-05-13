#include "tnc.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include "ax25/ax25_fcs.h"
#include "ax25/ax25_frame.h"
#include "util/crc.h"
#include "util/log.h"
#include "util/timer.h"

namespace axlora::tnc {
namespace {

void addressToText(const ax25::Address& address, char* out, size_t outCap) {
  ax25::formatAddress(address, out, outCap);
}

bool textToAddress(const char* text, ax25::Address& out) {
  return text != nullptr && text[0] != '\0' && ax25::parseAddress(text, out);
}

// Case-insensitive callsign comparison (no SSID)
bool callsignEq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = (*a >= 'a' && *a <= 'z') ? static_cast<char>(*a - 32) : *a;
    char cb = (*b >= 'a' && *b <= 'z') ? static_cast<char>(*b - 32) : *b;
    if (ca != cb) return false;
    ++a; ++b;
  }
  return *a == '\0' && *b == '\0';
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Tnc::begin(const char* callsign) {
  loadSettings();
  mailbox_.begin();
  if (!ax25::parseAddress(callsign, local_)) {
    ax25::parseAddress("N0CALL", local_);
  }
  if (beacon_.destination.callsign[0] == '\0') {
    ax25::parseAddress("CQ", beacon_.destination);
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
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    channelCtx_[i].tnc   = this;
    channelCtx_[i].chIdx = i;
    channels_[i].link.begin(config, radioTxCallback, dataCallback, &channelCtx_[i]);
  }
}

void Tnc::loop(bool radioReady) {
  serviceSerial();
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) {
    channels_[i].link.loop();
  }
  checkLinkStatusEvents();
  if (radioReady) {
    serviceBeacon(radioReady);
    serviceNetrom(radioReady);
    serviceRadio();
  }
}

// ---------------------------------------------------------------------------
// Radio TX / RX
// ---------------------------------------------------------------------------

bool Tnc::transmitRaw(const uint8_t* data, size_t len) {
  const radio::Result result = radio::driver().send(data, len);
  if (result == radio::Result::Ok) {
    ++rawTx_;
    return true;
  }
  LOG_WARN("radio tx failed: %s", radio::resultName(result));
  return false;
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
    self->enqueueDedEvent(channel, connected ? 7 : 6, data, len);
    return;
  }
  if (connected && self->netrom_.enabled) {
    self->processNodeInput(chIdx, data, len);
    return;
  }
  if (axlora::util::logEnabled()) {
    Serial.printf("[%s ch%u] ", connected ? "AX25/I" : "AX25/UI", channel);
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
  ++rawRx_;
  if (rx.len >= 2 && ax25::checkFcs(rx.data, rx.len)) {
    emitKissData(rx.data, rx.len - 2);
    ax25::Frame frame{};
    if (ax25::decodeFrame(rx.data, rx.len, frame, true)) {
      observeHeard(frame, rx.rssi, rx.snr);
      observeNetrom(frame);
      maybeDigipeat(frame);
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
    Serial.println("Commands: help info mode radio ax25 node nodes routes beacon mheard digi");
    Serial.println("          connect [ch] <CALL> disconnect [ch] sendui <DEST> <msg>");
    Serial.println("          send [ch] <msg> bbs stats");

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

  } else if (strcmp(cmd, "radio") == 0) {
    Serial.printf("RSSI=%.1f SNR=%.1f\n",
                  static_cast<double>(radio::driver().getRSSI()),
                  static_cast<double>(radio::driver().getSNR()));

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
    } else {
      Serial.println("usage: beacon [on|off|now|text|dest|interval|path]");
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
    else Serial.println("usage: digi [on|off]");

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
  Serial.printf("AXLoRaTNC local=%s variant=%s radio=%s freq=%.3f bw=%.1f sf=%u cr=4/%u\n",
                local, variant::NAME,
                variant::RADIO_TYPE == variant::RadioType::SX1262 ? "SX1262" : "SX1276",
                static_cast<double>(variant::DEFAULT_FREQUENCY_MHZ),
                static_cast<double>(variant::DEFAULT_BANDWIDTH_KHZ),
                variant::DEFAULT_SPREADING_FACTOR,
                variant::DEFAULT_CODING_RATE);
  Serial.printf("serial mode=%s channels=%u\n", serialModeName(), static_cast<unsigned>(CHANNEL_COUNT));
}

void Tnc::printStats() const {
  const radio::Stats& rs = radio::stats();
  Serial.printf("tnc raw_tx=%lu raw_rx=%lu kiss_txdelay=%u p=%u slot=%u fulldup=%u\n",
                static_cast<unsigned long>(rawTx_), static_cast<unsigned long>(rawRx_),
                kissParams_.txDelay, kissParams_.persistence, kissParams_.slotTime, kissParams_.fullDuplex);
  Serial.printf("digi enabled=%u tx=%lu dupes=%lu drops=%lu\n",
                digi_.enabled,
                static_cast<unsigned long>(digiTx_),
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
  Serial.printf("radio tx_ok=%lu tx_fail=%lu rx_ok=%lu rx_fail=%lu duty_drops=%lu\n",
                static_cast<unsigned long>(rs.txOk), static_cast<unsigned long>(rs.txFail),
                static_cast<unsigned long>(rs.rxOk), static_cast<unsigned long>(rs.rxFail),
                static_cast<unsigned long>(rs.dutyDrops));
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
  return channels_[chIdx].link.sendConnected(reinterpret_cast<const uint8_t*>(text), strlen(text));
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
      case ax25::LinkState::Connected:     dedStatus = "CONNECTED";             break;
      case ax25::LinkState::Disconnecting: dedStatus = "DISCONNECTING";         break;
      case ax25::LinkState::Disconnected:  dedStatus = "DISCONNECTED";          break;
      case ax25::LinkState::Recovery:      dedStatus = "LINK FAILURE";          break;
      default: break;
    }
    if (dedStatus != nullptr) {
      char text[64]{};
      snprintf(text, sizeof(text), "(%u) %s", static_cast<unsigned>(channel), dedStatus);
      enqueueDedEvent(channel, 3, reinterpret_cast<const uint8_t*>(text), strlen(text));
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
    const uint32_t now = axlora::util::nowMs();
    sendNodeText(chIdx, "callsign         dest       age_s  rssi  snr  via\r");
    for (const MheardEntry& entry : mheard_) {
      if (!entry.active) continue;
      char src[12]{}, dst[12]{};
      ax25::formatAddress(entry.source,      src, sizeof(src));
      ax25::formatAddress(entry.destination, dst, sizeof(dst));
      char row[64]{};
      snprintf(row, sizeof(row), "%-10s %-10s %-6lu %5.1f %5.1f %s\r",
               src, dst,
               static_cast<unsigned long>((now - entry.lastHeardMs) / 1000),
               static_cast<double>(entry.lastRssi),
               static_cast<double>(entry.lastSnr),
               entry.viaDigipeater ? "via" : "direct");
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
    channels_[chIdx].link.sendConnected(
        reinterpret_cast<const uint8_t*>(text), strlen(text));
  }
}

// ---------------------------------------------------------------------------
// WA8DED hostmode
// ---------------------------------------------------------------------------

void Tnc::serviceWa8ded(uint8_t byte) {
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
      if (channels_[chIdx].link.state() == ax25::LinkState::Connected &&
          channels_[chIdx].link.sendConnected(data, len)) {
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
    // G / G0 / G1 polling
    const uint8_t wanted = cmd[1] == '0' ? 7 : (cmd[1] == '1' ? 3 : 0);
    DedEvent event{};
    if (popDedEvent(channel, wanted, event)) {
      if (event.code == 6 || event.code == 7) {
        sendDedCounted(event.channel, event.code, event.data, event.len);
      } else if (event.code >= 1 && event.code <= 3) {
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

  if (cmd[0] == 'C') {
    const char* dest = cmd + 1;
    while (*dest == ' ') ++dest;
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

  if (cmd[0] == 'L') {
    const uint8_t chIdx = (channel >= 1 && channel <= CHANNEL_COUNT) ? channel - 1 : 0;
    char status[48]{};
    snprintf(status, sizeof(status), "%u %u %u %u %u %u",
             channels_[chIdx].link.state() == ax25::LinkState::Connected ? 1U : 0U,
             static_cast<unsigned>(channels_[chIdx].link.connectedQueueSize()),
             0U, 0U, 0U, 0U);
    sendDedText(channel, 1, status);
    return;
  }

  if (strncmp(cmd, "JHOST0", 6) == 0) {
    sendDedShort(channel, 0);
    setSerialMode(SerialMode::Console); saveSerialMode(SerialMode::Console);
    return;
  }

  // Acknowledge but ignore: JHOST1, M, U, T, P, S, F, N, O, V
  if (strncmp(cmd, "JHOST1", 6) == 0 || cmd[0] == 'M' || cmd[0] == 'U' ||
      cmd[0] == 'T' || cmd[0] == 'P' || cmd[0] == 'S' || cmd[0] == 'F' ||
      cmd[0] == 'N' || cmd[0] == 'O' || cmd[0] == 'V') {
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
  if (byte == '\r') return;
  if (byte == '\n') {
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
  if (!digi_.enabled || ax25::kind(frame.control) != ax25::FrameKind::U ||
      ax25::uType(frame.control) != ax25::UFrameType::UI) return false;
  if (ax25::addressEquals(frame.source, local_)) return false;
  const int repeaterIndex = findNextRepeater(frame);
  if (repeaterIndex < 0) return false;

  const uint16_t infoCrc = axlora::util::crc16Ccitt(frame.info, frame.infoLen);
  if (digiSeen(frame, infoCrc)) { ++digiDupes_; return false; }
  rememberDigi(frame, infoCrc);

  ax25::Frame repeated = frame;
  repeated.repeaters[repeaterIndex].repeated = true;
  uint8_t bytes[MAX_PACKET_BYTES]{};
  size_t len = 0;
  if (!ax25::encodeFrame(repeated, bytes, sizeof(bytes), len, true)) { ++digiDrops_; return false; }
  const radio::Result result = radio::driver().send(bytes, len);
  if (result != radio::Result::Ok) { ++digiDrops_; return false; }
  ++digiTx_;
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

bool Tnc::digiSeen(const ax25::Frame& frame, uint16_t infoCrc) const {
  const uint32_t now = axlora::util::nowMs();
  for (const DigiCacheEntry& entry : digiCache_) {
    if (entry.active && !axlora::util::elapsed(now, entry.seenMs, 30000) &&
        entry.control == frame.control && entry.infoCrc == infoCrc &&
        ax25::addressEquals(entry.source, frame.source) &&
        ax25::addressEquals(entry.destination, frame.destination)) {
      return true;
    }
  }
  return false;
}

void Tnc::rememberDigi(const ax25::Frame& frame, uint16_t infoCrc) {
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
  slot->seenMs      = now;
}

void Tnc::printDigipeater() const {
  char alias[12]{};
  if (digi_.hasAlias) ax25::formatAddress(digi_.alias, alias, sizeof(alias));
  else strcpy(alias, "off");
  Serial.printf("digipeat=%s alias=%s tx=%lu dupes=%lu drops=%lu\n",
                digi_.enabled ? "on" : "off", alias,
                static_cast<unsigned long>(digiTx_),
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
  const uint32_t now = axlora::util::nowMs();
  Serial.println("mheard callsign dest age_s frames rssi snr via path");
  for (const MheardEntry& entry : mheard_) {
    if (!entry.active) continue;
    char source[12]{}, destination[12]{};
    ax25::formatAddress(entry.source,      source,      sizeof(source));
    ax25::formatAddress(entry.destination, destination, sizeof(destination));
    Serial.printf("%s %s %lu %lu %.1f %.1f %s %u\n",
                  source, destination,
                  static_cast<unsigned long>((now - entry.lastHeardMs) / 1000),
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
  const radio::Result result = radio::driver().send(bytes, len);
  if (result != radio::Result::Ok) { ++beaconDrops_; return false; }
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
  const radio::Result result = radio::driver().send(bytes, len);
  if (result != radio::Result::Ok) return false;
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
// Settings (NVS)
// ---------------------------------------------------------------------------

void Tnc::loadSettings() {
  Preferences prefs;
  if (prefs.begin("axloratnc", true)) {
    serialMode_ = static_cast<SerialMode>(
        prefs.getUChar("mode", static_cast<uint8_t>(SerialMode::Console)));
    digi_.enabled  = prefs.getBool("digi_en",       digi_.enabled);
    digi_.hasAlias = prefs.getBool("digi_alias_en",  digi_.hasAlias);
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

void Tnc::saveSerialMode(SerialMode mode) {
  Preferences prefs;
  if (prefs.begin("axloratnc", false)) {
    prefs.putUChar("mode", static_cast<uint8_t>(mode));
    prefs.end();
  }
}

void Tnc::setSerialMode(SerialMode mode) {
  serialMode_   = mode;
  kissActive_   = false;
  linePos_      = 0;
  escapePos_    = 0;
  dedHeaderPos_ = 0;
  dedDataPos_   = 0;
  dedDataLen_   = 0;
  axlora::util::setLogEnabled(serialMode_ == SerialMode::Console);
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
