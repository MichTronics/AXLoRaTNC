#pragma once

#if !defined(AXLORA_VARIANT_DEVKITV1_E22) && !defined(AXLORA_VARIANT_HELTEC_V3) && !defined(AXLORA_VARIANT_TBEAM)
#error "Select an AXLoRa hardware variant with a build flag, for example -DAXLORA_VARIANT_DEVKITV1_E22"
#endif

#include "variant.h"

namespace axlora {

static constexpr uint16_t MAX_PACKET_BYTES = 330;

}
