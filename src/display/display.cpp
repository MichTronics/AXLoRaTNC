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

// Draw one text row. Right-aligns rhs if provided (non-null), left-aligns lhs.
void drawRow(uint8_t y, const char* lhs, const char* rhs = nullptr) {
  u8g2.drawStr(0, y, lhs);
  if (rhs) {
    const int w = static_cast<int>(u8g2.getStrWidth(rhs));
    u8g2.drawStr(128 - w, y, rhs);
  }
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
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);  // 6px wide, 10px tall, 12px line spacing

  char buf[24]{};

  // Row 1: callsign (left) + serial mode (right)
  drawRow(10, info.callsign, info.mode);
  u8g2.drawHLine(0, 12, 128);

  // Row 2: radio config
  snprintf(buf, sizeof(buf), "%.1fMHz SF%u %+ddBm",
           static_cast<double>(info.freqMHz), info.sf, info.powerDbm);
  u8g2.drawStr(0, 25, buf);

  // Row 3: TX / RX frame counters
  snprintf(buf, sizeof(buf), "TX:%-5lu RX:%-5lu",
           static_cast<unsigned long>(info.txCount),
           static_cast<unsigned long>(info.rxCount));
  u8g2.drawStr(0, 37, buf);

  // Row 4: first active channel, or idle indicator
  if (info.anyConnected) {
    snprintf(buf, sizeof(buf), "CH%u:%-9s %3s",
             info.connChannel, info.connPeer, info.connState);
  } else if (!info.radioReady) {
    strncpy(buf, "radio: init...", sizeof(buf) - 1);
  } else {
    strncpy(buf, "[no connection]", sizeof(buf) - 1);
  }
  u8g2.drawStr(0, 49, buf);

  // Row 5: RSSI / SNR of last received packet
  if (info.rxCount > 0) {
    snprintf(buf, sizeof(buf), "RSSI:%+.0f  SNR:%+.1f",
             static_cast<double>(info.lastRssi),
             static_cast<double>(info.lastSnr));
  } else {
    strncpy(buf, "RSSI:---  SNR:---", sizeof(buf) - 1);
  }
  u8g2.drawStr(0, 61, buf);

  u8g2.sendBuffer();
}

}  // namespace axlora::display

#else  // AXLORA_HAS_OLED

namespace axlora::display {
void init() {}
void update(const DisplayInfo&) {}
}

#endif  // AXLORA_HAS_OLED
