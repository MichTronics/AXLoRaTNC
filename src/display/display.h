#pragma once

#include <stdint.h>

namespace axlora::display {

// Single entry in the mheard table passed to the display.
struct DisplayMheard {
  bool     active = false;
  char     call[10]{};
  float    rssi   = 0.0f;
  float    snr    = 0.0f;
  uint32_t agoMs  = 0;   // millis since last heard
};

// Data snapshot filled by the TNC each display update cycle.
struct DisplayInfo {
  // ---- page 1: radio status (existing) ----
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

  // ---- page 2: RF config + baud rates ----
  float    bandwidthKhz = 125.0f;
  uint8_t  codingRate   = 5;
  uint32_t baudKiss     = 9600;
  uint32_t baudDed      = 9600;
  bool     dedHostMode  = false;
  uint8_t  dedSelectedChannel = 0;
  uint8_t  dedLastCallsignChannel = 0;
  char     dedLastCallsign[12]{};
  uint8_t  dedLastConnectChannel = 0;
  char     dedLastConnect[12]{};

  // ---- page 3: AX.25 protocol stats (aggregated across channels) ----
  uint32_t l2Retries    = 0;
  uint32_t l2FcsDrops   = 0;
  uint32_t l2RejTx      = 0;
  uint32_t l2SrejTx     = 0;
  uint32_t l2FrmrTx     = 0;
  uint32_t l2QueueDrops = 0;
  uint32_t freeHeapBytes = 0;

  // ---- page 4: heard stations ----
  static constexpr uint8_t MHEARD_COUNT = 4;
  DisplayMheard mheard[MHEARD_COUNT]{};

  // ---- page 5: services ----
  bool     digiEnabled      = false;
  uint32_t digiTx           = 0;
  bool     beaconEnabled    = false;
  uint32_t beaconTx         = 0;
  uint32_t beaconIntervalMs = 0;
  bool     netromEnabled    = false;
  uint8_t  netromRoutes     = 0;
  uint8_t  bbsMsgCount      = 0;
  uint32_t uptimeMs         = 0;
};

// Call once during setup(). No-op when compiled without HAS_OLED.
void init();

// Call periodically from the main loop with a fresh DisplayInfo snapshot.
void update(const DisplayInfo& info);

}  // namespace axlora::display
