#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ax25_addr.h"
#include "ax25_frame.h"
#include "ax25_timers.h"
#include "axlora_config.h"

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
  uint8_t n2 = 5;
};

struct L2Stats {
  uint32_t uiRx = 0;
  uint32_t uiTx = 0;
  uint32_t iRx = 0;
  uint32_t iTx = 0;
  uint32_t retries = 0;
  uint32_t fcsDrops = 0;
  uint32_t stateChanges = 0;
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
  void receive(const uint8_t* data, size_t len);
  void printStats() const;
  void printStatus() const;
  LinkState state() const { return state_; }
  const Address& peer() const { return peer_; }
  const L2Stats& stats() const { return stats_; }

 private:
  void setState(LinkState state);
  bool transmit(const Frame& frame, bool remember);
  bool sendSupervisory(SFrameType type);
  bool sendUnnumbered(UFrameType type);
  void handleI(const Frame& frame);
  void handleS(const Frame& frame);
  void handleU(const Frame& frame);
  void processAck(uint8_t nrValue);
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
  Frame outstanding_{};
  bool hasOutstanding_ = false;
  Timer t1_;
  Timer t3_;
  L2Stats stats_{};
};

}
