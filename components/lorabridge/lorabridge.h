#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include <RadioLib.h>
#include <array>
#include <vector>
#include "esphome_spi_hal.h"

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#ifdef USE_LORABRIDGE_VIRTUAL_GATEWAY
#include <atomic>
#include "capture_radio.h"
#include "virtual_gateway_forwarder.h"
#endif

namespace esphome {
namespace lorabridge {

class LoRaBridge : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // Initialize after the SPI bus, but before the "regular" components
  float get_setup_priority() const override { return setup_priority::DATA; }

  // LoRaWAN config setters
  void set_region(const LoRaWANBand_t &region) { this->region_ = region; }
  void set_sub_band(uint8_t sub_band) { this->sub_band_ = sub_band; }
  void set_join_eui(uint64_t join_eui) { this->join_eui_ = join_eui; }
  void set_dev_eui(uint64_t dev_eui) { this->dev_eui_ = dev_eui; }
  void set_app_key(const std::array<uint8_t, 16> &app_key) { this->app_key_ = app_key; }
  void set_nwk_key(const std::array<uint8_t, 16> &nwk_key) { this->nwk_key_ = nwk_key; }
  void set_uplink_interval(uint32_t uplink_interval) { this->uplink_interval_ = uplink_interval; }
  void set_join_dr(uint8_t join_dr) { this->join_dr_ = join_dr; }
  void set_scan_guard(uint16_t scan_guard) { this->scan_guard_ = scan_guard; }

  // Chip + pin setters
  void set_chip(const std::string &chip) { this->chip_ = chip; }
  void set_nss_pin(int8_t pin) { this->nss_pin_ = pin; }
  void set_rst_pin(int8_t pin) { this->rst_pin_ = pin; }
  void set_irq_pin(int8_t pin) { this->irq_pin_ = pin; }
  void set_busy_pin(int8_t pin) { this->busy_pin_ = pin; }
  void set_gpio_pin(int8_t pin) { this->gpio_pin_ = pin; }

  // Payload
  void add_sensor_payload_item(sensor::Sensor *sens, float multiplier, float offset, uint8_t bytes);
  void add_binary_payload_item(binary_sensor::BinarySensor *bin_sens);
  void add_text_payload_item(text_sensor::TextSensor *text_sens);

#ifdef USE_LORABRIDGE_VIRTUAL_GATEWAY
  // Virtual gateway config setters
  void set_vgw_server(const std::string &server) { this->vgw_server_ = server; }
  void set_vgw_port(uint16_t port) { this->vgw_port_ = port; }
  void set_vgw_keepalive(uint32_t keepalive_ms) { this->vgw_keepalive_ms_ = keepalive_ms; }

  // Diagnostics
  void set_transport_mode_text_sensor(text_sensor::TextSensor *sens) { this->transport_mode_sensor_ = sens; }
  void set_gateway_connected_binary_sensor(binary_sensor::BinarySensor *sens) {
    this->gateway_connected_sensor_ = sens;
  }
#endif

 private:
  // Chip type (set via YAML)
  std::string chip_{"SX1262"};

  // Pins (Defaults: T-Connect Pro)
  int8_t nss_pin_{14};
  int8_t rst_pin_{42};
  int8_t irq_pin_{45};
  int8_t busy_pin_{38};
  int8_t gpio_pin_{-1};

  // Radio + Node pointers
  ESPHomeSPIHal *hal_{nullptr};
  PhysicalLayer *radio_{nullptr};
  LoRaWANNode *node_{nullptr};
  bool joined_{false};
  bool init_done_{false};
  uint32_t last_uplink_ms_{0};

#ifdef USE_LORABRIDGE_VIRTUAL_GATEWAY
  // Virtual gateway: capture wrapper control surface + UDP forwarder
  CaptureControl *capture_ctl_{nullptr};
  VirtualGatewayForwarder *forwarder_{nullptr};
  std::string vgw_server_;
  uint16_t vgw_port_{1700};
  uint32_t vgw_keepalive_ms_{10000};
  // Written by the LoRaWAN task, published from loop() on the main task
  std::atomic<bool> transport_capture_{false};
  bool last_published_capture_{false};
  bool last_published_connected_{false};
  bool published_once_{false};
  text_sensor::TextSensor *transport_mode_sensor_{nullptr};
  binary_sensor::BinarySensor *gateway_connected_sensor_{nullptr};
#endif

  // LoRaWAN configuration
  LoRaWANBand_t region_ = EU868;
  uint8_t sub_band_ = 0;
  uint64_t join_eui_;
  uint64_t dev_eui_;
  std::array<uint8_t, 16> app_key_;
  std::array<uint8_t, 16> nwk_key_;
  uint32_t uplink_interval_;
  uint8_t join_dr_{0};
  uint16_t scan_guard_{50};
  int16_t state_;

  // DevNonce/JoinNonce persistence (LoRaWAN 1.0.4 requires DevNonces
  // to increase monotonically across reboots)
  using NoncesBuffer = std::array<uint8_t, RADIOLIB_LORAWAN_NONCES_BUF_SIZE>;
  ESPPreferenceObject nonces_pref_;
  void save_nonces_();

  // Factory: creates the correct RadioLib type
  PhysicalLayer *createRadio(Module *mod, int16_t &state);

  // Configuration
  static const uint32_t JOIN_DELAY_MS = 30000;

  // Payload structs
  struct SensorPayloadItem {
    sensor::Sensor *sensor_{nullptr};
    float multiplier_{1.0f};
    float offset_{0.0f};
    uint8_t bytes_{2};
  };

  struct BinaryPayloadItem {
    binary_sensor::BinarySensor *binary_sensor_{nullptr};
    uint8_t bit_position_{0};
  };

  struct TextPayloadItem {
    text_sensor::TextSensor *text_sensor_{nullptr};
  };

  // Payload lists
  std::vector<SensorPayloadItem> sensor_payload_items_;
  std::vector<BinaryPayloadItem> binary_payload_items_;
  std::vector<TextPayloadItem> text_payload_items_;

  // Packing helpers
  std::vector<uint8_t> pack_binary_sensors();
  std::vector<uint8_t> pack_text_sensors();
};

}  // namespace lorabridge
}  // namespace esphome
