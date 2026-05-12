#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ax25/ax25_addr.h"
#include "ax25/ax25_frame.h"
#include "ax25/ax25_kiss.h"
#include "ax25/ax25_l2.h"
#include "radio/radio.h"
#include "util/ringbuffer.h"

namespace axlora::tnc {

struct KissParams {
  uint8_t txDelay = 30;
  uint8_t persistence = 63;
  uint8_t slotTime = 10;
  uint8_t fullDuplex = 0;
};

struct DigipeaterConfig {
  bool enabled = false;
  ax25::Address alias{};
  bool hasAlias = false;
};

enum class SerialMode : uint8_t {
  Console = 0,
  Kiss = 1,
  Wa8ded = 2,
};

class Tnc {
 public:
  void begin(const char* callsign);
  void loop(bool radioReady);
  bool quietSerialMode() const { return serialMode_ != SerialMode::Console; }
  void printInfo() const;
  void printStats() const;
  bool connect(const char* destination);
  bool disconnect();
  bool sendUi(const char* destination, const char* text);
  bool sendConnected(const char* text);
  ax25::LinkLayer& link() { return link_; }

 private:
  struct DigiCacheEntry {
    bool active = false;
    ax25::Address source{};
    ax25::Address destination{};
    uint16_t infoCrc = 0;
    uint8_t control = 0;
    uint32_t seenMs = 0;
  };
  struct DedEvent {
    uint8_t channel = 0;
    uint8_t code = 0;
    uint8_t data[256]{};
    size_t len = 0;
  };

  static bool radioTxCallback(const uint8_t* data, size_t len, void* ctx);
  static void dataCallback(const uint8_t* data, size_t len, bool connected, void* ctx);
  void serviceRadio();
  void serviceSerial();
  void handleKiss(const ax25::KissFrame& frame);
  void emitKissData(const uint8_t* frameNoFcs, size_t len);
  void handleConsoleLine(char* line);
  void handleQuietEscape(uint8_t byte);
  void serviceWa8ded(uint8_t byte);
  void handleDedHostFrame(uint8_t channel, uint8_t infoCmd, const uint8_t* data, size_t len);
  void handleDedCommand(uint8_t channel, const char* command, size_t len);
  void sendDedShort(uint8_t channel, uint8_t code);
  void sendDedText(uint8_t channel, uint8_t code, const char* text);
  void sendDedCounted(uint8_t channel, uint8_t code, const uint8_t* data, size_t len);
  bool enqueueDedEvent(uint8_t channel, uint8_t code, const uint8_t* data, size_t len);
  bool popDedEvent(uint8_t channel, uint8_t wanted, DedEvent& out);
  void checkLinkStatusEvent();
  void loadSettings();
  void saveSerialMode(SerialMode mode);
  void setSerialMode(SerialMode mode);
  const char* serialModeName() const;
  bool maybeDigipeat(const ax25::Frame& frame);
  int findNextRepeater(const ax25::Frame& frame) const;
  bool matchesDigiAddress(const ax25::Address& address) const;
  bool digiSeen(const ax25::Frame& frame, uint16_t infoCrc) const;
  void rememberDigi(const ax25::Frame& frame, uint16_t infoCrc);
  void printDigipeater() const;

  ax25::Address local_{};
  ax25::LinkLayer link_;
  ax25::KissDecoder kiss_;
  KissParams kissParams_{};
  char line_[256]{};
  size_t linePos_ = 0;
  char escapeLine_[16]{};
  size_t escapePos_ = 0;
  bool kissActive_ = false;
  SerialMode serialMode_ = SerialMode::Console;
  uint8_t dedHeader_[3]{};
  uint8_t dedHeaderPos_ = 0;
  uint8_t dedData_[256]{};
  size_t dedDataPos_ = 0;
  size_t dedDataLen_ = 0;
  ax25::LinkState lastDedState_ = ax25::LinkState::Disconnected;
  axlora::util::RingBuffer<DedEvent, 12> dedEvents_;
  DigipeaterConfig digi_{};
  DigiCacheEntry digiCache_[16]{};
  uint32_t rawTx_ = 0;
  uint32_t rawRx_ = 0;
  uint32_t digiTx_ = 0;
  uint32_t digiDupes_ = 0;
  uint32_t digiDrops_ = 0;
};

}
