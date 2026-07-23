#include "esphome/core/defines.h"
#ifdef USE_LORABRIDGE_VIRTUAL_GATEWAY

#include "virtual_gateway_forwarder.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

#include <lwip/netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <esp_timer.h>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace lorabridge {

static const char *TAG = "lorabridge.vgw";

// Semtech UDP packet-forwarder protocol v2
static const uint8_t PROTOCOL_VERSION = 0x02;
static const uint8_t PKT_PUSH_DATA = 0x00;
static const uint8_t PKT_PUSH_ACK = 0x01;
static const uint8_t PKT_PULL_DATA = 0x02;
static const uint8_t PKT_PULL_RESP = 0x03;
static const uint8_t PKT_PULL_ACK = 0x04;

static const uint32_t DNS_RETRY_MS = 30000;

void VirtualGatewayForwarder::setup() {
  // EUI-64 from the factory MAC: mac[0..2] FF FE mac[3..5]
  uint8_t mac[6];
  get_mac_address_raw(mac);
  this->gateway_eui_[0] = mac[0];
  this->gateway_eui_[1] = mac[1];
  this->gateway_eui_[2] = mac[2];
  this->gateway_eui_[3] = 0xFF;
  this->gateway_eui_[4] = 0xFE;
  this->gateway_eui_[5] = mac[3];
  this->gateway_eui_[6] = mac[4];
  this->gateway_eui_[7] = mac[5];
  ESP_LOGI(TAG, "Virtual gateway EUI: %02X%02X%02X%02X%02X%02X%02X%02X (register this in the TTN console)",
           gateway_eui_[0], gateway_eui_[1], gateway_eui_[2], gateway_eui_[3], gateway_eui_[4], gateway_eui_[5],
           gateway_eui_[6], gateway_eui_[7]);
}

bool VirtualGatewayForwarder::is_connected() const {
  if (!this->got_pull_ack_)
    return false;
  return (millis() - this->last_pull_ack_ms_.load()) < 3 * this->keepalive_ms_;
}

bool VirtualGatewayForwarder::resolve_and_connect_() {
  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%u", this->port_);

  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  struct addrinfo *res = nullptr;
  int err = getaddrinfo(this->host_.c_str(), port_str, &hints, &res);
  if (err != 0 || res == nullptr) {
    ESP_LOGW(TAG, "DNS lookup of %s failed (%d), retrying in %" PRIu32 "s", this->host_.c_str(), err,
             DNS_RETRY_MS / 1000);
    return false;
  }

  int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    ESP_LOGW(TAG, "socket() failed: errno %d", errno);
    freeaddrinfo(res);
    return false;
  }
  // connect() the UDP socket so send()/recv() work and the kernel filters
  // datagrams from other sources.
  if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    ESP_LOGW(TAG, "connect() failed: errno %d", errno);
    freeaddrinfo(res);
    ::close(fd);
    return false;
  }
  freeaddrinfo(res);
  fcntl(fd, F_SETFL, O_NONBLOCK);

  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->fd_ = fd;
  }
  this->ready_.store(true);
  ESP_LOGI(TAG, "Virtual gateway socket ready (%s:%u)", this->host_.c_str(), this->port_);
  return true;
}

void VirtualGatewayForwarder::close_socket_() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->fd_ >= 0) {
    ::close(this->fd_);
    this->fd_ = -1;
  }
  this->ready_.store(false);
  this->pending_active_ = false;
  this->got_pull_ack_ = false;
}

void VirtualGatewayForwarder::loop() {
  const uint32_t now = millis();
  const bool net = network::is_connected();

  // On network loss drop the socket: after reconnect the interface may have a
  // new address, so re-resolve and re-connect from scratch.
  if (!net && this->last_net_connected_) {
    ESP_LOGD(TAG, "Network lost, closing virtual gateway socket");
    this->close_socket_();
  }
  this->last_net_connected_ = net;

  if (net && !this->ready_.load() && (now - this->last_dns_attempt_ms_ >= DNS_RETRY_MS || this->last_dns_attempt_ms_ == 0)) {
    this->last_dns_attempt_ms_ = now;
    this->resolve_and_connect_();
  }
  if (!this->ready_.load())
    return;

  this->drain_rx_();
  this->check_retransmit_();

  // PULL_DATA keepalive only while CAPTURE is the active transport
  // (keeps the NAT/CGNAT mapping open for PULL_ACK/PULL_RESP).
  if (this->active_.load() && now - this->last_pull_data_ms_ >= this->keepalive_ms_) {
    this->last_pull_data_ms_ = now;
    this->send_pull_data_();
  }
}

void VirtualGatewayForwarder::send_pull_data_() {
  uint8_t pkt[12];
  uint16_t token = (uint16_t) random_uint32();
  pkt[0] = PROTOCOL_VERSION;
  pkt[1] = token >> 8;
  pkt[2] = token & 0xFF;
  pkt[3] = PKT_PULL_DATA;
  memcpy(&pkt[4], this->gateway_eui_, 8);

  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->fd_ < 0)
    return;
  if (::send(this->fd_, pkt, sizeof(pkt), 0) < 0) {
    ESP_LOGW(TAG, "PULL_DATA send failed: errno %d", errno);
  } else {
    ESP_LOGV(TAG, "PULL_DATA sent (token %04X)", token);
  }
}

