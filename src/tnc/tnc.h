#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "ax25/ax25_addr.h"
#include "ax25/ax25_frame.h"
#include "ax25/ax25_kiss.h"
#include "ax25/ax25_l2.h"
#include "display/display.h"
#include "mailbox.h"
#include "radio/radio.h"
#include "util/ringbuffer.h"

namespace axlora::tnc {

static constexpr uint8_t CHANNEL_COUNT = 8;

struct KissParams {
  uint8_t txDelay    = 0;     // 0 ms — LoRa has no RF warm-up requirement
  uint8_t persistence = 255;  // transmit immediately (LoRa: no collision risk on single freq)
  uint8_t slotTime   = 20;    // 200 ms slot
  uint8_t fullDuplex = 0;
};

struct DigipeaterConfig {
  bool         enabled  = false;
  bool         allFrames = false;
  ax25::Address alias{};
  bool         hasAlias = false;
};

struct BeaconConfig {
  bool          enabled     = false;
  ax25::Address destination{};
  ax25::Address path[ax25::MAX_REPEATERS]{};
  uint8_t       pathCount   = 0;
  uint32_t      intervalMs  = 600000;
  uint32_t      lastTxMs    = 0;
  char          text[96]{};
};

struct NetromConfig {
  bool     enabled             = false;
  char     alias[7]{};
  char     ident[48]{};
  uint32_t broadcastIntervalMs = 1800000;
  uint32_t lastBroadcastMs     = 0;
};

enum class SerialMode : uint8_t {
  Console = 0,
  Kiss    = 1,
  Wa8ded  = 2,
};

class Tnc {
 public:
  void begin(const char* callsign);
  void loop(bool radioReady);
  bool quietSerialMode() const { return serialMode_ != SerialMode::Console; }
  void printInfo() const;
  void printStats() const;
  void fillDisplayInfo(display::DisplayInfo& out) const;
  bool connect(uint8_t chIdx, const char* destination);
  bool disconnect(uint8_t chIdx);
  bool sendUi(const char* destination, const char* text);
  bool sendConnected(uint8_t chIdx, const char* text);

 private:
  // Per-channel transmit context (passed as void* to LinkLayer callbacks)
  struct ChannelCtx {
    Tnc*    tnc   = nullptr;
    uint8_t chIdx = 0;   // 0-based index into channels_[]
  };

  enum class ShellMode : uint8_t { Node, Bbs, BbsCompose };

  struct ChannelState {
    ax25::LinkLayer   link{};
    ax25::LinkState   lastState       = ax25::LinkState::Disconnected;
    char              nodeLine[96]{};
    size_t            nodeLinePos     = 0;
    bool              nodeGreetingSent = false;
    ShellMode         shellMode       = ShellMode::Node;
    // BBS compose state
    char              composeTo[12]{};
    char              composeBody[Mailbox::MAX_BODY + 1]{};
    size_t            composeBodyPos  = 0;
  };

  struct DigiCacheEntry {
    bool          active = false;
    ax25::Address source{};
    ax25::Address destination{};
    uint16_t      infoCrc = 0;
    uint16_t      pathCrc = 0;
    uint8_t       control = 0;
    uint8_t       repeaterCount = 0;
    uint32_t      seenMs  = 0;
  };

  struct DedEvent {
    uint8_t channel = 0;
    uint8_t code    = 0;
    uint8_t data[256]{};
    size_t  len     = 0;
  };

  struct MheardEntry {
    bool          active            = false;
    ax25::Address source{};
    ax25::Address destination{};
    uint32_t      firstHeardMs      = 0;
    uint32_t      lastHeardMs       = 0;
    uint32_t      frames            = 0;
    float         lastRssi          = 0.0f;
    float         lastSnr           = 0.0f;
    uint8_t       lastRepeaterCount = 0;
    bool          viaDigipeater     = false;
  };

  struct NetromRoute {
    bool          active       = false;
    char          alias[7]{};
    ax25::Address node{};
    ax25::Address heardFrom{};
    uint8_t       quality      = 0;
    uint8_t       obsolescence = 6;
    uint32_t      lastHeardMs  = 0;
  };

