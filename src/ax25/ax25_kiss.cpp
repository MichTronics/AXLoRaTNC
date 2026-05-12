#include "ax25_kiss.h"
#include <string.h>

namespace axlora::ax25 {

void KissDecoder::reset() {
  inFrame_ = false;
  escaped_ = false;
  len_ = 0;
}

bool KissDecoder::feed(uint8_t byte, KissFrame& out) {
  if (byte == KISS_FEND) {
    if (inFrame_ && len_ > 0) {
      out = KissFrame{};
      out.port = static_cast<uint8_t>((buffer_[0] >> 4) & 0x0F);
      out.command = static_cast<KissCommand>(buffer_[0] & 0x0F);
      out.len = len_ - 1;
      memcpy(out.data, &buffer_[1], out.len);
      reset();
      inFrame_ = true;
      return true;
    }
    inFrame_ = true;
    escaped_ = false;
    len_ = 0;
    return false;
  }
  if (!inFrame_) {
    return false;
  }
  if (escaped_) {
    if (byte == KISS_TFEND) {
      byte = KISS_FEND;
    } else if (byte == KISS_TFESC) {
      byte = KISS_FESC;
    }
    escaped_ = false;
  } else if (byte == KISS_FESC) {
    escaped_ = true;
    return false;
  }
  if (len_ < sizeof(buffer_)) {
    buffer_[len_++] = byte;
  } else {
    reset();
  }
  return false;
}

bool encodeKiss(const KissFrame& frame, uint8_t* out, size_t outCap, size_t& outLen) {
  if (out == nullptr || outCap < 4) {
    return false;
  }
  size_t p = 0;
  out[p++] = KISS_FEND;
  out[p++] = static_cast<uint8_t>((frame.port << 4) | (static_cast<uint8_t>(frame.command) & 0x0F));
  for (size_t i = 0; i < frame.len; ++i) {
    const uint8_t b = frame.data[i];
    if (b == KISS_FEND || b == KISS_FESC) {
      if (p + 2 >= outCap) {
        return false;
      }
      out[p++] = KISS_FESC;
      out[p++] = b == KISS_FEND ? KISS_TFEND : KISS_TFESC;
    } else {
      if (p + 1 >= outCap) {
        return false;
      }
      out[p++] = b;
    }
  }
  if (p >= outCap) {
    return false;
  }
  out[p++] = KISS_FEND;
  outLen = p;
  return true;
}

}
