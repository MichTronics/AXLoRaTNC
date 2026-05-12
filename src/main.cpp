#include <Arduino.h>
#include "axlora_config.h"
#include "app/chat.h"
#include "app/console.h"
#include "mesh/relay.h"
#include "radio/radio.h"
#include "util/log.h"
#include "util/timer.h"

namespace {

axlora::mesh::MeshNode meshNode;
axlora::app::ChatApp chat(meshNode);
axlora::app::Console console(meshNode, chat);
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
  LOG_INFO("boot AXLoRa");
  meshNode.begin(axlora::variant::DEFAULT_CALLSIGN);

  serviceRadioInit(true);
  console.begin();
  meshNode.printInfo();
}

void loop() {
  console.loop();
  serviceRadioInit(false);
  if (radioReady) {
    meshNode.loop();
  }
  taskYIELD();
}