  struct PendingRawTx {
    uint8_t  data[MAX_PACKET_BYTES]{};
    size_t   len         = 0;
    uint32_t notBeforeMs = 0;
    uint8_t  chIdx       = 0xFF;  // 0xFF = not a link-layer frame
    uint32_t id          = 0;
    bool     priority    = false;
  };

  static constexpr size_t RAW_TX_QUEUE_CAPACITY = 160;
  static constexpr size_t RAW_TX_QUEUE_FALLBACK_CAPACITY = 64;
  static constexpr size_t RAW_TX_PRIORITY_QUEUE_CAPACITY = 32;
  static constexpr size_t RAW_TX_PRIORITY_QUEUE_FALLBACK_CAPACITY = 16;
  static constexpr size_t KISS_SERIAL_TXQ_HEADROOM = 4;

  class RawTxQueue {
   public:
    bool begin(size_t desiredCapacity, size_t fallbackCapacity) {
      if (items_ != nullptr) return true;
      capacity_ = desiredCapacity;
      items_ = static_cast<PendingRawTx*>(malloc(sizeof(PendingRawTx) * capacity_));
      if (items_ == nullptr && fallbackCapacity > 0 && fallbackCapacity < desiredCapacity) {
        capacity_ = fallbackCapacity;
        items_ = static_cast<PendingRawTx*>(malloc(sizeof(PendingRawTx) * capacity_));
      }
      clear();
      return items_ != nullptr;
    }

    bool push(const PendingRawTx& item) {
      if (items_ == nullptr || full()) return false;
      items_[head_] = item;
      head_ = (head_ + 1) % capacity_;
      ++count_;
      return true;
    }

    bool pop(PendingRawTx& out) {
      if (items_ == nullptr || empty()) return false;
      out = items_[tail_];
      tail_ = (tail_ + 1) % capacity_;
      --count_;
      return true;
    }

    bool peek(PendingRawTx& out) const {
      if (items_ == nullptr || empty()) return false;
      out = items_[tail_];
      return true;
    }

    bool peek(size_t i, PendingRawTx& out) const {
      if (items_ == nullptr || i >= count_) return false;
      out = items_[(tail_ + i) % capacity_];
      return true;
    }

    bool empty() const { return count_ == 0; }
    bool full() const { return items_ == nullptr || count_ == capacity_; }
    size_t size() const { return count_; }
    size_t free() const { return capacity_ > count_ ? capacity_ - count_ : 0; }
    size_t capacity() const { return capacity_; }
    void clear() { head_ = 0; tail_ = 0; count_ = 0; }

   private:
    PendingRawTx* items_ = nullptr;
    size_t capacity_ = 0;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
  };

  // Static LinkLayer callbacks
  static bool radioTxCallback(const uint8_t* data, size_t len, void* ctx);
  static void dataCallback(const uint8_t* data, size_t len, bool connected, void* ctx);

  // Radio helpers
  bool transmitRaw(const uint8_t* data, size_t len, uint8_t chIdx = 0xFF);
  bool enqueueRawTx(const uint8_t* data, size_t len, uint32_t delayMs, uint8_t chIdx = 0xFF);
  bool rawFramePriority(const uint8_t* data, size_t len) const;
  void serviceRawTx(bool radioReady);
  void serviceRadio();
  int  findChannelForIncoming(const ax25::Frame& frame) const;

  // Timed services
  void serviceBeacon(bool radioReady);
  void serviceNetrom(bool radioReady);

  // Serial
  void serviceSerial();
  void handleKiss(const ax25::KissFrame& frame);
  void emitKissData(const uint8_t* frameNoFcs, size_t len);
  void handleConsoleLine(char* line);
  void handleQuietEscape(uint8_t byte);

