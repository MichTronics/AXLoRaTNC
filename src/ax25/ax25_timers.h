#pragma once

#include <stdint.h>
#include "util/timer.h"

namespace axlora::ax25 {

class Timer {
 public:
  void start(uint32_t intervalMs) {
    intervalMs_ = intervalMs;
    startedMs_ = axlora::util::nowMs();
    running_ = true;
  }
  void stop() { running_ = false; }
  bool running() const { return running_; }
  bool expired() const {
    return running_ && axlora::util::elapsed(axlora::util::nowMs(), startedMs_, intervalMs_);
  }

 private:
  bool running_ = false;
  uint32_t startedMs_ = 0;
  uint32_t intervalMs_ = 0;
};

}
