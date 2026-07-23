#pragma once

#include "esphome/core/defines.h"
#ifdef USE_LORABRIDGE_VIRTUAL_GATEWAY

#include <RadioLib.h>
#include <atomic>

namespace esphome {
namespace lorabridge {

enum class TransportMode : uint8_t { RADIO = 0, CAPTURE = 1 };

// Receives the finished PHYPayload (MHDR..MIC) when an uplink is captured
// instead of RF-transmitted. Called from the LoRaWAN FreeRTOS task.
class UplinkCaptureSink {
 public:
  virtual void on_uplink_captured(const uint8_t *phy_payload, size_t len, float freq_mhz, uint8_t sf,
                                  float bw_khz) = 0;
};

// Chip-independent control surface so LoRaBridge can steer the wrapper
// without knowing the concrete CaptureRadio<R> instantiation.
class CaptureControl {
 public:
  virtual void set_transport_mode(TransportMode m) = 0;
  virtual TransportMode get_transport_mode() const = 0;
  virtual void set_capture_sink(UplinkCaptureSink *sink) = 0;

  // TODO(Phase 2 - RX emulation): downlink injection interface.
  // Planned flow: the forwarder parses a PULL_RESP txpk (freq, datr, base64
  // data) and calls inject_downlink(); the wrapper buffers the frame, reports
  // RX_DONE instead of TIMEOUT from getIrqFlags() during the fake RX window,
  // fires the registered packet-received action and serves the bytes via
  // overridden readData()/getPacketLength()/getSNR(). The TX path stays
  // untouched. Do NOT implement in Phase 1.
  // virtual int16_t inject_downlink(const uint8_t *phy_payload, size_t len) = 0;
};

// Sits between LoRaWANNode and the physical radio. In RADIO mode every call
// passes through unchanged. In CAPTURE mode transmit() hands the frame to the
// sink and the RX windows are answered with an immediate fake timeout, so the
// radio never keys the PA and never listens.
//
// Config calls (setFrequency/setDataRate/...) are ALWAYS passed through to the
// real hardware, so the chip state stays consistent and switching back to
// RADIO between uplinks needs no re-initialization.
template<typename R> class CaptureRadio : public R, public CaptureControl {
 public:
  explicit CaptureRadio(Module *mod) : R(mod) {}

  void set_transport_mode(TransportMode m) override { this->mode_.store(m); }
  TransportMode get_transport_mode() const override { return this->mode_.load(); }
  void set_capture_sink(UplinkCaptureSink *sink) override { this->sink_ = sink; }

  // LoRaWANNode uses the blocking transmit() for join requests and uplinks
  // and reads no radio state afterwards (verified against RadioLib 7.1.2),
  // so returning ERR_NONE immediately is sufficient.
  int16_t transmit(const uint8_t *data, size_t len, uint8_t addr = 0) override {
    if (this->mode_.load() == TransportMode::CAPTURE && this->sink_ != nullptr) {
      this->sink_->on_uplink_captured(data, len, this->last_freq_mhz_, this->last_sf_, this->last_bw_khz_);
      return RADIOLIB_ERR_NONE;
    }
    return R::transmit(data, len, addr);
  }

  int16_t startReceive(uint32_t timeout, RadioLibIrqFlags_t irq_flags, RadioLibIrqFlags_t irq_mask,
                       size_t len) override {
    if (this->mode_.load() == TransportMode::CAPTURE) {
      this->fake_rx_active_ = true;  // radio stays in standby, no RF listening
      return RADIOLIB_ERR_NONE;
    }
    return R::startReceive(timeout, irq_flags, irq_mask, len);
  }

  uint32_t getIrqFlags() override {
    if (this->mode_.load() == TransportMode::CAPTURE && this->fake_rx_active_) {
      this->fake_rx_active_ = false;
      // checkIrq(RADIOLIB_IRQ_TIMEOUT) == getIrqFlags() & irqMap[TIMEOUT]:
      // report "RX timeout" so receiveCommon() closes the window cleanly.
      return this->irqMap[RADIOLIB_IRQ_TIMEOUT];
    }
    return R::getIrqFlags();
  }

  int16_t setFrequency(float freq) override {
    this->last_freq_mhz_ = freq;
    return R::setFrequency(freq);
  }

  int16_t setDataRate(DataRate_t dr) override {
    // LoRaWAN uplinks in this component are LoRa-modem only
    this->last_sf_ = dr.lora.spreadingFactor;
    this->last_bw_khz_ = dr.lora.bandwidth;
    return R::setDataRate(dr);
  }

 private:
  std::atomic<TransportMode> mode_{TransportMode::RADIO};
  UplinkCaptureSink *sink_{nullptr};
  volatile bool fake_rx_active_{false};
  float last_freq_mhz_{868.1f};
  uint8_t last_sf_{12};
  float last_bw_khz_{125.0f};
};

}  // namespace lorabridge
}  // namespace esphome

#endif  // USE_LORABRIDGE_VIRTUAL_GATEWAY