  // WA8DED
  void serviceWa8ded(uint8_t byte);
  void serviceWa8dedHost(uint8_t byte);
  void serviceWa8dedTerminal(uint8_t byte);
  void handleDedTerminalLine(const char* line, bool command);
  void handleDedAtCommand(const char* cmd);
  void applyDedLinkConfig();
  void serviceDedTerminalOutput();
  void printDedTerminalStatus(int channel) const;
  void handleDedHostFrame(uint8_t channel, uint8_t infoCmd, const uint8_t* data, size_t len);
  void handleDedCommand(uint8_t channel, const char* command, size_t len);
  void sendDedShort(uint8_t channel, uint8_t code);
  void sendDedText(uint8_t channel, uint8_t code, const char* text);
  void sendDedCounted(uint8_t channel, uint8_t code, const uint8_t* data, size_t len);
  unsigned dedFreeBufferBytes(uint8_t channel) const;
  size_t dedConnectedFrameCapacity(uint8_t chIdx) const;
  size_t dedConnectedAcceptFrameCapacity(uint8_t chIdx) const;
  bool enqueueDedEvent(uint8_t channel, uint8_t code, const uint8_t* data, size_t len);
  size_t pendingDedEvents(uint8_t channel, uint8_t wanted = 0);
  bool popDedEvent(uint8_t channel, uint8_t wanted, DedEvent& out);
  void checkLinkStatusEvents();

  // Node / BBS shell (per channel, chIdx = 0-based)
  void processNodeInput(uint8_t chIdx, const uint8_t* data, size_t len);
  void handleNodeLine(uint8_t chIdx, const char* line);
  void handleBbsLine(uint8_t chIdx, const char* line);
  void handleBbsComposeLine(uint8_t chIdx, const char* line);
  void sendNodeText(uint8_t chIdx, const char* text);
  // Splits data into PACLEN-sized I-frames before queuing
  bool sendConnectedChunked(uint8_t chIdx, const uint8_t* data, size_t len);

  // Digipeater
  bool maybeDigipeat(const ax25::Frame& frame);
  int  findNextRepeater(const ax25::Frame& frame) const;
  bool matchesDigiAddress(const ax25::Address& address) const;
  uint16_t digiPathCrc(const ax25::Frame& frame) const;
  bool digiSeen(const ax25::Frame& frame, uint16_t infoCrc, uint16_t pathCrc) const;
  void rememberDigi(const ax25::Frame& frame, uint16_t infoCrc, uint16_t pathCrc);
  void printDigipeater() const;

  // Mheard
  void observeHeard(const ax25::Frame& frame, float rssi, float snr);
  void printMheard() const;
  void clearMheard();

  // Beacon
  bool sendBeacon();
  void printBeacon() const;
  bool setBeaconPath(const char* path);

  // NET/ROM
  void observeNetrom(const ax25::Frame& frame);
  bool sendNetromBroadcast();
  void printNetrom() const;
  void printNetromRoutes() const;

  // Settings
  void loadSettings();
  void saveSettings();
  void saveDedConfig();
  void saveKissConfig();
  void saveRadioConfig();
  void beginLink(uint8_t chIdx, const ax25::Address& local);
  void resetLinksForLocal();
  bool setChannelLocal(uint8_t chIdx, const ax25::Address& local);
  void applyRadioConfig();
  void saveSerialMode(SerialMode mode);
  void setSerialMode(SerialMode mode);
  void applySerialBaud();
  const char* serialModeName() const;

  // CSMA
  bool isChannelBusy() const;

  // ---- Members ----
  ax25::Address       local_{};
  char                savedCallsign_[12]{};
  radio::RadioConfig  radioConfig_{};
  bool                dutyCycleEnabled_  = true;
  uint32_t            dutyCyclePpm_      = variant::DUTY_CYCLE_PPM;
  bool                radioConfigApplied_ = false;
  uint32_t            lastRxMs_           = 0;
  ChannelCtx     channelCtx_[CHANNEL_COUNT]{};
  ChannelState   channels_[CHANNEL_COUNT]{};

  ax25::KissDecoder kiss_;
  KissParams        kissParams_{};
  char              line_[256]{};
  size_t            linePos_     = 0;
  char              escapeLine_[16]{};
  size_t            escapePos_   = 0;
  bool              kissActive_  = false;
  SerialMode        serialMode_  = SerialMode::Console;
  uint32_t          baudKiss_    = 9600;
  uint32_t          baudDed_     = 9600;

