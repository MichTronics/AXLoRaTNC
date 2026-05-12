#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ax25/ax25_addr.h"
#include "ax25/ax25_kiss.h"
#include "ax25/ax25_l2.h"
#include "radio/radio.h"

namespace axlora::tnc {

struct KissParams {
  uint8_t txDelay = 30;
  uint8_t persistence = 63;
  uint8_t slotTime = 10;
  uint8_t fullDuplex = 0;
};

class Tnc {
 public:
  void begin(const char* callsign);
  void loop(bool radioReady);
  void printInfo() const;
  void printStats() const;
  bool connect(const char* destination);
  bool disconnect();
  bool sendUi(const char* destination, const char* text);
  bool sendConnected(const char* text);
  ax25::LinkLayer& link() { return link_; }

 private:
  static bool radioTxCallback(const uint8_t* data, size_t len, void* ctx);
  static void dataCallback(const uint8_t* data, size_t len, bool connected, void* ctx);
  void serviceRadio();
  void serviceSerial();
  void handleKiss(const ax25::KissFrame& frame);
  void emitKissData(const uint8_t* frameNoFcs, size_t len);
  void handleConsoleLine(char* line);

  ax25::Address local_{};
  ax25::LinkLayer link_;
  ax25::KissDecoder kiss_;
  KissParams kissParams_{};
  char line_[256]{};
  size_t linePos_ = 0;
  bool kissActive_ = false;
  uint32_t rawTx_ = 0;
  uint32_t rawRx_ = 0;
};

}
