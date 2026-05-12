#include "ax25_frame.h"
#include <string.h>
#include "ax25_fcs.h"

namespace axlora::ax25 {

FrameKind kind(uint8_t control) {
  if ((control & 0x01) == 0) {
    return FrameKind::I;
  }
  if ((control & 0x03) == 0x01) {
    return FrameKind::S;
  }
  if ((control & 0x03) == 0x03) {
    return FrameKind::U;
  }
  return FrameKind::Unknown;
}

uint8_t makeI(uint8_t nsValue, uint8_t nrValue, bool poll) {
  return static_cast<uint8_t>(((nrValue & 0x07) << 5) | (poll ? 0x10 : 0x00) | ((nsValue & 0x07) << 1));
}

uint8_t makeS(SFrameType type, uint8_t nrValue, bool poll) {
  return static_cast<uint8_t>(((nrValue & 0x07) << 5) | (poll ? 0x10 : 0x00) |
                              ((static_cast<uint8_t>(type) & 0x03) << 2) | 0x01);
}

uint8_t makeU(UFrameType type, bool poll) {
  uint8_t base = CTRL_DM;
  switch (type) {
    case UFrameType::SABM: base = CTRL_SABM; break;
    case UFrameType::UA: base = CTRL_UA; break;
    case UFrameType::DISC: base = CTRL_DISC; break;
    case UFrameType::DM: base = CTRL_DM; break;
    case UFrameType::UI: base = CTRL_UI; break;
    default: break;
  }
  if (!poll) {
    base &= static_cast<uint8_t>(~0x10);
  }
  return base;
}

uint8_t ns(uint8_t control) { return static_cast<uint8_t>((control >> 1) & 0x07); }
uint8_t nr(uint8_t control) { return static_cast<uint8_t>((control >> 5) & 0x07); }
SFrameType sType(uint8_t control) { return static_cast<SFrameType>((control >> 2) & 0x03); }

UFrameType uType(uint8_t control) {
  switch (control & static_cast<uint8_t>(~0x10)) {
    case CTRL_SABM & static_cast<uint8_t>(~0x10): return UFrameType::SABM;
    case CTRL_UA & static_cast<uint8_t>(~0x10): return UFrameType::UA;
    case CTRL_DISC & static_cast<uint8_t>(~0x10): return UFrameType::DISC;
    case CTRL_DM & static_cast<uint8_t>(~0x10): return UFrameType::DM;
    case CTRL_UI & static_cast<uint8_t>(~0x10): return UFrameType::UI;
    default: return UFrameType::Unknown;
  }
}

bool encodeFrame(const Frame& frame, uint8_t* out, size_t outCap, size_t& outLen, bool includeFcs) {
  if (out == nullptr || outCap < 16 || frame.repeaterCount > MAX_REPEATERS || frame.infoLen > MAX_INFO_LEN) {
    return false;
  }
  uint8_t raw[MAX_PACKET_BYTES]{};
  size_t p = 0;
  if (!encodeAddress(frame.destination, false, &raw[p])) {
    return false;
  }
  p += 7;
  if (!encodeAddress(frame.source, frame.repeaterCount == 0, &raw[p])) {
    return false;
  }
  p += 7;
  for (uint8_t i = 0; i < frame.repeaterCount; ++i) {
    if (!encodeAddress(frame.repeaters[i], i == frame.repeaterCount - 1, &raw[p])) {
      return false;
    }
    p += 7;
  }
  raw[p++] = frame.control;
  if (kind(frame.control) == FrameKind::I || uType(frame.control) == UFrameType::UI) {
    raw[p++] = frame.pid;
  }
  if (p + frame.infoLen + (includeFcs ? 2 : 0) > outCap || p + frame.infoLen > sizeof(raw)) {
    return false;
  }
  memcpy(&raw[p], frame.info, frame.infoLen);
  p += frame.infoLen;
  if (includeFcs) {
    return appendFcs(raw, p, out, outCap, outLen);
  }
  memcpy(out, raw, p);
  outLen = p;
  return true;
}

bool decodeFrame(const uint8_t* data, size_t len, Frame& out, bool expectFcs) {
  if (data == nullptr || len < 16 || (expectFcs && !checkFcs(data, len))) {
    return false;
  }
  const size_t frameLen = expectFcs ? len - 2 : len;
  out = Frame{};
  size_t p = 0;
  bool last = false;
  if (!decodeAddress(&data[p], out.destination, last)) {
    return false;
  }
  p += 7;
  if (!decodeAddress(&data[p], out.source, last)) {
    return false;
  }
  p += 7;
  while (!last && out.repeaterCount < MAX_REPEATERS && p + 7 <= frameLen) {
    if (!decodeAddress(&data[p], out.repeaters[out.repeaterCount], last)) {
      return false;
    }
    ++out.repeaterCount;
    p += 7;
  }
  if (!last || p >= frameLen) {
    return false;
  }
  out.control = data[p++];
  if (kind(out.control) == FrameKind::I || uType(out.control) == UFrameType::UI) {
    if (p >= frameLen) {
      return false;
    }
    out.pid = data[p++];
  }
  out.infoLen = frameLen - p;
  if (out.infoLen > MAX_INFO_LEN) {
    return false;
  }
  memcpy(out.info, &data[p], out.infoLen);
  return true;
}

}