  // WA8DED host framing state
  uint8_t dedHeader_[3]{};
  uint8_t dedHeaderPos_ = 0;
  uint8_t dedData_[256]{};
  size_t  dedDataPos_   = 0;
  size_t  dedDataLen_   = 0;
  axlora::util::RingBuffer<DedEvent, 32> dedEvents_;
  bool    dedHostMode_         = false;
  bool    dedTerminalCommand_  = false;
  bool    dedEcho_             = true;
  bool    dedAutoLf_           = true;
  bool    dedTimestamp_        = false;
  bool    dedFlow_             = true;
  bool    dedXonXoff_          = true;
  bool    dedOutputPaused_     = false;
  bool    dedUnattended_       = false;
  uint8_t dedCtextMode_        = 0;
  uint8_t dedSelectedChannel_  = 0;
  uint8_t dedMaxIncoming_      = 4;
  uint16_t dedDamaTimeout_     = 120;
  uint16_t dedFrack_           = 3000;  // T1 = 30000 ms for lab-speed LoRa/BBS tests
  uint8_t dedHeardMode_        = 0;
  uint8_t dedRetryLimit_       = 12;    // More retries while tuning faster LoRa profiles
  uint8_t dedMaxFrame_         = 2;     // Lab profile may pipeline lightly for throughput
  bool    dedTxEnabled_        = true;
  uint8_t dedSrttA1_           = 7;
  uint8_t dedSrttA2_           = 15;
  uint8_t dedSrttA3_           = 3;
  uint8_t dedIPollFrameLength_ = 64;    // Fits LinFBB 64-byte host blocks in one I-frame
  bool    dedEightBitTerminal_ = true;
  bool    dedValidateCallsign_ = true;
  uint16_t dedT2_              = 20;    // T2 = 200 ms for quicker RR/ACK turnaround in lab tests
  uint16_t dedT3_              = 18000;
  ax25::Address dedUnprotoDestination_{};
  char    dedMonitorMode_[20]  = "IU";
  char    dedUnattendedText_[80]{};
  char    dedLastCallsign_[12]{};
  uint8_t dedLastCallsignChannel_ = 0;
  char    dedLastConnect_[12]{};
  uint8_t dedLastConnectChannel_ = 0;

  DigipeaterConfig digi_{};
  DigiCacheEntry   digiCache_[16]{};
  BeaconConfig     beacon_{};
  MheardEntry      mheard_[20]{};
  NetromConfig     netrom_{};
  NetromRoute      netromRoutes_[20]{};

  bool     monitorEnabled_   = false;
  uint32_t monitorLastEnqueueMs_ = 0;
  float    lastRssi_         = 0.0f;
  float    lastSnr_          = 0.0f;
  uint32_t rawTx_           = 0;
  uint32_t rawRx_           = 0;
  uint32_t rawTxQueued_     = 0;
  uint32_t rawTxDeferred_   = 0;
  uint32_t rawTxQueueDrops_ = 0;
  uint32_t rawTxNextId_     = 1;
  uint32_t rawTxHighWater_  = 0;
  uint32_t nextRawTxAttemptMs_ = 0;
  RawTxQueue rawTxQueue_;
  RawTxQueue rawTxPriorityQueue_;
  uint32_t kissInFrames_ = 0;
  uint32_t kissInBytes_ = 0;
  uint32_t kissDecodeErrors_ = 0;
  uint32_t kissIFrameDuplicates_ = 0;
  uint32_t ax25IFramesIn_ = 0;
  uint32_t ax25IFramesOut_ = 0;
  uint32_t ax25UiFrames_ = 0;
  uint32_t ax25RrRx_ = 0;
  uint32_t ax25RrTx_ = 0;
  uint32_t ax25RejRx_ = 0;
  uint32_t ax25RejTx_ = 0;
  uint32_t radioTxStarted_ = 0;
  uint32_t radioTxDone_ = 0;
  uint32_t radioRxFrames_ = 0;
  uint32_t radioCrcFail_ = 0;
  uint32_t kissOutFrames_ = 0;
  uint32_t kissOutPartialWrites_ = 0;
  uint32_t kissOutBlockMsMax_ = 0;
  uint32_t beaconTx_        = 0;
  uint32_t beaconDrops_     = 0;
  uint32_t netromBroadcasts_  = 0;
  uint32_t netromRoutesHeard_ = 0;
  uint32_t digiTx_          = 0;
  uint32_t digiUiTx_        = 0;
  uint32_t digiConnTx_      = 0;
  uint32_t digiDupes_       = 0;
  uint32_t digiDrops_       = 0;

  Mailbox mailbox_;
};

}
