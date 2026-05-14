#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ax25_addr.h"
#include "ax25_frame.h"
#include "ax25_timers.h"
#include "axlora_config.h"
#include "util/ringbuffer.h"

namespace axlora::ax25 {

enum class LinkState : uint8_t {
  Disconnected,
  Connecting,
  Connected,
  Disconnecting,
  Recovery,
};

struct L2Config {
  Address local{};
  uint32_t t1Ms = 3000;
  uint32_t t2Ms = 500;
  uint32_t t3Ms = 30000;
  uint32_t t4Ms = 60000;  // RNR busy-timeout; 0 = disabled
  uint8_t n2 = 5;
};

struct L2Stats {
  uint32_t uiRx = 0;
  uint32_t uiTx = 0;
  uint32_t iRx = 0;
  uint32_t iTx = 0;
  uint32_t retries = 0;
  uint32_t rejTx = 0;
  uint32_t srejTx = 0;
  uint32_t srejRx = 0;
  uint32_t fcsDrops = 0;
  uint32_t stateChanges = 0;
  uint32_t queued = 0;
  uint32_t queueDrops = 0;
  uint32_t frmrTx = 0;
  uint32_t sabmeRx = 0;
  uint32_t xidRx = 0;
  uint32_t testRx = 0;
  uint32_t nrInvalid = 0;
};

class LinkLayer {
 public:
  using TxCallback = bool (*)(const uint8_t* data, size_t len, void* ctx);
  using DataCallback = void (*)(const uint8_t* data, size_t len, bool connected, void* ctx);

  void begin(const L2Config& config, TxCallback tx, DataCallback data, void* ctx);
  void loop();
  bool sendUi(const Address& destination, const uint8_t* data, size_t len);
  bool connectTo(const Address& destination);
  bool disconnect();
  bool sendConnected(const uint8_t* data, size_t len);
  size_t connectedQueueSize() const { return txQueue_.size(); }
  size_t connectedQueueFree() const { return txQueue_.free(); }
  uint8_t outstandingFrameCount() const { return outstandingCount(); }
  uint8_t retryCount() const { return retryCount_; }
  void setTimers(uint32_t t1Ms, uint32_t t2Ms, uint32_t t3Ms, uint32_t t4Ms = 60000);
  void setRetryLimit(uint8_t n2);
  void setMaxFrame(uint8_t maxFrame);
  void receive(const uint8_t* data, size_t len);
  void printStats() const;
  void printStatus() const;
  LinkState state() const { return state_; }
  const Address& peer() const { return peer_; }
  const Address& local() const { return config_.local; }
  const L2Stats& stats() const { return stats_; }
  bool setLocalAddress(const Address& local);

 private:
  struct QueuedInfo {
    uint8_t data[MAX_INFO_LEN]{};
    size_t len = 0;
  };
  struct WindowSlot {
    bool active = false;
    Frame frame{};
  };
  struct ReceiveSlot {
    bool active = false;
    Frame frame{};
  };

  void setState(LinkState state);
  bool transmit(const Frame& frame, bool remember);
  bool sendNextQueued();
  bool sendIFrame(const uint8_t* data, size_t len);
  void fillWindow();
  void clearWindow();
  bool windowFull() const;
  bool hasOutstanding() const;
  uint8_t outstandingCount() const;
  void retransmitWindow();
  bool retransmitOne(uint8_t nsValue);
  void storeOutstanding(const Frame& frame);
  void clearReceiveBuffer();
  bool receiveBuffered(uint8_t nsValue, Frame& out);
  bool storeReceiveBuffered(const Frame& frame);
  bool inReceiveWindow(uint8_t nsValue) const;
  void deliverIFrame(const Frame& frame);
  void deferAck();
  bool sendSupervisoryNr(SFrameType type, uint8_t nrValue, bool poll = false);
  bool sendSupervisory(SFrameType type, bool poll = false);
  bool sendUnnumbered(UFrameType type);
  void handleI(const Frame& frame);
  void handleS(const Frame& frame);
  void handleU(const Frame& frame);
  void processAck(uint8_t nrValue);
  bool nrValid(uint8_t nrValue) const;
  void sendFrmr(uint8_t rejectedControl, bool cr, uint8_t reasonBits);
  void sendXidResponse(const Frame& rxFrame);
  bool addressedToLocal(const Frame& frame) const;
  static const char* stateName(LinkState state);

  L2Config config_{};
  TxCallback tx_ = nullptr;
  DataCallback data_ = nullptr;
  void* ctx_ = nullptr;
  LinkState state_ = LinkState::Disconnected;
  Address peer_{};
  uint8_t vs_ = 0;
  uint8_t va_ = 0;
  uint8_t vr_ = 0;
  uint8_t retryCount_ = 0;
  bool peerBusy_ = false;
  static constexpr uint8_t WINDOW_SIZE = 7;
  WindowSlot window_[WINDOW_SIZE]{};
  ReceiveSlot receiveWindow_[WINDOW_SIZE]{};
  bool srejPending_[8]{};
  Timer t1_;
  Timer t2_;
  Timer t3_;
  Timer t4_;  // RNR busy-timeout
  bool  t2PendingAck_ = false;
  uint8_t maxFrame_ = WINDOW_SIZE;
  axlora::util::RingBuffer<QueuedInfo, 6> txQueue_;
  L2Stats stats_{};
};

}
