#pragma once

#include "esphome/core/defines.h"
#ifdef USE_LORABRIDGE_VIRTUAL_GATEWAY

#include <RadioLib.h>
#include <atomic>

namespace esphome {
namespace lorabridge {

// Receives a copy of the PHYPayload (MHDR..MIC) after it was RF-transmitted.
// Called from the LoRaWAN FreeRTOS task.
class UplinkCaptureSink {
 public:
  virtual void on_uplink_captured(const uint8_t *phy_payload, size_t len, float freq_mhz, uint8_t sf,
                                  float bw_khz) = 0;
};

// Chip-independent control surface so LoRaBridge can steer the wrapper
// without knowing the concrete CaptureRadio<R> instantiation.
class CaptureControl {
 public:
  virtual void set_capture_sink(UplinkCaptureSink *sink) = 0;
  // Enable the TX tee. Stays false until the OTAA join succeeded, so join
  // requests are never copied: a join-accept cannot be delivered through the
  // TX-only virtual gateway, and if only the virtual gateway heard the join
  // request, the network server would route the accept there and the join
  // would fail despite possible RF coverage.
  virtual void set_tee_enabled(bool enabled) = 0;

  // ---------------------------------------------------------------------
  // TODO(Phase 2 - RX fallback injection). Planned flow:
  //  - A PULL_RESP arrives before RX1 (the server sends immediately). The
  //    forwarder parses the txpk (freq/datr -> RX1-or-RX2 assignment),
  //    buffers the frame with a cycle marker and answers with TX_ACK.
  //  - getIrqFlags(): real hardware ALWAYS wins - a hardware RX_DONE is
  //    passed through and the buffer discarded. Only on hardware timeout
  //    WITH a buffered frame for the current window: report RX_DONE and
  //    serve readData()/getPacketLength()/getSNR() from the buffer.
  //  - Cycle binding is mandatory: clear the buffer at window close /
  //    cycle end - a late PULL_RESP must never slip into the next cycle
  //    (RadioLib would formally accept it).
  // The TX tee below stays untouched when this is attached.
  // virtual int16_t inject_downlink(const uint8_t *phy_payload, size_t len,
  //                                 uint8_t window, uint32_t cycle_marker) = 0;
  // ---------------------------------------------------------------------
};

// TX tee between LoRaWANNode and the physical radio: every transmission goes
// out over RF unchanged; when the tee is enabled, a copy of the PHYPayload is
// handed to the sink afterwards. RX windows run on the real hardware - no
// receive-path overrides.
template<typename R> class CaptureRadio : public R, public CaptureControl {
 public:
  explicit CaptureRadio(Module *mod) : R(mod) {}

  void set_capture_sink(UplinkCaptureSink *sink) override { this->sink_ = sink; }
  void set_tee_enabled(bool enabled) override { this->tee_enabled_.store(enabled); }

  int16_t transmit(const uint8_t *data, size_t len, uint8_t addr = 0) override {
    int16_t state = R::transmit(data, len, addr);  // always transmit over RF (blocking)
    // Send the copy only AFTER transmit() returns: it then arrives within the
    // network server's ~200 ms dedup window together with the reports of real
    // gateways instead of ~1.5 s (SF12 airtime) earlier - otherwise the
    // virtual gateway would win the downlink routing.
    if (state == RADIOLIB_ERR_NONE && this->tee_enabled_.load() && this->sink_ != nullptr)
      this->sink_->on_uplink_captured(data, len, this->last_freq_mhz_, this->last_sf_, this->last_bw_khz_);
    return state;
  }

  // Pass-through with parameter capture: LoRaWANNode programs the channel via
  // setPhyProperties() right before each transmit; these values feed the rxpk
  // JSON (freq/datr).
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
  UplinkCaptureSink *sink_{nullptr};
  std::atomic<bool> tee_enabled_{false};
  float last_freq_mhz_{868.1f};
  uint8_t last_sf_{12};
  float last_bw_khz_{125.0f};
};

}  // namespace lorabridge
}  // namespace esphome

#endif  // USE_LORABRIDGE_VIRTUAL_GATEWAY
