#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ax25_addr.h"
#include "axlora_config.h"

namespace axlora::ax25 {

static constexpr uint8_t PID_NO_LAYER3 = 0xF0;
static constexpr uint16_t MAX_INFO_LEN = 256;
static constexpr uint8_t CTRL_UI = 0x03;
static constexpr uint8_t CTRL_SABM = 0x2F;
static constexpr uint8_t CTRL_DISC = 0x43;
static constexpr uint8_t CTRL_DM = 0x0F;
static constexpr uint8_t CTRL_UA = 0x63;

enum class FrameKind : uint8_t {
  I,
  S,
  U,
  Unknown,
};

enum class SFrameType : uint8_t {
  RR = 0,
  RNR = 1,
  REJ = 2,
};

enum class UFrameType : uint8_t {
  SABM,
  UA,
  DISC,
  DM,
  UI,
  Unknown,
};

struct Frame {
  Address destination{};
  Address source{};
  Address repeaters[MAX_REPEATERS]{};
  uint8_t repeaterCount = 0;
  uint8_t control = CTRL_UI;
  uint8_t pid = PID_NO_LAYER3;
  uint8_t info[MAX_INFO_LEN]{};
  size_t infoLen = 0;
};

FrameKind kind(uint8_t control);
uint8_t makeI(uint8_t ns, uint8_t nr, bool poll);
uint8_t makeS(SFrameType type, uint8_t nr, bool poll);
uint8_t makeU(UFrameType type, bool poll);
uint8_t ns(uint8_t control);
uint8_t nr(uint8_t control);
SFrameType sType(uint8_t control);
UFrameType uType(uint8_t control);
bool encodeFrame(const Frame& frame, uint8_t* out, size_t outCap, size_t& outLen, bool includeFcs);
bool decodeFrame(const uint8_t* data, size_t len, Frame& out, bool expectFcs);

}
