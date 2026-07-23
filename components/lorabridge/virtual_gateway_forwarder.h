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

// Injects captured LoRaWAN uplinks into a network server via the Semtech UDP
// packet-forwarder protocol v2 (PUSH_DATA/rxpk) and keeps the NAT mapping
// alive with PULL_DATA while capture is active.
//
// Threading: on_uplink_captured() runs on the LoRaWAN FreeRTOS task and only
// sends on the socket. loop() runs on the ESPHome main task and owns DNS
// resolution, socket lifecycle, keepalives, RX draining and the single
// PUSH_DATA retransmit. mutex_ guards the socket fd lifecycle and the
// pending-PUSH state against cross-task access.
class VirtualGatewayForwarder : public UplinkCaptureSink {
 public:
  void set_server(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_keepalive_interval(uint32_t ms) { this->keepalive_ms_ = ms; }

  void setup();  // derives + logs the gateway EUI, no network access
  void loop();   // main task: DNS, socket, keepalive, drain RX, retransmit

  // True while CAPTURE is the selected transport; gates keepalives.
  void set_active(bool active) { this->active_.store(active); }
  // DNS resolved and socket open — precondition for choosing CAPTURE.
  bool is_ready() const { return this->ready_.load(); }
  // PULL_ACK seen within 3 keepalive intervals.
  bool is_connected() const;

  const uint8_t *gateway_eui() const { return this->gateway_eui_; }

  void on_uplink_captured(const uint8_t *phy_payload, size_t len, float freq_mhz, uint8_t sf,
                          float bw_khz) override;

 private:
  bool resolve_and_connect_();  // getaddrinfo + socket() + connect(), non-blocking fd
  void close_socket_();
  void send_pull_data_();
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
  bool last_net_connected_{false};

  bool got_pull_ack_{false};
  std::atomic<uint32_t> last_pull_ack_ms_{0};
  uint32_t last_pull_data_ms_{0};
  uint32_t last_dns_attempt_ms_{0};

  // Pending PUSH_DATA awaiting its PUSH_ACK: retransmitted exactly once with
  // the same token if no ACK arrives within RETRANSMIT_TIMEOUT_MS.
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
