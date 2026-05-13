#pragma once

#include <stdint.h>

namespace axlora::variant {

// ESP32 DevKit V1 + EBYTE E22-900M30S/M33S SX1262 wiring.
static constexpr int PIN_SPI_SCK = 18;
static constexpr int PIN_SPI_MISO = 19;
static constexpr int PIN_SPI_MOSI = 23;
static constexpr int PIN_RADIO_CS = 5;
static constexpr int PIN_RADIO_DIO1 = 33;
static constexpr int PIN_RADIO_BUSY = 32;
static constexpr int PIN_RADIO_RST = 25;
static constexpr int PIN_RADIO_DIO2 = -1;
static constexpr int PIN_RADIO_RXEN = 14;
static constexpr int PIN_RADIO_TXEN = 13;

static constexpr int PIN_LED_TX = 2;
static constexpr int PIN_LED_RX = -1;
static constexpr int PIN_BATTERY_ADC = 35;

static constexpr int PIN_OLED_SDA  = 21;   // Standard ESP32 I2C — adjust if needed
static constexpr int PIN_OLED_SCL  = 22;
static constexpr int PIN_OLED_RST  = -1;   // Most SSD1306 breakouts have no reset pin
static constexpr int PIN_OLED_VEXT = -1;   // No power switch on bare breakout
static constexpr int PIN_GPS_RX = -1;
static constexpr int PIN_GPS_TX = -1;
static constexpr int PIN_DS18B20_ONEWIRE = 27;
static constexpr int PIN_AM2302_DATA = 26;

}
