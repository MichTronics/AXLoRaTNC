#include "radio.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "util/timer.h"
#include "util/log.h"

#if defined(AXLORA_VARIANT_TBEAM) || defined(AXLORA_VARIANT_LILYGO_T3_V161)

namespace axlora::radio {
namespace {

static volatile bool rxFlag_ = false;

IRAM_ATTR static void onDio0Interrupt() {
  rxFlag_ = true;
}

constexpr uint32_t radioLibPin(int pin) {
  return pin >= 0 ? static_cast<uint32_t>(pin) : RADIOLIB_NC;
}

class Sx1276Driver final : public Driver {
 public:
  Result init() override {
    if constexpr (variant::PIN_LED_TX >= 0) {
      pinMode(variant::PIN_LED_TX, OUTPUT);
      digitalWrite(variant::PIN_LED_TX, LOW);
    }
    if constexpr (variant::PIN_LED_RX >= 0) {
      pinMode(variant::PIN_LED_RX, OUTPUT);
      digitalWrite(variant::PIN_LED_RX, LOW);
    }
    SPI.begin(variant::PIN_SPI_SCK, variant::PIN_SPI_MISO, variant::PIN_SPI_MOSI, variant::PIN_RADIO_CS);
    logicalFrequencyMHz_ = variant::DEFAULT_FREQUENCY_MHZ;
    frequencyCorrectionMHz_ = variant::DEFAULT_FREQUENCY_CORRECTION_MHZ;
    const int16_t state = radio_.begin(correctedFrequencyMHz(logicalFrequencyMHz_),
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
    radio_.setCRC(true);
    radio_.invertIQ(false);
    radio_.setDio0Action(onDio0Interrupt, RISING);
    radio_.startReceive();
    return Result::Ok;
  }

  Result send(const uint8_t* data, size_t len) override {
    updateRxLed();
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
    setTxLed(true);
    const int16_t state = radio_.transmit(const_cast<uint8_t*>(data), len);
    setTxLed(false);
    rxFlag_ = false;
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
    updateRxLed();
    if (!rxFlag_) {
      return Result::NoPacket;
    }
    rxFlag_ = false;

    packet.len = radio_.getPacketLength();
    if (packet.len == 0 || packet.len > sizeof(packet.data)) {
      radio_.startReceive();
      return Result::NoPacket;
    }
    const int16_t state = radio_.readData(packet.data, packet.len);
    if (state == RADIOLIB_ERR_NONE) {
      pulseRxLed();
      packet.rssi = radio_.getRSSI();
      packet.snr = radio_.getSNR();
      radio_.getFrequencyError(true);
      ++stats().rxOk;
      radio_.startReceive();
      return Result::Ok;
    }
    if (state == RADIOLIB_ERR_RX_TIMEOUT || state == RADIOLIB_ERR_CRC_MISMATCH) {
      if (state == RADIOLIB_ERR_CRC_MISMATCH) {
        ++stats().rxFail;
      }
      radio_.startReceive();
      return Result::NoPacket;
    }
    radio_.startReceive();
    return Result::NoPacket;
  }

  Result setFrequency(float frequencyMHz) override {
    logicalFrequencyMHz_ = frequencyMHz;
    const int16_t s = radio_.setFrequency(correctedFrequencyMHz(frequencyMHz));
    if (s != RADIOLIB_ERR_NONE) return Result::HardwareError;
    radio_.startReceive();
    return Result::Ok;
  }

  Result setFrequencyCorrection(float correctionMHz) override {
    frequencyCorrectionMHz_ = correctionMHz;
    const int16_t s = radio_.setFrequency(correctedFrequencyMHz(logicalFrequencyMHz_));
    if (s != RADIOLIB_ERR_NONE) return Result::HardwareError;
    radio_.startReceive();
    return Result::Ok;
  }

  Result setPower(int8_t powerDbm) override {
    if (powerDbm > variant::MAX_TX_POWER_DBM) {
      powerDbm = variant::MAX_TX_POWER_DBM;
    }
    const int16_t s = radio_.setOutputPower(powerDbm);
    if (s != RADIOLIB_ERR_NONE) return Result::HardwareError;
    radio_.startReceive();
    return Result::Ok;
  }

  Result setSpreadingFactor(uint8_t sf) override {
    const int16_t s = radio_.setSpreadingFactor(sf);
    if (s != RADIOLIB_ERR_NONE) return Result::HardwareError;
    radio_.startReceive();
    return Result::Ok;
  }

  Result setBandwidth(float bandwidthKhz) override {
    const int16_t s = radio_.setBandwidth(bandwidthKhz);
    if (s != RADIOLIB_ERR_NONE) return Result::HardwareError;
    radio_.startReceive();
    return Result::Ok;
  }

  Result setCodingRate(uint8_t cr) override {
    const int16_t s = radio_.setCodingRate(cr);
    if (s != RADIOLIB_ERR_NONE) return Result::HardwareError;
    radio_.startReceive();
    return Result::Ok;
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
  float correctedFrequencyMHz(float frequencyMHz) const {
    return frequencyMHz + frequencyCorrectionMHz_;
  }

  void updateRxLed() {
    if constexpr (variant::PIN_LED_RX >= 0) {
      if (rxLedOffMs_ != 0 &&
          static_cast<int32_t>(util::nowMs() - rxLedOffMs_) >= 0) {
        digitalWrite(variant::PIN_LED_RX, LOW);
        rxLedOffMs_ = 0;
      }
    }
  }

  void pulseRxLed() {
    if constexpr (variant::PIN_LED_RX >= 0) {
      digitalWrite(variant::PIN_LED_RX, HIGH);
      rxLedOffMs_ = util::nowMs() + 25;
    }
  }

  void setTxLed(bool on) {
    if constexpr (variant::PIN_LED_TX >= 0) {
      digitalWrite(variant::PIN_LED_TX, on ? HIGH : LOW);
    }
  }

  uint32_t estimateAirtimeMs(size_t len) {
    const RadioLibTime_t airtimeUs = radio_.getTimeOnAir(len);
    const uint32_t airtimeMs = static_cast<uint32_t>((airtimeUs + 999) / 1000);
    return airtimeMs == 0 ? 1 : airtimeMs;
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
  float logicalFrequencyMHz_ = variant::DEFAULT_FREQUENCY_MHZ;
  float frequencyCorrectionMHz_ = variant::DEFAULT_FREQUENCY_CORRECTION_MHZ;
  uint32_t lastTxMs_ = 0;
  uint32_t lastAirTimeMs_ = 0;
  uint32_t rxLedOffMs_ = 0;
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
