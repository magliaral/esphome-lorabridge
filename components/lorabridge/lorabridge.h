#pragma once

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

namespace esphome {
namespace lorabridge {

class LoRaBridge : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // Nach dem SPI-Bus, aber vor den "normalen" Komponenten initialisieren
  float get_setup_priority() const override { return setup_priority::DATA; }

  // LoRaWAN-Config Setter
  void set_region(const LoRaWANBand_t &region) { this->region_ = region; }
  void set_sub_band(uint8_t sub_band) { this->sub_band_ = sub_band; }
  void set_join_eui(uint64_t join_eui) { this->join_eui_ = join_eui; }
  void set_dev_eui(uint64_t dev_eui) { this->dev_eui_ = dev_eui; }
  void set_app_key(const std::array<uint8_t, 16> &app_key) { this->app_key_ = app_key; }
  void set_nwk_key(const std::array<uint8_t, 16> &nwk_key) { this->nwk_key_ = nwk_key; }
  void set_uplink_interval(uint32_t uplink_interval) { this->uplink_interval_ = uplink_interval; }

  // Chip + Pin Setter
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

  // LoRaWAN configuration
  LoRaWANBand_t region_ = EU868;
  uint8_t sub_band_ = 0;
  uint64_t join_eui_;
  uint64_t dev_eui_;
  std::array<uint8_t, 16> app_key_;
  std::array<uint8_t, 16> nwk_key_;
  uint32_t uplink_interval_;
  int16_t state_;

  // DevNonce/JoinNonce-Persistenz (LoRaWAN 1.0.4 verlangt monoton
  // steigende DevNonces ueber Reboots hinweg)
  using NoncesBuffer = std::array<uint8_t, RADIOLIB_LORAWAN_NONCES_BUF_SIZE>;
  ESPPreferenceObject nonces_pref_;
  void save_nonces_();

  // Factory: creates the correct RadioLib type
  PhysicalLayer *createRadio(Module *mod, int16_t &state);

  // Configuration
  static const uint8_t MAX_JOIN_ATTEMPTS = 0;
  static const uint32_t JOIN_DELAY_MS = 30000;

  // Helpers
  String stateDecode(const int16_t result);

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
