#pragma once

#include "esphome/core/defines.h"
#ifdef USE_LORABRIDGE_VIRTUAL_GATEWAY

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <lwip/sockets.h>

#include "capture_radio.h"

namespace esphome {
namespace lorabridge {

// Injects a copy of every RF-transmitted LoRaWAN uplink into a network server
// via the Semtech UDP packet-forwarder protocol v2 (PUSH_DATA/rxpk). The
// virtual gateway runs in parallel to real gateways; the network server
// deduplicates the frame like it does for any multi-gateway reception. Keeps
// the NAT mapping alive with PULL_DATA while the network is up.
//
// Threading: on_uplink_captured() runs on the LoRaWAN FreeRTOS task and only
// sends on the socket. loop() runs on the ESPHome main task and owns DNS
// resolution, socket lifecycle, keepalives, RX draining and the single
// PUSH_DATA retransmit.
class VirtualGatewayForwarder : public UplinkCaptureSink {
 public:
  void set_server(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_keepalive_interval(uint32_t ms) { this->keepalive_ms_ = ms; }

  void setup();  // derives + logs the gateway EUI, no network access
  void loop();   // main task: DNS, socket, keepalive, drain RX, retransmit

  // Fed with network::is_connected() from the main loop. Gates the PULL_DATA
  // keepalive and the acceptance of uplink copies; when false, captured
  // frames are dropped silently (VERBOSE).
  void set_active(bool active) { this->active_.store(active); }
  // PULL_ACK seen within 3 keepalive intervals.
  bool is_connected() const;
  // Uplink copies confirmed by the server via PUSH_ACK (exactly once per
  // uplink, retransmit double-ACKs not counted). Says nothing about whether
  // the network server used the frame or dropped it as a duplicate.
  uint32_t uplinks_forwarded() const { return this->uplinks_forwarded_.load(); }

  const uint8_t *gateway_eui() const { return this->gateway_eui_; }

  void on_uplink_captured(const uint8_t *phy_payload, size_t len, float freq_mhz, uint8_t sf,
                          float bw_khz) override;

 private:
  bool resolve_and_connect_();  // getaddrinfo + socket() + connect(), non-blocking fd
  void close_socket_();
  void send_pull_data_();
  void send_tx_ack_(uint16_t token);
  void drain_rx_();
  void check_retransmit_();

  std::string host_{"eu1.cloud.thethings.network"};
  uint16_t port_{1700};
  uint32_t keepalive_ms_{10000};
  uint8_t gateway_eui_[8]{};

  int fd_{-1};
  std::mutex mutex_;

  std::atomic<bool> ready_{false};
  std::atomic<bool> active_{false};
  std::atomic<uint32_t> uplinks_forwarded_{0};
  bool last_net_connected_{false};

  bool got_pull_ack_{false};
  std::atomic<uint32_t> last_pull_ack_ms_{0};
  uint32_t last_pull_data_ms_{0};
  uint32_t last_dns_attempt_ms_{0};

  // Pending PUSH_DATA awaiting its PUSH_ACK: retransmitted exactly once with
  // the same token if no ACK arrives within RETRANSMIT_TIMEOUT_MS.
  //
  // SYNCHRONIZATION INVARIANT: this pending state is touched by both tasks -
  // the LoRaWAN task writes it in on_uplink_captured(), the main task clears
  // it on the ACK match in drain_rx_() and triggers the retransmit in
  // check_retransmit_(). EVERY access to the fields below (and the counter
  // increment tied to the ACK match) must happen under mutex_ - the same
  // mutex that guards the socket lifecycle. No lock-free path, no partial
  // access outside the lock.
  static const uint32_t RETRANSMIT_TIMEOUT_MS = 500;
  uint8_t pending_buf_[512];
  size_t pending_len_{0};
  uint16_t pending_token_{0};
  uint32_t pending_sent_ms_{0};
  bool pending_active_{false};
  bool pending_retransmitted_{false};
};

}  // namespace lorabridge
}  // namespace esphome

#endif  // USE_LORABRIDGE_VIRTUAL_GATEWAY
