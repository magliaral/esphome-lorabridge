#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include <RadioLib.h>
#include <array>
#include <vector>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#define RADIO_BOARD_AUTO
#include <RadioBoards.h>

namespace esphome {
namespace lorabridge {

class LoRaBridge : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  void set_region(const LoRaWANBand_t &region) { this->region_ = region; }
  void set_sub_band(uint8_t sub_band) { this->sub_band_ = sub_band; }
  void set_join_eui(uint64_t join_eui) { this->join_eui_ = join_eui; }
  void set_dev_eui(uint64_t dev_eui) { this->dev_eui_ = dev_eui; }
  void set_app_key(const std::array<uint8_t, 16> &app_key) { this->app_key_ = app_key; }
  void set_nwk_key(const std::array<uint8_t, 16> &nwk_key) { this->nwk_key_ = nwk_key; }
  void set_uplink_interval(uint32_t uplink_interval) { this->uplink_interval_ = uplink_interval; }

  void add_sensor_payload_item(sensor::Sensor *sens, float multiplier, float offset, uint8_t bytes);
  void add_binary_payload_item(binary_sensor::BinarySensor *bin_sens);
  void add_text_payload_item(text_sensor::TextSensor *text_sens);

 private:
  // LoRaWAN-Objekte und Variablen
  LoRaWANBand_t region_ = EU868;
  uint8_t sub_band_ = 0;
  uint64_t join_eui_;
  uint64_t dev_eui_;
  std::array<uint8_t, 16> app_key_;
  std::array<uint8_t, 16> nwk_key_;
  uint32_t uplink_interval_;
  int16_t state;

  Radio radio = new RadioModule();
  LoRaWANNode node = LoRaWANNode(&radio, &region_, sub_band_);

  // Task-Handle
  static void joinLoRaWanTask(void *pvParameters);

  // Weitere Konfigurationsparameter
  static const uint8_t MAX_JOIN_ATTEMPTS = 0;    // 0 für unbegrenzte Versuche
  static const uint32_t JOIN_DELAY_MS = 30000;   // 30 Sekunden

  // Hilfsfunktionen
  String stateDecode(const int16_t result);

  // Kleiner Struct für jedes Payload-Element
  struct SensorPayloadItem {
    sensor::Sensor *sensor_{nullptr};
    float multiplier_{1.0f};
    float offset_{0.0f};
    uint8_t bytes_{2};
  };

  // Struct für Binary-Sensor-Payload-Elemente
  struct BinaryPayloadItem {
    binary_sensor::BinarySensor *binary_sensor_{nullptr};
    uint8_t bit_position_{0};
  };

  // Struct für Text-Sensor-Payload-Elemente
  struct TextPayloadItem {
    text_sensor::TextSensor *text_sensor_{nullptr};
  };

  // Listen aller Payload-Einträge
  std::vector<SensorPayloadItem> sensor_payload_items_;
  std::vector<BinaryPayloadItem> binary_payload_items_;
  std::vector<TextPayloadItem> text_payload_items_;

  // Hilfsfunktion zum Packen der Binary-Sensoren
  std::vector<uint8_t> pack_binary_sensors();

  // Hilfsfunktion zum Packen der Text-Sensoren
  std::vector<uint8_t> pack_text_sensors();

};

}  // namespace lorabridge
}  // namespace esphome
