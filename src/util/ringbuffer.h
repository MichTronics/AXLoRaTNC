#pragma once

#include <stddef.h>
#include <stdint.h>

namespace axlora::util {

template <typename T, size_t Capacity>
class RingBuffer {
 public:
  bool push(const T& item) {
    if (full()) {
      return false;
    }
    items_[head_] = item;
    head_ = (head_ + 1) % Capacity;
    ++count_;
    return true;
  }

  bool pop(T& out) {
    if (empty()) {
      return false;
    }
    out = items_[tail_];
    tail_ = (tail_ + 1) % Capacity;
    --count_;
    return true;
  }

  bool peek(T& out) const {
    if (empty()) {
      return false;
    }
    out = items_[tail_];
    return true;
  }

  bool empty() const { return count_ == 0; }
  bool full() const { return count_ == Capacity; }
  size_t size() const { return count_; }
  size_t free() const { return Capacity - count_; }
  void clear() { head_ = 0; tail_ = 0; count_ = 0; }

 private:
  T items_[Capacity]{};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
};

}

