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

void Tnc::begin(const char* callsign) {
  loadSettings();
  if (!ax25::parseAddress(callsign, local_)) {
    ax25::parseAddress("N0CALL", local_);
  }
  ax25::L2Config config{};
  config.local = local_;
  link_.begin(config, radioTxCallback, dataCallback, this);
}

void Tnc::loop(bool radioReady) {
  serviceSerial();
  link_.loop();
  checkLinkStatusEvent();
  if (radioReady) {
    serviceRadio();
  }
}

bool Tnc::radioTxCallback(const uint8_t* data, size_t len, void* ctx) {
  Tnc* self = static_cast<Tnc*>(ctx);
  const radio::Result result = radio::driver().send(data, len);
  if (result == radio::Result::Ok) {
    ++self->rawTx_;
    return true;
  }
  LOG_WARN("radio tx failed: %s", radio::resultName(result));
  return false;
}

void Tnc::dataCallback(const uint8_t* data, size_t len, bool connected, void* ctx) {
  Tnc* self = static_cast<Tnc*>(ctx);
  if (self != nullptr && self->serialMode_ == SerialMode::Wa8ded) {
    self->enqueueDedEvent(connected ? 1 : 0, connected ? 7 : 6, data, len);
  }
  if (!axlora::util::logEnabled()) {
    return;
  }
  Serial.printf("[%s] ", connected ? "AX25/I" : "AX25/UI");
  for (size_t i = 0; i < len; ++i) {
    Serial.write(data[i]);
  }
  Serial.println();
}

void Tnc::serviceRadio() {
  radio::RxPacket rx{};
  if (radio::driver().receive(rx) != radio::Result::Ok) {
    return;
  }
  ++rawRx_;
  if (rx.len >= 2 && ax25::checkFcs(rx.data, rx.len)) {
    emitKissData(rx.data, rx.len - 2);
    ax25::Frame frame{};
    if (ax25::decodeFrame(rx.data, rx.len, frame, true)) {
      maybeDigipeat(frame);
    }
  }
  link_.receive(rx.data, rx.len);
}

void Tnc::serviceSerial() {
  while (Serial.available() > 0) {
    const uint8_t b = static_cast<uint8_t>(Serial.read());
    if (serialMode_ == SerialMode::Wa8ded) {
      serviceWa8ded(b);
      handleQuietEscape(b);
      continue;
    }
    ax25::KissFrame frame{};
    if (b == ax25::KISS_FEND) {
      kissActive_ = true;
    }
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
    if (b == '\r') {
      continue;
    }
    if (b == '\n') {
      line_[linePos_] = '\0';
      handleConsoleLine(line_);
      linePos_ = 0;
      continue;
    }
    if (linePos_ < sizeof(line_) - 1) {
      line_[linePos_++] = static_cast<char>(b);
    }
  }
}

