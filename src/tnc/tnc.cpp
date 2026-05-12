#include "tnc.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "ax25/ax25_fcs.h"
#include "ax25/ax25_frame.h"
#include "util/log.h"

namespace axlora::tnc {

void Tnc::begin(const char* callsign) {
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

void Tnc::dataCallback(const uint8_t* data, size_t len, bool connected, void*) {
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
  }
  link_.receive(rx.data, rx.len);
}

void Tnc::serviceSerial() {
  while (Serial.available() > 0) {
    const uint8_t b = static_cast<uint8_t>(Serial.read());
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
    Serial.println("AXLoRaTNC commands: help, info, radio, ax25, connect <CALLSIGN-SSID>, disconnect, sendui <DEST> <message>, send <message>, stats");
  } else if (strcmp(cmd, "info") == 0) {
    printInfo();
  } else if (strcmp(cmd, "radio") == 0) {
    Serial.printf("RSSI=%.1f SNR=%.1f\n",
                  static_cast<double>(radio::driver().getRSSI()),
                  static_cast<double>(radio::driver().getSNR()));
  } else if (strcmp(cmd, "ax25") == 0) {
    link_.printStatus();
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
    Serial.println(sendConnected(msg) ? "i queued" : "send failed");
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

}
