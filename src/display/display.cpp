#include "display.h"
#include "axlora_config.h"

#ifdef AXLORA_HAS_OLED

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

namespace {

// U8X8_PIN_NONE = 255 (uint8_t wrap of -1). Encode variant RST pin safely.
static constexpr uint8_t kOledRst = static_cast<uint8_t>(
    axlora::variant::PIN_OLED_RST >= 0 ? axlora::variant::PIN_OLED_RST : 255);

// SSD1306 128×64 full-buffer hardware I2C. Change to SH1106 if you have that chip.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, kOledRst);

static constexpr uint8_t PAGE_COUNT       = 6;
static constexpr uint8_t PAGE_DWELL_TICKS = 4;  // calls to update() per page (= 4 s)
static uint8_t sPage  = 0;
static uint8_t sTicks = 0;

// Draw one text row. Right-aligns rhs if provided (non-null), left-aligns lhs.
void drawRow(uint8_t y, const char* lhs, const char* rhs = nullptr) {
  u8g2.drawStr(0, y, lhs);
  if (rhs) {
    const int w = static_cast<int>(u8g2.getStrWidth(rhs));
    u8g2.drawStr(128 - w, y, rhs);
  }
}

// Common header: callsign (left) + "MODE p/P" (right), then a divider.
void drawHeader(const axlora::display::DisplayInfo& info) {
  char modeTag[12]{};
  snprintf(modeTag, sizeof(modeTag), "%s %u/%u", info.mode, sPage + 1, PAGE_COUNT);
  drawRow(10, info.callsign, modeTag);
  u8g2.drawHLine(0, 12, 128);
}

// Format a duration in ms as a short string: "5s", "2m", "3h".
void fmtAgo(uint32_t ms, char* buf, size_t cap) {
  const uint32_t s = ms / 1000;
  if (s < 60)        snprintf(buf, cap, "%lus",  static_cast<unsigned long>(s));
  else if (s < 3600) snprintf(buf, cap, "%lum",  static_cast<unsigned long>(s / 60));
  else               snprintf(buf, cap, "%luh",  static_cast<unsigned long>(s / 3600));
}

// Format uptime from ms: "1:23:45" (h:mm:ss).
void fmtUptime(uint32_t ms, char* buf, size_t cap) {
  const uint32_t s = ms / 1000;
  snprintf(buf, cap, "%lu:%02lu:%02lu",
           static_cast<unsigned long>(s / 3600),
           static_cast<unsigned long>((s % 3600) / 60),
           static_cast<unsigned long>(s % 60));
}

// ---- Page 1: Radio status (original page) ----
void drawPageRadio(const axlora::display::DisplayInfo& info) {
  char buf[24]{};
  drawHeader(info);

  snprintf(buf, sizeof(buf), "%.1fMHz SF%u %+ddBm",
           static_cast<double>(info.freqMHz), info.sf, info.powerDbm);
  u8g2.drawStr(0, 25, buf);

  snprintf(buf, sizeof(buf), "TX:%-5lu RX:%-5lu",
           static_cast<unsigned long>(info.txCount),
           static_cast<unsigned long>(info.rxCount));
  u8g2.drawStr(0, 37, buf);

  if (info.anyConnected) {
    snprintf(buf, sizeof(buf), "CH%u:%-9s %3s",
             info.connChannel, info.connPeer, info.connState);
  } else if (!info.radioReady) {
    strncpy(buf, "radio: init...", sizeof(buf) - 1);
  } else {
    strncpy(buf, "[no connection]", sizeof(buf) - 1);
  }
  u8g2.drawStr(0, 49, buf);

  if (info.rxCount > 0) {
    snprintf(buf, sizeof(buf), "RSSI:%+.0f SNR:%+.1f",
             static_cast<double>(info.lastRssi),
             static_cast<double>(info.lastSnr));
  } else {
    strncpy(buf, "RSSI:---  SNR:---", sizeof(buf) - 1);
  }
  u8g2.drawStr(0, 61, buf);
}

// ---- Page 2: RF config + baud rates ----
void drawPageConfig(const axlora::display::DisplayInfo& info) {
  char buf[24]{};
  drawHeader(info);

  // BW and coding rate
  snprintf(buf, sizeof(buf), "BW:%.0fkHz  CR:4/%u",
           static_cast<double>(info.bandwidthKhz), info.codingRate);
  u8g2.drawStr(0, 25, buf);

  // SF and power (same as page 1 row 2 but without freq — freq is on p1)
  snprintf(buf, sizeof(buf), "SF:%u   PWR:%+d dBm", info.sf, info.powerDbm);
  u8g2.drawStr(0, 37, buf);

  // KISS baud rate
  snprintf(buf, sizeof(buf), "KISS: %7lu bps", static_cast<unsigned long>(info.baudKiss));
  u8g2.drawStr(0, 49, buf);

  // WA8DED baud rate
  snprintf(buf, sizeof(buf), "DED:  %7lu bps", static_cast<unsigned long>(info.baudDed));
  u8g2.drawStr(0, 61, buf);
}

// ---- Page 3: WA8DED host status ----
void drawPageDed(const axlora::display::DisplayInfo& info) {
  char buf[24]{};
  drawHeader(info);

  snprintf(buf, sizeof(buf), "HOST:%-3s SEL:%u",
           info.dedHostMode ? "ON" : "OFF",
           info.dedSelectedChannel);
  u8g2.drawStr(0, 25, buf);

  snprintf(buf, sizeof(buf), "MYCALL:%s", info.callsign);
  u8g2.drawStr(0, 37, buf);

  if (info.dedLastCallsign[0] != '\0') {
    snprintf(buf, sizeof(buf), "I ch%u: %s",
             info.dedLastCallsignChannel,
             info.dedLastCallsign);
  } else {
    strncpy(buf, "I ch-: ---", sizeof(buf) - 1);
  }
  u8g2.drawStr(0, 49, buf);

  if (info.dedLastConnect[0] != '\0') {
    snprintf(buf, sizeof(buf), "C ch%u: %s",
             info.dedLastConnectChannel,
             info.dedLastConnect);
  } else {
    strncpy(buf, "C ch-: ---", sizeof(buf) - 1);
  }
  u8g2.drawStr(0, 61, buf);
}

// ---- Page 4: AX.25 protocol stats ----
void drawPageStats(const axlora::display::DisplayInfo& info) {
  char buf[24]{};
  drawHeader(info);

  snprintf(buf, sizeof(buf), "RETRY:%-4lu FCS:%-4lu",
           static_cast<unsigned long>(info.l2Retries),
           static_cast<unsigned long>(info.l2FcsDrops));
  u8g2.drawStr(0, 25, buf);

  snprintf(buf, sizeof(buf), "REJ:%-5lu SREJ:%-4lu",
           static_cast<unsigned long>(info.l2RejTx),
           static_cast<unsigned long>(info.l2SrejTx));
  u8g2.drawStr(0, 37, buf);

  snprintf(buf, sizeof(buf), "FRMR:%-4lu QDROP:%-3lu",
           static_cast<unsigned long>(info.l2FrmrTx),
           static_cast<unsigned long>(info.l2QueueDrops));
  u8g2.drawStr(0, 49, buf);

  if (info.freeHeapBytes > 0) {
    snprintf(buf, sizeof(buf), "HEAP: %6lu B",
             static_cast<unsigned long>(info.freeHeapBytes));
  } else {
    strncpy(buf, "HEAP: ---", sizeof(buf) - 1);
  }
  u8g2.drawStr(0, 61, buf);
}

// ---- Page 5: Heard stations ----
void drawPageMheard(const axlora::display::DisplayInfo& info) {
  char buf[24]{};
  drawHeader(info);

  bool anyHeard = false;
  // 4 rows for up to 4 heard entries: "W1AW-1  -95 +5  5s"
  // Format: %-7s%+4.0f%+4.0f%4s  = 7+4+4+4 = 19 chars
  static constexpr uint8_t yPos[4] = {25, 37, 49, 61};
  for (uint8_t i = 0; i < axlora::display::DisplayInfo::MHEARD_COUNT; ++i) {
    const axlora::display::DisplayMheard& m = info.mheard[i];
    if (!m.active) continue;
    anyHeard = true;
    char ago[7]{};
    fmtAgo(m.agoMs, ago, sizeof(ago));
    snprintf(buf, sizeof(buf), "%-7s%+4.0f%+4.0f%4s",
             m.call,
             static_cast<double>(m.rssi),
             static_cast<double>(m.snr),
             ago);
    u8g2.drawStr(0, yPos[i], buf);
  }
  if (!anyHeard) {
    u8g2.drawStr(0, 37, "--- none heard ---");
  }
}

// ---- Page 6: Services status ----
void drawPageServices(const axlora::display::DisplayInfo& info) {
  char buf[24]{};
  drawHeader(info);

  // Digipeater
  snprintf(buf, sizeof(buf), "DIGI:%-3s  TX:%lu",
           info.digiEnabled ? "ON" : "OFF",
           static_cast<unsigned long>(info.digiTx));
  u8g2.drawStr(0, 25, buf);

  // Beacon
  if (info.beaconEnabled) {
    snprintf(buf, sizeof(buf), "BCN:ON  INT:%lus",
             static_cast<unsigned long>(info.beaconIntervalMs / 1000));
  } else {
    snprintf(buf, sizeof(buf), "BCN:OFF  TX:%lu",
             static_cast<unsigned long>(info.beaconTx));
  }
  u8g2.drawStr(0, 37, buf);

  // NET/ROM
  snprintf(buf, sizeof(buf), "NETROM:%-3s  RT:%u",
           info.netromEnabled ? "ON" : "OFF", info.netromRoutes);
  u8g2.drawStr(0, 49, buf);

  // BBS + uptime
  char up[12]{};
  fmtUptime(info.uptimeMs, up, sizeof(up));
  snprintf(buf, sizeof(buf), "BBS:%-2u  UP:%s", info.bbsMsgCount, up);
  u8g2.drawStr(0, 61, buf);
}

}  // namespace