void Tnc::handleKiss(const ax25::KissFrame& frame) {
  if (frame.command == ax25::KissCommand::Data) {
    uint8_t withFcs[MAX_PACKET_BYTES]{};
    size_t len = 0;
    if (ax25::appendFcs(frame.data, frame.len, withFcs, sizeof(withFcs), len)) {
      radioTxCallback(withFcs, len, this);
    }
    return;
  }
  if (frame.len < 1) {
    return;
  }
  switch (frame.command) {
    case ax25::KissCommand::TxDelay: kissParams_.txDelay = frame.data[0]; break;
    case ax25::KissCommand::Persistence: kissParams_.persistence = frame.data[0]; break;
    case ax25::KissCommand::SlotTime: kissParams_.slotTime = frame.data[0]; break;
    case ax25::KissCommand::FullDuplex: kissParams_.fullDuplex = frame.data[0]; break;
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

void Tnc::handleConsoleLine(char* line) {
  char* cmd = strtok(line, " ");
  if (cmd == nullptr) {
    return;
  }
  if (strcmp(cmd, "help") == 0) {
    Serial.println("AXLoRaTNC commands: help, info, mode, mode console|kiss|ded, radio, ax25, digi, digi on|off, digialias <CALLSIGN-SSID|off>, connect <CALLSIGN-SSID>, disconnect, sendui <DEST> <message>, send <message>, stats");
  } else if (strcmp(cmd, "info") == 0) {
    printInfo();
  } else if (strcmp(cmd, "mode") == 0) {
    char* mode = strtok(nullptr, " ");
    if (mode == nullptr) {
      Serial.printf("mode=%s\n", serialModeName());
    } else if (strcmp(mode, "kiss") == 0) {
      Serial.println("mode=kiss");
      Serial.flush();
      setSerialMode(SerialMode::Kiss);
      saveSerialMode(SerialMode::Kiss);
    } else if (strcmp(mode, "ded") == 0 || strcmp(mode, "wa8ded") == 0) {
      Serial.println("mode=ded");
      Serial.flush();
      setSerialMode(SerialMode::Wa8ded);
      saveSerialMode(SerialMode::Wa8ded);
    } else if (strcmp(mode, "console") == 0) {
      setSerialMode(SerialMode::Console);
      saveSerialMode(SerialMode::Console);
      Serial.println("mode=console");
    } else {
      Serial.println("usage: mode [console|kiss|ded]");
    }
  } else if (strcmp(cmd, "radio") == 0) {
    Serial.printf("RSSI=%.1f SNR=%.1f\n",
                  static_cast<double>(radio::driver().getRSSI()),
                  static_cast<double>(radio::driver().getSNR()));
  } else if (strcmp(cmd, "ax25") == 0) {
    link_.printStatus();
  } else if (strcmp(cmd, "digi") == 0) {
    char* mode = strtok(nullptr, " ");
    if (mode == nullptr) {
      printDigipeater();
    } else if (strcmp(mode, "on") == 0) {
      digi_.enabled = true;
      printDigipeater();
    } else if (strcmp(mode, "off") == 0) {
      digi_.enabled = false;
      printDigipeater();
    } else {
      Serial.println("usage: digi [on|off]");
    }
  } else if (strcmp(cmd, "digialias") == 0) {
    char* value = strtok(nullptr, " ");
    if (value == nullptr) {
      printDigipeater();
    } else if (strcmp(value, "off") == 0) {
      digi_.hasAlias = false;
      printDigipeater();
    } else if (ax25::parseAddress(value, digi_.alias)) {
      digi_.hasAlias = true;
      printDigipeater();
    } else {
      Serial.println("usage: digialias <CALLSIGN-SSID|off>");
    }
  } else if (strcmp(cmd, "connect") == 0) {
    char* dest = strtok(nullptr, " ");
    Serial.println(connect(dest) ? "connecting" : "connect failed");
  } else if (strcmp(cmd, "disconnect") == 0) {
    Serial.println(disconnect() ? "disconnecting" : "disconnect failed");
  } else if (strcmp(cmd, "sendui") == 0) {
    char* dest = strtok(nullptr, " ");
    char* msg = strtok(nullptr, "");
    Serial.println(sendUi(dest, msg) ? "ui queued" : "sendui failed");
  } else if (strcmp(cmd, "send") == 0) {
    char* msg = strtok(nullptr, "");
    if (sendConnected(msg)) {
      Serial.printf("i queued depth=%u\n", static_cast<unsigned>(link_.connectedQueueSize()));
    } else {
      Serial.println("send failed");
    }
  } else if (strcmp(cmd, "stats") == 0) {
    printStats();
  } else {
    Serial.println("unknown command");
  }
}

void Tnc::printInfo() const {
  char local[12]{};
  ax25::formatAddress(local_, local, sizeof(local));
  Serial.printf("AXLoRaTNC local=%s variant=%s radio=%s freq=%.3f bw=%.1f sf=%u cr=4/%u\n",
                local,
                variant::NAME,
                variant::RADIO_TYPE == variant::RadioType::SX1262 ? "SX1262" : "SX1276",
                static_cast<double>(variant::DEFAULT_FREQUENCY_MHZ),
                static_cast<double>(variant::DEFAULT_BANDWIDTH_KHZ),
                variant::DEFAULT_SPREADING_FACTOR,
                variant::DEFAULT_CODING_RATE);
  Serial.println("LoRa transports one complete AX.25 frame per LoRa packet, with AX.25 FCS retained.");
  Serial.printf("serial mode=%s\n", serialModeName());
}

void Tnc::printStats() const {
  const radio::Stats& rs = radio::stats();
  Serial.printf("tnc raw_tx=%lu raw_rx=%lu kiss_txdelay=%u p=%u slot=%u fulldup=%u\n",
                static_cast<unsigned long>(rawTx_),
                static_cast<unsigned long>(rawRx_),
                kissParams_.txDelay,
                kissParams_.persistence,
                kissParams_.slotTime,
                kissParams_.fullDuplex);
  Serial.printf("digi enabled=%u tx=%lu dupes=%lu drops=%lu\n",
                digi_.enabled,
                static_cast<unsigned long>(digiTx_),
                static_cast<unsigned long>(digiDupes_),
                static_cast<unsigned long>(digiDrops_));
  Serial.printf("radio tx_ok=%lu tx_fail=%lu rx_ok=%lu rx_fail=%lu duty_drops=%lu\n",
                static_cast<unsigned long>(rs.txOk),
                static_cast<unsigned long>(rs.txFail),
                static_cast<unsigned long>(rs.rxOk),
                static_cast<unsigned long>(rs.rxFail),
                static_cast<unsigned long>(rs.dutyDrops));
  link_.printStats();
}

bool Tnc::connect(const char* destination) {
  ax25::Address dest{};
  return ax25::parseAddress(destination, dest) && link_.connectTo(dest);
}

bool Tnc::disconnect() {
  return link_.disconnect();
}

bool Tnc::sendUi(const char* destination, const char* text) {
  ax25::Address dest{};
  if (!ax25::parseAddress(destination, dest) || text == nullptr) {
    return false;
  }
  return link_.sendUi(dest, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

bool Tnc::sendConnected(const char* text) {
  if (text == nullptr) {
    return false;
  }
  return link_.sendConnected(reinterpret_cast<const uint8_t*>(text), strlen(text));
}

bool Tnc::maybeDigipeat(const ax25::Frame& frame) {
  if (!digi_.enabled || ax25::kind(frame.control) != ax25::FrameKind::U ||
      ax25::uType(frame.control) != ax25::UFrameType::UI) {
    return false;
  }
  if (ax25::addressEquals(frame.source, local_)) {
    return false;
  }
  const int repeaterIndex = findNextRepeater(frame);
  if (repeaterIndex < 0) {
    return false;
  }

  const uint16_t infoCrc = axlora::util::crc16Ccitt(frame.info, frame.infoLen);
  if (digiSeen(frame, infoCrc)) {
    ++digiDupes_;
    return false;
  }
  rememberDigi(frame, infoCrc);

  ax25::Frame repeated = frame;
  repeated.repeaters[repeaterIndex].repeated = true;
  uint8_t bytes[MAX_PACKET_BYTES]{};
  size_t len = 0;
  if (!ax25::encodeFrame(repeated, bytes, sizeof(bytes), len, true)) {
    ++digiDrops_;
    return false;
  }
  const radio::Result result = radio::driver().send(bytes, len);
  if (result != radio::Result::Ok) {
    ++digiDrops_;
    LOG_WARN("digipeat tx failed: %s", radio::resultName(result));
    return false;
  }
  ++digiTx_;
  LOG_PROTO("digipeated UI via index=%d", repeaterIndex);
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
  return ax25::addressEquals(address, local_) || (digi_.hasAlias && ax25::addressEquals(address, digi_.alias));
}

bool Tnc::digiSeen(const ax25::Frame& frame, uint16_t infoCrc) const {
  const uint32_t now = axlora::util::nowMs();
  for (const DigiCacheEntry& entry : digiCache_) {
    if (!entry.active || !axlora::util::elapsed(now, entry.seenMs, 30000)) {
      if (entry.active && entry.control == frame.control && entry.infoCrc == infoCrc &&
          ax25::addressEquals(entry.source, frame.source) &&
          ax25::addressEquals(entry.destination, frame.destination)) {
        return true;
      }
    }
  }
  return false;
}

void Tnc::rememberDigi(const ax25::Frame& frame, uint16_t infoCrc) {
  const uint32_t now = axlora::util::nowMs();
  DigiCacheEntry* slot = nullptr;
  for (DigiCacheEntry& entry : digiCache_) {
    if (!entry.active || axlora::util::elapsed(now, entry.seenMs, 30000)) {
      slot = &entry;
      break;
    }
  }
  if (slot == nullptr) {
    slot = &digiCache_[0];
  }
  slot->active = true;
  slot->source = frame.source;
  slot->destination = frame.destination;
  slot->control = frame.control;
  slot->infoCrc = infoCrc;
  slot->seenMs = now;
}

void Tnc::printDigipeater() const {
  char alias[12]{};
  if (digi_.hasAlias) {
    ax25::formatAddress(digi_.alias, alias, sizeof(alias));
  } else {
    strcpy(alias, "off");
  }
  Serial.printf("digipeat=%s alias=%s tx=%lu dupes=%lu drops=%lu\n",
                digi_.enabled ? "on" : "off",
                alias,
                static_cast<unsigned long>(digiTx_),
                static_cast<unsigned long>(digiDupes_),
                static_cast<unsigned long>(digiDrops_));
}

void Tnc::serviceWa8ded(uint8_t byte) {
  if (dedHeaderPos_ < sizeof(dedHeader_)) {
    dedHeader_[dedHeaderPos_++] = byte;
    if (dedHeaderPos_ == sizeof(dedHeader_)) {
      dedDataLen_ = static_cast<size_t>(dedHeader_[2]) + 1;
      dedDataPos_ = 0;
    }
    return;
  }
  if (dedDataPos_ < sizeof(dedData_)) {
    dedData_[dedDataPos_++] = byte;
  }
  if (dedDataPos_ >= dedDataLen_) {
    handleDedHostFrame(dedHeader_[0], dedHeader_[1], dedData_, dedDataLen_);
    dedHeaderPos_ = 0;
    dedDataPos_ = 0;
    dedDataLen_ = 0;
  }
}

void Tnc::handleDedHostFrame(uint8_t channel, uint8_t infoCmd, const uint8_t* data, size_t len) {
  if (infoCmd == 0) {
    if (channel == 0) {
      ax25::Address dest{};
      ax25::parseAddress("CQ", dest);
      if (link_.sendUi(dest, data, len)) {
        sendDedShort(channel, 0);
      } else {
        sendDedText(channel, 2, "TNC BUSY - LINE IGNORED");
      }
      return;
    }
    if (channel == 1 && link_.state() == ax25::LinkState::Connected && link_.sendConnected(data, len)) {
      sendDedShort(channel, 0);
    } else {
      sendDedText(channel, 2, "TNC BUSY - LINE IGNORED");
    }
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
  const size_t copyLen = len >= sizeof(cmd) ? sizeof(cmd) - 1 : len;
  memcpy(cmd, command, copyLen);
  for (size_t i = 0; i < copyLen; ++i) {
    if (cmd[i] >= 'a' && cmd[i] <= 'z') {
      cmd[i] = static_cast<char>(cmd[i] - 32);
    }
  }

  if (cmd[0] == 'G') {
    const uint8_t wanted = cmd[1] == '0' ? 7 : (cmd[1] == '1' ? 3 : 0);
    DedEvent event{};
    if (popDedEvent(channel, wanted, event)) {
      if (event.code == 6 || event.code == 7) {
        sendDedCounted(event.channel, event.code, event.data, event.len);
      } else if (event.code == 1 || event.code == 2 || event.code == 3) {
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
    while (*dest == ' ') {
      ++dest;
    }
    if (channel == 1 && connect(dest)) {
      sendDedShort(channel, 0);
    } else {
      sendDedText(channel, 2, "CONNECT FAILED");
    }
    return;
  }

  if (cmd[0] == 'D') {
    if (channel == 1 && disconnect()) {
      sendDedShort(channel, 0);
    } else {
      sendDedText(channel, 2, "DISCONNECT FAILED");
    }
    return;
  }

  if (cmd[0] == 'L') {
    char status[48]{};
    snprintf(status, sizeof(status), "%u %u %u %u %u %u",
             link_.state() == ax25::LinkState::Connected ? 1U : 0U,
             static_cast<unsigned>(link_.connectedQueueSize()),
             0U, 0U, 0U, 0U);
    sendDedText(channel, 1, status);
    return;
  }

  if (strncmp(cmd, "JHOST0", 6) == 0) {
    sendDedShort(channel, 0);
    setSerialMode(SerialMode::Console);
    saveSerialMode(SerialMode::Console);
    return;
  }

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
  if (text != nullptr) {
    Serial.write(reinterpret_cast<const uint8_t*>(text), strlen(text));
  }
  Serial.write(static_cast<uint8_t>(0));
}

void Tnc::sendDedCounted(uint8_t channel, uint8_t code, const uint8_t* data, size_t len) {
  const size_t capped = len > 256 ? 256 : len;
  Serial.write(channel);
  Serial.write(code);
  Serial.write(static_cast<uint8_t>(capped == 0 ? 0 : capped - 1));
  if (capped > 0) {
    Serial.write(data, capped);
  }
}

bool Tnc::enqueueDedEvent(uint8_t channel, uint8_t code, const uint8_t* data, size_t len) {
  DedEvent event{};
  event.channel = channel;
  event.code = code;
  event.len = len > sizeof(event.data) ? sizeof(event.data) : len;
  if (data != nullptr && event.len > 0) {
    memcpy(event.data, data, event.len);
  }
  return dedEvents_.push(event);
}

bool Tnc::popDedEvent(uint8_t channel, uint8_t wanted, DedEvent& out) {
  const size_t count = dedEvents_.size();
  for (size_t i = 0; i < count; ++i) {
    DedEvent event{};
    if (!dedEvents_.pop(event)) {
      return false;
    }
    const bool channelOk = event.channel == channel || (channel == 0 && event.channel == 0);
    const bool typeOk = wanted == 0 || event.code == wanted;
    if (channelOk && typeOk) {
      out = event;
      return true;
    }
    dedEvents_.push(event);
  }
  return false;
}

void Tnc::checkLinkStatusEvent() {
  const ax25::LinkState state = link_.state();
  if (state == lastDedState_) {
    return;
  }
  lastDedState_ = state;
  const char* text = nullptr;
  switch (state) {
    case ax25::LinkState::Connecting: text = "(1) LINK RESET to station"; break;
    case ax25::LinkState::Connected: text = "(1) CONNECTED"; break;
    case ax25::LinkState::Disconnecting: text = "(1) DISCONNECTING"; break;
    case ax25::LinkState::Disconnected: text = "(1) DISCONNECTED"; break;
    case ax25::LinkState::Recovery: text = "(1) LINK FAILURE"; break;
    default: break;
  }
  if (text != nullptr) {
    enqueueDedEvent(1, 3, reinterpret_cast<const uint8_t*>(text), strlen(text));
  }
}

void Tnc::handleQuietEscape(uint8_t byte) {
  if (byte == '\r') {
    return;
  }
  if (byte == '\n') {
    escapeLine_[escapePos_] = '\0';
    if (strcmp(escapeLine_, "console") == 0) {
      setSerialMode(SerialMode::Console);
      saveSerialMode(SerialMode::Console);
      Serial.println("mode=console");
    }
    escapePos_ = 0;
    return;
  }
  if (escapePos_ < sizeof(escapeLine_) - 1) {
    escapeLine_[escapePos_++] = static_cast<char>(byte);
  } else {
    escapePos_ = 0;
  }
}

void Tnc::loadSettings() {
  Preferences prefs;
  if (prefs.begin("axloratnc", true)) {
    serialMode_ = static_cast<SerialMode>(prefs.getUChar("mode", static_cast<uint8_t>(SerialMode::Console)));
    prefs.end();
  }
  if (serialMode_ != SerialMode::Console && serialMode_ != SerialMode::Kiss && serialMode_ != SerialMode::Wa8ded) {
    serialMode_ = SerialMode::Console;
  }
  axlora::util::setLogEnabled(serialMode_ == SerialMode::Console);
}

void Tnc::saveSerialMode(SerialMode mode) {
  Preferences prefs;
  if (prefs.begin("axloratnc", false)) {
    prefs.putUChar("mode", static_cast<uint8_t>(mode));
    prefs.end();
  }
}

void Tnc::setSerialMode(SerialMode mode) {
  serialMode_ = mode;
  kissActive_ = false;
  linePos_ = 0;
  escapePos_ = 0;
  dedHeaderPos_ = 0;
  dedDataPos_ = 0;
  dedDataLen_ = 0;
  axlora::util::setLogEnabled(serialMode_ == SerialMode::Console);
}

const char* Tnc::serialModeName() const {
  switch (serialMode_) {
    case SerialMode::Console: return "console";
    case SerialMode::Kiss: return "kiss";
    case SerialMode::Wa8ded: return "ded";
    default: return "?";
  }
}

}
