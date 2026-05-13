#pragma once

#include <stdint.h>

namespace axlora::variant {

// LilyGo TTGO T3 v1.6.1 — ESP32 + SX1276
static constexpr int PIN_SPI_SCK  = 5;
static constexpr int PIN_SPI_MISO = 19;
static constexpr int PIN_SPI_MOSI = 27;
static constexpr int PIN_RADIO_CS   = 18;
static constexpr int PIN_RADIO_DIO1 = 26;   // SX1276 DIO0 (IRQ)
static constexpr int PIN_RADIO_BUSY = -1;    // SX1276 has no BUSY pin
static constexpr int PIN_RADIO_RST  = 14;
static constexpr int PIN_RADIO_DIO2 = -1;
static constexpr int PIN_RADIO_RXEN = -1;
static constexpr int PIN_RADIO_TXEN = -1;

static constexpr int PIN_LED_TX      = 25;   // Onboard blue LED
static constexpr int PIN_LED_RX      = -1;
static constexpr int PIN_BATTERY_ADC = 35;   // 1:2 voltage divider to BAT+

static constexpr int PIN_OLED_SDA  = 21;
static constexpr int PIN_OLED_SCL  = 22;
static constexpr int PIN_OLED_RST  = 16;
static constexpr int PIN_OLED_VEXT = -1;
static constexpr int PIN_GPS_RX = -1;
static constexpr int PIN_GPS_TX = -1;
static constexpr int PIN_DS18B20_ONEWIRE = -1;
static constexpr int PIN_AM2302_DATA     = -1;

}
