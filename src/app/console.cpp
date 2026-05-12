#include "console.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "radio/radio.h"
#include "util/log.h"

namespace axlora::app {

void Console::begin() {
  printHelp();
}

void Console::loop() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      line_[pos_] = '\0';
      handleLine(line_);
      pos_ = 0;
      continue;
    }
    if (pos_ < sizeof(line_) - 1) {
      line_[pos_++] = c;
    }
  }
}

void Console::handleLine(char* line) {
  char* cmd = strtok(line, " ");
  if (cmd == nullptr) {
    return;
  }
  if (strcmp(cmd, "help") == 0) {
    printHelp();
  } else if (strcmp(cmd, "info") == 0) {
    mesh_.printInfo();
  } else if (strcmp(cmd, "neighbors") == 0) {
    mesh_.printNeighbors();
  } else if (strcmp(cmd, "stats") == 0) {
    mesh_.printStats();
  } else if (strcmp(cmd, "radio") == 0) {
    Serial.printf("RSSI=%.1f SNR=%.1f\n",
                  static_cast<double>(radio::driver().getRSSI()),
                  static_cast<double>(radio::driver().getSNR()));
  } else if (strcmp(cmd, "setfreq") == 0) {
    char* value = strtok(nullptr, " ");
    if (value == nullptr) {
      Serial.println("usage: setfreq <mhz>");
      return;
    }
    const float mhz = static_cast<float>(atof(value));
    LOG_RADIO("set frequency %.3f MHz -> %s", static_cast<double>(mhz),
              radio::resultName(radio::driver().setFrequency(mhz)));
  } else if (strcmp(cmd, "setpower") == 0) {
    char* value = strtok(nullptr, " ");
    if (value == nullptr) {
      Serial.println("usage: setpower <dbm>");
      return;
    }
    const int power = atoi(value);
    LOG_RADIO("set power %d dBm -> %s", power,
              radio::resultName(radio::driver().setPower(static_cast<int8_t>(power))));
  } else if (strcmp(cmd, "send") == 0) {
    char* destination = strtok(nullptr, " ");
    char* message = strtok(nullptr, "");
    if (destination == nullptr || message == nullptr) {
      Serial.println("usage: send <callsign> <message>");
      return;
    }
    Serial.println(chat_.send(destination, message) ? "queued" : "send failed");
  } else {
    Serial.println("unknown command");
  }
}

void Console::printHelp() const {
  Serial.println("AXLoRa commands: help, info, neighbors, send <callsign> <message>, stats, radio, setfreq <mhz>, setpower <dbm>");
}

}

