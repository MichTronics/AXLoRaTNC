#pragma once

#include <stddef.h>
#include <stdint.h>
#include "axlora_config.h"

namespace axlora::ax25 {

static constexpr uint8_t KISS_FEND = 0xC0;
static constexpr uint8_t KISS_FESC = 0xDB;
static constexpr uint8_t KISS_TFEND = 0xDC;
static constexpr uint8_t KISS_TFESC = 0xDD;

enum class KissCommand : uint8_t {
  Data = 0x00,
  TxDelay = 0x01,
  Persistence = 0x02,
  SlotTime = 0x03,
  FullDuplex = 0x05,
};

struct KissFrame {
  uint8_t port = 0;
  KissCommand command = KissCommand::Data;
  uint8_t data[MAX_PACKET_BYTES]{};
  size_t len = 0;
};

class KissDecoder {
 public:
  bool feed(uint8_t byte, KissFrame& out);
  void reset();

 private:
  bool inFrame_ = false;
  bool escaped_ = false;
  uint8_t buffer_[MAX_PACKET_BYTES + 1]{};
  size_t len_ = 0;
};

bool encodeKiss(const KissFrame& frame, uint8_t* out, size_t outCap, size_t& outLen);

}
