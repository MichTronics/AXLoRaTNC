#pragma once

#include <stdint.h>

namespace axlora::display {

// Data snapshot filled by the TNC each display update cycle.
struct DisplayInfo {
  char     callsign[12]{};     // local callsign (formatted)
  char     mode[5]{};          // "CONS", "KISS", "DED "
  float    freqMHz    = 0.0f;
  uint8_t  sf         = 7;
  int8_t   powerDbm   = 14;
  uint32_t txCount    = 0;     // radio::stats().txOk
  uint32_t rxCount    = 0;     // radio::stats().rxOk
  float    lastRssi   = 0.0f;
  float    lastSnr    = 0.0f;
  bool     radioReady = false;
  bool     anyConnected  = false;
  uint8_t  connChannel   = 0;  // 1-based; 0 = no active channel
  char     connPeer[12]{};     // peer callsign of first active channel
  char     connState[4]{};     // "OK", "CON", "DIS", "REC"
};

// Call once during setup(). No-op when compiled without HAS_OLED.
void init();

// Call periodically from the main loop with a fresh DisplayInfo snapshot.
void update(const DisplayInfo& info);

}  // namespace axlora::display
