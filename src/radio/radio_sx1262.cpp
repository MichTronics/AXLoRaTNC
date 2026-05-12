#include "radio.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "util/timer.h"
#include "util/log.h"

#if defined(AXLORA_VARIANT_DEVKITV1_E22) || defined(AXLORA_VARIANT_HELTEC_V3)

namespace axlora::radio {
namespace {

constexpr uint32_t radioLibPin(int pin) {
  return pin >= 0 ? static_cast<uint32_t>(pin) : RADIOLIB_NC;
}

class Sx1262Driver final : public Driver {
 public:
  Result init() override {
    SPI.begin(variant::PIN_SPI_SCK, variant::PIN_SPI_MISO, variant::PIN_SPI_MOSI, variant::PIN_RADIO_CS);
    const int16_t state = radio_.begin(variant::DEFAULT_FREQUENCY_MHZ,
                                       variant::DEFAULT_BANDWIDTH_KHZ,
                                       variant::DEFAULT_SPREADING_FACTOR,
                                       variant::DEFAULT_CODING_RATE,
                                       variant::DEFAULT_SYNC_WORD,
                                       variant::DEFAULT_TX_POWER_DBM,
                                       variant::PREAMBLE_LENGTH,
                                       variant::TCXO_VOLTAGE);
    if (state != RADIOLIB_ERR_NONE) {
      LOG_ERR("SX1262 init failed: %d", state);
      return Result::HardwareError;
    }
    radio_.setDio2AsRfSwitch(variant::USE_DIO2_RF_SWITCH);
    if constexpr (variant::HAS_EXTERNAL_RF_SWITCH) {
      radio_.setRfSwitchPins(radioLibPin(variant::PIN_RADIO_RXEN), radioLibPin(variant::PIN_RADIO_TXEN));
    }
    radio_.setCRC(true);
    radio_.startReceive();
    return Result::Ok;
  }

  Result send(const uint8_t* data, size_t len) override {
    if (data == nullptr || len == 0) {
      return Result::Invalid;
    }
    if (len > MAX_PACKET_BYTES) {
      return Result::TooLarge;
    }
    const uint32_t now = util::nowMs();
    if (!canTransmit(now)) {
      ++stats().dutyDrops;
      return Result::Busy;
    }
    const int16_t state = radio_.transmit(const_cast<uint8_t*>(data), len);
    radio_.startReceive();
    if (state == RADIOLIB_ERR_NONE) {
      lastTxMs_ = now;
      lastAirTimeMs_ = estimateAirtimeMs(len);
      ++stats().txOk;
      return Result::Ok;
    }
    ++stats().txFail;
    return Result::HardwareError;
  }

  Result receive(RxPacket& packet) override {
    packet.len = radio_.getPacketLength();
    if (packet.len == 0 || packet.len > sizeof(packet.data)) {
      return Result::NoPacket;
    }
    const int16_t state = radio_.readData(packet.data, packet.len);
    if (state == RADIOLIB_ERR_NONE) {
      packet.rssi = radio_.getRSSI();
      packet.snr = radio_.getSNR();
      ++stats().rxOk;
      radio_.startReceive();
      return Result::Ok;
    }
    if (state == RADIOLIB_ERR_RX_TIMEOUT || state == RADIOLIB_ERR_CRC_MISMATCH) {
      if (state == RADIOLIB_ERR_CRC_MISMATCH) {
        ++stats().rxFail;
      }
      return Result::NoPacket;
    }
    return Result::NoPacket;
  }

  Result setFrequency(float frequencyMHz) override {
    return radio_.setFrequency(frequencyMHz) == RADIOLIB_ERR_NONE ? Result::Ok : Result::HardwareError;
  }

  Result setPower(int8_t powerDbm) override {
    if (powerDbm > variant::MAX_TX_POWER_DBM) {
      powerDbm = variant::MAX_TX_POWER_DBM;
    }
    return radio_.setOutputPower(powerDbm) == RADIOLIB_ERR_NONE ? Result::Ok : Result::HardwareError;
  }

  float getRSSI() override { return radio_.getRSSI(); }
  float getSNR() override { return radio_.getSNR(); }
  void sleep() override { radio_.sleep(); }
  void standby() override { radio_.standby(); }

 private:
  uint32_t estimateAirtimeMs(size_t len) const {
    return static_cast<uint32_t>(500 + (len * 12));
  }

  bool canTransmit(uint32_t now) const {
    if (lastTxMs_ == 0) {
      return true;
    }
    const uint32_t minGap = (lastAirTimeMs_ * 1000000UL) / variant::DUTY_CYCLE_PPM;
    return util::elapsed(now, lastTxMs_, minGap);
  }

  Module module_{radioLibPin(variant::PIN_RADIO_CS),
                 radioLibPin(variant::PIN_RADIO_DIO1),
                 radioLibPin(variant::PIN_RADIO_RST),
                 radioLibPin(variant::PIN_RADIO_BUSY)};
  SX1262 radio_{&module_};
  uint32_t lastTxMs_ = 0;
  uint32_t lastAirTimeMs_ = 0;
};

Sx1262Driver sx1262Driver;

}

Driver& driver() {
  return sx1262Driver;
}

}

#endif
