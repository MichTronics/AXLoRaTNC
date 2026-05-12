#include <Arduino.h>
#include "axlora_config.h"
#include "app/chat.h"
#include "app/console.h"
#include "mesh/relay.h"
#include "radio/radio.h"
#include "util/log.h"

namespace {

axlora::mesh::MeshNode meshNode;
axlora::app::ChatApp chat(meshNode);
axlora::app::Console console(meshNode, chat);

}

void setup() {
  Serial.begin(axlora::variant::SERIAL_BAUD);
  pinMode(axlora::variant::PIN_LED_TX, OUTPUT);
  if (axlora::variant::PIN_LED_RX >= 0) {
    pinMode(axlora::variant::PIN_LED_RX, OUTPUT);
  }
  LOG_INFO("boot AXLoRa");
  meshNode.begin(axlora::variant::DEFAULT_CALLSIGN);

  const axlora::radio::Result radioResult = axlora::radio::driver().init();
  LOG_RADIO("init result=%s", axlora::radio::resultName(radioResult));
  console.begin();
  meshNode.printInfo();
}

void loop() {
  console.loop();
  meshNode.loop();
  taskYIELD();
}
