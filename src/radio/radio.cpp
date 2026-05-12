#include "radio.h"

namespace axlora::radio {

Stats& stats() {
  static Stats instance;
  return instance;
}

const char* resultName(Result result) {
  switch (result) {
    case Result::Ok: return "OK";
    case Result::Busy: return "BUSY";
    case Result::NoPacket: return "NO_PACKET";
    case Result::Invalid: return "INVALID";
    case Result::HardwareError: return "HW_ERROR";
    case Result::TooLarge: return "TOO_LARGE";
    default: return "UNKNOWN";
  }
}

}