void VirtualGatewayForwarder::drain_rx_() {
  uint8_t buf[512];
  while (true) {
    ssize_t n;
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      if (this->fd_ < 0)
        return;
      n = ::recv(this->fd_, buf, sizeof(buf), 0);
    }
    if (n < 0)
      return;  // EWOULDBLOCK or error: nothing (more) to read
    if (n < 4 || buf[0] != PROTOCOL_VERSION)
      continue;

    const uint16_t token = (uint16_t) ((buf[1] << 8) | buf[2]);
    switch (buf[3]) {
      case PKT_PUSH_ACK: {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->pending_active_ && token == this->pending_token_) {
          ESP_LOGD(TAG, "PUSH_ACK received (token %04X%s)", token, this->pending_retransmitted_ ? ", after retransmit" : "");
          this->pending_active_ = false;
        } else {
          ESP_LOGV(TAG, "PUSH_ACK received (token %04X, no pending frame)", token);
        }
        break;
      }
      case PKT_PULL_ACK:
        this->last_pull_ack_ms_.store(millis());
        this->got_pull_ack_ = true;
        ESP_LOGV(TAG, "PULL_ACK received (token %04X)", token);
        break;
      case PKT_PULL_RESP: {
        // Phase 1: downlinks are not emulated — log and discard.
        // TODO(Phase 2): parse txpk and feed it into CaptureControl::inject_downlink().
        size_t json_len = n - 4;
        if (json_len > 200)
          json_len = 200;
        ESP_LOGD(TAG, "PULL_RESP received, discarding downlink: %.*s", (int) json_len, (const char *) &buf[4]);
        break;
      }
      default:
        ESP_LOGV(TAG, "Unknown packet id 0x%02X (%d bytes)", buf[3], (int) n);
        break;
    }
  }
}

void VirtualGatewayForwarder::check_retransmit_() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (!this->pending_active_ || this->fd_ < 0)
    return;
  if (millis() - this->pending_sent_ms_ < RETRANSMIT_TIMEOUT_MS)
    return;

  if (!this->pending_retransmitted_) {
    // Exactly one retransmit with the identical datagram (same token).
    if (::send(this->fd_, this->pending_buf_, this->pending_len_, 0) < 0) {
      ESP_LOGW(TAG, "PUSH_DATA retransmit failed: errno %d", errno);
    } else {
      ESP_LOGD(TAG, "No PUSH_ACK after %" PRIu32 " ms, retransmitted PUSH_DATA (token %04X)", RETRANSMIT_TIMEOUT_MS,
               this->pending_token_);
    }
    this->pending_retransmitted_ = true;
    this->pending_sent_ms_ = millis();
  } else {
    ESP_LOGW(TAG, "No PUSH_ACK after retransmit (token %04X), giving up", this->pending_token_);
    this->pending_active_ = false;
  }
}

void VirtualGatewayForwarder::on_uplink_captured(const uint8_t *phy_payload, size_t len, float freq_mhz, uint8_t sf,
                                                 float bw_khz) {
  const std::string b64 = base64_encode(phy_payload, len);
  // tmst is defined as a wrapping uint32 microsecond counter; the network
  // server only uses it for downlink timing, which Phase 1 discards anyway.
  const uint32_t tmst = (uint32_t) esp_timer_get_time();

  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->fd_ < 0) {
    // Race guard: the transport chooser verified is_ready() before selecting
    // CAPTURE; the next uplink falls back to RF.
    ESP_LOGW(TAG, "Socket not ready, dropping captured uplink (%u bytes)", (unsigned) len);
    return;
  }

  const uint16_t token = (uint16_t) random_uint32();
  this->pending_buf_[0] = PROTOCOL_VERSION;
  this->pending_buf_[1] = token >> 8;
  this->pending_buf_[2] = token & 0xFF;
  this->pending_buf_[3] = PKT_PUSH_DATA;
  memcpy(&this->pending_buf_[4], this->gateway_eui_, 8);

  // rssi/lsnr are fixed poor values so the network server never sees a "good"
  // link and tries to ADR the device to a faster data rate (ADR is off, this
  // is belt-and-braces).
  int json_len = snprintf(
      (char *) &this->pending_buf_[12], sizeof(this->pending_buf_) - 12,
      "{\"rxpk\":[{\"tmst\":%" PRIu32 ",\"chan\":0,\"rfch\":0,\"freq\":%.3f,\"stat\":1,\"modu\":\"LORA\","
      "\"datr\":\"SF%uBW%u\",\"codr\":\"4/5\",\"rssi\":-119,\"lsnr\":-14.0,\"size\":%u,\"data\":\"%s\"}]}",
      tmst, (double) freq_mhz, (unsigned) sf, (unsigned) bw_khz, (unsigned) len, b64.c_str());
  if (json_len <= 0 || (size_t) json_len >= sizeof(this->pending_buf_) - 12) {
    ESP_LOGW(TAG, "rxpk JSON too large, dropping uplink");
    return;
  }
  this->pending_len_ = 12 + (size_t) json_len;

  if (::send(this->fd_, this->pending_buf_, this->pending_len_, 0) < 0) {
    ESP_LOGW(TAG, "PUSH_DATA send failed: errno %d", errno);
    // Keep the frame pending: check_retransmit_() gets one more attempt.
  } else {
    ESP_LOGI(TAG, "Uplink injected via virtual gateway (%u bytes, %.3f MHz, SF%uBW%u, token %04X)", (unsigned) len,
             (double) freq_mhz, (unsigned) sf, (unsigned) bw_khz, token);
  }
  this->pending_token_ = token;
  this->pending_sent_ms_ = millis();
  this->pending_active_ = true;
  this->pending_retransmitted_ = false;
}

}  // namespace lorabridge
}  // namespace esphome

#endif  // USE_LORABRIDGE_VIRTUAL_GATEWAY
