#pragma once

namespace axlora::variant {

static constexpr int PIN_SPI_SCK = 9;
static constexpr int PIN_SPI_MISO = 11;
static constexpr int PIN_SPI_MOSI = 10;
static constexpr int PIN_RADIO_CS = 8;
static constexpr int PIN_RADIO_DIO1 = 14;
static constexpr int PIN_RADIO_BUSY = 13;
static constexpr int PIN_RADIO_RST = 12;
static constexpr int PIN_RADIO_DIO2 = -1;
static constexpr int PIN_RADIO_RXEN = -1;
static constexpr int PIN_RADIO_TXEN = -1;

static constexpr int PIN_LED_TX = 35;
static constexpr int PIN_LED_RX = -1;
static constexpr int PIN_BATTERY_ADC = 1;

static constexpr int PIN_OLED_SDA  = 17;
static constexpr int PIN_OLED_SCL  = 18;
static constexpr int PIN_OLED_RST  = 21;   // Heltec V3 OLED reset
static constexpr int PIN_OLED_VEXT = 36;   // TPS22860 Vext switch — HIGH = on
static constexpr int PIN_GPS_RX = -1;
static constexpr int PIN_GPS_TX = -1;
static constexpr int PIN_DS18B20_ONEWIRE = -1;
static constexpr int PIN_AM2302_DATA = -1;

}
