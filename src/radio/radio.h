#pragma once

#include <stddef.h>
#include <stdint.h>
#include "axlora_config.h"

namespace axlora::radio {

enum class Result : int8_t {
  Ok = 0,
  Busy = 1,
  NoPacket = 2,
  Invalid = -1,
  HardwareError = -2,
  TooLarge = -3,
};

struct RxPacket {
  uint8_t data[MAX_PACKET_BYTES]{};
  size_t len = 0;
  float rssi = 0.0f;
  float snr = 0.0f;
};

struct Stats {
  uint32_t txOk = 0;
  uint32_t txFail = 0;
  uint32_t rxOk = 0;
  uint32_t rxFail = 0;
  uint32_t dutyDrops = 0;
};

class Driver {
 public:
  virtual ~Driver() = default;
  virtual Result init() = 0;
  virtual Result send(const uint8_t* data, size_t len) = 0;
  virtual Result receive(RxPacket& packet) = 0;
  virtual Result setFrequency(float frequencyMHz) = 0;
  virtual Result setPower(int8_t powerDbm) = 0;
  virtual float getRSSI() = 0;
  virtual float getSNR() = 0;
  virtual void sleep() = 0;
  virtual void standby() = 0;
};

Driver& driver();
Stats& stats();
const char* resultName(Result result);

}

