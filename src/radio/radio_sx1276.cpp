#include "radio.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "util/timer.h"
#include "util/log.h"

#if defined(AXLORA_VARIANT_TBEAM)

namespace axlora::radio {
namespace {

constexpr uint32_t radioLibPin(int pin) {
  return pin >= 0 ? static_cast<uint32_t>(pin) : RADIOLIB_NC;
}

class Sx1276Driver final : public Driver {
 public:
  Result init() override {
    SPI.begin(variant::PIN_SPI_SCK, variant::PIN_SPI_MISO, variant::PIN_SPI_MOSI, variant::PIN_RADIO_CS);
    const int16_t state = radio_.begin(variant::DEFAULT_FREQUENCY_MHZ,
                                       variant::DEFAULT_BANDWIDTH_KHZ,
                                       variant::DEFAULT_SPREADING_FACTOR,
                                       variant::DEFAULT_CODING_RATE,
                                       variant::DEFAULT_SYNC_WORD,
                                       variant::DEFAULT_TX_POWER_DBM,
                                       variant::PREAMBLE_LENGTH);
    if (state != RADIOLIB_ERR_NONE) {
      LOG_ERR("SX1276 init failed: %d", state);
      return Result::HardwareError;
    }
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

  Result setSpreadingFactor(uint8_t sf) override {
    return radio_.setSpreadingFactor(sf) == RADIOLIB_ERR_NONE ? Result::Ok : Result::HardwareError;
  }

  Result setBandwidth(float bandwidthKhz) override {
    const int16_t s = radio_.setBandwidth(bandwidthKhz);
    if (s != RADIOLIB_ERR_NONE) return Result::HardwareError;
    radio_.startReceive();
    return Result::Ok;
  }

  Result setCodingRate(uint8_t cr) override {
    return radio_.setCodingRate(cr) == RADIOLIB_ERR_NONE ? Result::Ok : Result::HardwareError;
  }

  void setDutyCycle(bool enabled, uint32_t dutyCyclePpm) override {
    dutyCycleEnabled_ = enabled;
    dutyCyclePpm_ = dutyCyclePpm == 0 ? variant::DUTY_CYCLE_PPM : dutyCyclePpm;
  }

  bool dutyCycleEnabled() const override { return dutyCycleEnabled_; }
  uint32_t dutyCyclePpm() const override { return dutyCyclePpm_; }

  float getRSSI() override { return radio_.getRSSI(); }
  float getSNR() override { return radio_.getSNR(); }
  void sleep() override { radio_.sleep(); }
  void standby() override { radio_.standby(); }

 private:
  uint32_t estimateAirtimeMs(size_t len) const {
    return static_cast<uint32_t>(500 + (len * 12));
  }

  bool canTransmit(uint32_t now) const {
    if (!dutyCycleEnabled_) {
      return true;
    }
    if (lastTxMs_ == 0) {
      return true;
    }
    const uint32_t minGap = (lastAirTimeMs_ * 1000000UL) / dutyCyclePpm_;
    return util::elapsed(now, lastTxMs_, minGap);
  }

  Module module_{radioLibPin(variant::PIN_RADIO_CS),
                 radioLibPin(variant::PIN_RADIO_DIO1),
                 radioLibPin(variant::PIN_RADIO_RST),
                 radioLibPin(variant::PIN_RADIO_DIO2)};
  SX1276 radio_{&module_};
  uint32_t lastTxMs_ = 0;
  uint32_t lastAirTimeMs_ = 0;
  bool dutyCycleEnabled_ = true;
  uint32_t dutyCyclePpm_ = variant::DUTY_CYCLE_PPM;
};

Sx1276Driver sx1276Driver;

}

Driver& driver() {
  return sx1276Driver;
}

}

#endif
