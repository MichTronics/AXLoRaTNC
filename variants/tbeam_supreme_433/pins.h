#pragma once

namespace axlora::variant {

// LILYGO T-Beam SUPREME V3.0 — ESP32-S3 + SX1262, 433 MHz variant.
// Pinout follows the official LILYGO T-Beam SUPREME pin overview.
static constexpr int PIN_SPI_SCK = 12;
static constexpr int PIN_SPI_MISO = 13;
static constexpr int PIN_SPI_MOSI = 11;
static constexpr int PIN_RADIO_CS = 10;
static constexpr int PIN_RADIO_DIO1 = 1;
static constexpr int PIN_RADIO_BUSY = 4;
static constexpr int PIN_RADIO_RST = 5;
static constexpr int PIN_RADIO_DIO2 = -1;
static constexpr int PIN_RADIO_RXEN = -1;
static constexpr int PIN_RADIO_TXEN = -1;

static constexpr int PIN_LED_TX = -1;
static constexpr int PIN_LED_RX = -1;
static constexpr int PIN_BATTERY_ADC = -1;

static constexpr int PIN_OLED_SDA  = 17;
static constexpr int PIN_OLED_SCL  = 18;
static constexpr int PIN_OLED_RST  = -1;
static constexpr int PIN_OLED_VEXT = -1;
static constexpr int PIN_GPS_RX = 8;   // ESP32-S3 RX, GNSS TX
static constexpr int PIN_GPS_TX = 9;   // ESP32-S3 TX, GNSS RX
static constexpr int PIN_DS18B20_ONEWIRE = -1;
static constexpr int PIN_AM2302_DATA = -1;

// UART hardware flow control (-1 = disabled)
static constexpr int PIN_UART_RTS = -1;
static constexpr int PIN_UART_CTS = -1;

}
