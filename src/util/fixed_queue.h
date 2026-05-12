#pragma once

#include "ringbuffer.h"

namespace axlora::util {

template <typename T, size_t Capacity>
using FixedQueue = RingBuffer<T, Capacity>;

}
