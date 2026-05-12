#include <Arduino.h>
#include "axlora_config.h"
#include "radio/radio.h"
#include "tnc/tnc.h"
#include "util/log.h"
#include "util/timer.h"

namespace {

axlora::tnc::Tnc tnc;
bool radioReady = false;
uint32_t lastRadioInitMs = 0;
static constexpr uint32_t RADIO_INIT_RETRY_MS = 5000;

void serviceRadioInit(bool force) {
  const uint32_t now = axlora::util::nowMs();
  if (radioReady || (!force && !axlora::util::elapsed(now, lastRadioInitMs, RADIO_INIT_RETRY_MS))) {
    return;
  }

  lastRadioInitMs = now;
  const axlora::radio::Result radioResult = axlora::radio::driver().init();
  radioReady = radioResult == axlora::radio::Result::Ok;
  LOG_RADIO("init result=%s", axlora::radio::resultName(radioResult));
}

}

void setup() {
  Serial.begin(axlora::variant::SERIAL_BAUD);
  pinMode(axlora::variant::PIN_LED_TX, OUTPUT);
  if (axlora::variant::PIN_LED_RX >= 0) {
    pinMode(axlora::variant::PIN_LED_RX, OUTPUT);
  }
  tnc.begin(axlora::variant::DEFAULT_CALLSIGN);
  LOG_INFO("boot AXLoRaTNC");

  serviceRadioInit(true);
  if (!tnc.quietSerialMode()) {
    Serial.println("AXLoRaTNC commands: help, info, mode, mode console|kiss|ded, radio, ax25, connect <CALLSIGN-SSID>, disconnect, sendui <DEST> <message>, send <message>, stats");
    tnc.printInfo();
  }
}

void loop() {
  serviceRadioInit(false);
  tnc.loop(radioReady);
  taskYIELD();
}
