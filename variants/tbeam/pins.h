#pragma once

namespace axlora::variant {

static constexpr int PIN_SPI_SCK = 5;
static constexpr int PIN_SPI_MISO = 19;
static constexpr int PIN_SPI_MOSI = 27;
static constexpr int PIN_RADIO_CS = 18;
static constexpr int PIN_RADIO_DIO1 = 26;
static constexpr int PIN_RADIO_BUSY = -1;
static constexpr int PIN_RADIO_RST = 23;
static constexpr int PIN_RADIO_DIO2 = -1;
static constexpr int PIN_RADIO_RXEN = -1;
static constexpr int PIN_RADIO_TXEN = -1;

static constexpr int PIN_LED_TX = 4;
static constexpr int PIN_LED_RX = -1;
static constexpr int PIN_BATTERY_ADC = 35;

static constexpr int PIN_OLED_SDA  = 21;
static constexpr int PIN_OLED_SCL  = 22;
static constexpr int PIN_OLED_RST  = -1;
static constexpr int PIN_OLED_VEXT = -1;
static constexpr int PIN_GPS_RX = 34;
static constexpr int PIN_GPS_TX = 12;
static constexpr int PIN_DS18B20_ONEWIRE = -1;
static constexpr int PIN_AM2302_DATA = -1;

}