namespace axlora::display {

void init() {
  // Power on OLED via Vext switch if the variant has one.
  if constexpr (variant::PIN_OLED_VEXT >= 0) {
    pinMode(variant::PIN_OLED_VEXT, OUTPUT);
    digitalWrite(variant::PIN_OLED_VEXT, HIGH);
    delay(50);
  }
  Wire.begin(variant::PIN_OLED_SDA, variant::PIN_OLED_SCL);
  if (!u8g2.begin()) return;  // silently skip if display not found
  u8g2.setContrast(220);

  // Splash screen
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(14, 28, "AXLoRaTNC");
  u8g2.drawStr(14, 42, "LoRa AX.25 TNC");
  u8g2.sendBuffer();
}

void update(const DisplayInfo& info) {
  // Advance page every PAGE_DWELL_TICKS calls
  if (++sTicks >= PAGE_DWELL_TICKS) {
    sTicks = 0;
    sPage  = static_cast<uint8_t>((sPage + 1) % PAGE_COUNT);
  }

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  switch (sPage) {
    case 0: drawPageRadio(info);    break;
    case 1: drawPageConfig(info);   break;
    case 2: drawPageDed(info);      break;
    case 3: drawPageStats(info);    break;
    case 4: drawPageMheard(info);   break;
    case 5: drawPageServices(info); break;
    default: drawPageRadio(info);   break;
  }

  u8g2.sendBuffer();
}

}  // namespace axlora::display

#else  // AXLORA_HAS_OLED

namespace axlora::display {
void init() {}
void update(const DisplayInfo&) {}
}

#endif  // AXLORA_HAS_OLED
