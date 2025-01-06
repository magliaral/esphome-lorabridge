#include "lorabridge.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lorabridge {

static const char *TAG = "lorabridge.component";

TaskHandle_t join_lora_wan_task_handle_;

void LoRaBridge::add_sensor_payload_item(sensor::Sensor *sens, float multiplier, float offset, uint8_t bytes) {
  SensorPayloadItem item;
  item.sensor_ = sens;
  item.multiplier_ = multiplier;
  item.offset_ = offset;
  item.bytes_ = bytes;
  this->sensor_payload_items_.push_back(item);
}

void LoRaBridge::add_binary_payload_item(binary_sensor::BinarySensor *bin_sens) {
  BinaryPayloadItem item;
  item.binary_sensor_ = bin_sens;
  this->binary_payload_items_.push_back(item);
}

void LoRaBridge::add_text_payload_item(text_sensor::TextSensor *text_sens) {
  TextPayloadItem item;
  item.text_sensor_ = text_sens;
  this->text_payload_items_.push_back(item);
}

std::vector<uint8_t> LoRaBridge::pack_binary_sensors() {
  std::vector<uint8_t> bin_bytes;
  size_t total = this->binary_payload_items_.size();
  size_t full_bytes = total / 8;
  size_t remaining_bits = total % 8;
  
  for (size_t i = 0; i < full_bytes; ++i) {
    uint8_t byte = 0;
    for (size_t bit = 0; bit < 8; ++bit) {
      size_t index = i * 8 + bit;
      if (this->binary_payload_items_[index].binary_sensor_->state) {
        byte |= (1 << bit);
      }
    }
    bin_bytes.push_back(byte);
  }
  
  if (remaining_bits > 0) {
    uint8_t byte = 0;
    for (size_t bit = 0; bit < remaining_bits; ++bit) {
      size_t index = full_bytes * 8 + bit;
      if (this->binary_payload_items_[index].binary_sensor_->state) {
        byte |= (1 << bit);
      }
    }
    bin_bytes.push_back(byte);
  }
  
  return bin_bytes;
}

std::vector<uint8_t> LoRaBridge::pack_text_sensors() {
  std::vector<uint8_t> text_bytes;
  
  for (auto &item : this->text_payload_items_) {
    std::string text = item.text_sensor_->get_state();
    size_t len = text.length();

    if (len > 255) {
      ESP_LOGW(TAG, "Text-Sensor '%s' überschreitet die maximale Länge von 255 Bytes. Es wird abgeschnitten.", item.text_sensor_->get_name().c_str());
      len = 255;
      text = text.substr(0, 255);
    }

    text_bytes.push_back(static_cast<uint8_t>(len));  // Länge als vorangestelltes Byte
    for (size_t i = 0; i < len; ++i) {
      text_bytes.push_back(static_cast<uint8_t>(text[i]));
    }
  }
  
  return text_bytes;
}

void LoRaBridge::setup() {
  ESP_LOGI(TAG, "Setup der LoRaBridge gestartet");
  
  state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    ESP_LOGW(TAG, "Initialisierung des Radio fehlgeschlagen, state: %s (%d)", stateDecode(state).c_str(), state);
  }

  state = node.beginOTAA(join_eui_, dev_eui_, nullptr, app_key_.data());
  if (state != RADIOLIB_ERR_NONE) {
    ESP_LOGW(TAG, "Initialisierung des Node fehlgeschlagen, state: %s (%d)", stateDecode(state).c_str(), state);
  }

  xTaskCreatePinnedToCore(
    joinLoRaWanTask,
    "LoRaWAN Task",
    8192,
    this,
    1,
    &join_lora_wan_task_handle_,
    1
  );
}

void LoRaBridge::loop() {
  // Der Hauptloop bleibt leer, da die Arbeit im separaten Task erledigt wird
}

void LoRaBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "LoRaBridge:");
}

void LoRaBridge::joinLoRaWanTask(void *pvParameters) {
  LoRaBridge *self = static_cast<LoRaBridge *>(pvParameters);
  uint8_t attempt = 0;
  bool joined = false;

  while (!joined && (self->MAX_JOIN_ATTEMPTS == 0 || attempt < self->MAX_JOIN_ATTEMPTS)) {
    attempt++;
    ESP_LOGI(TAG, "Versuch %u dem LoRaWAN beizutreten...", attempt);
    self->state = self->node.activateOTAA();
    if (self->state == RADIOLIB_LORAWAN_NEW_SESSION) {
      ESP_LOGI(TAG, "Join mit dem LoRaWAN erfolgreich");
      joined = true;
    } else {
      ESP_LOGD(TAG, "Join ist fehlgeschlagen, state: %s (%d)", self->stateDecode(self->state).c_str(), self->state);
      vTaskDelay(self->JOIN_DELAY_MS / portTICK_PERIOD_MS);
    }
  }

  if (joined) {
    while (true) {
      ESP_LOGI(TAG, "Sende Uplink...");

        // 1) Gesamt-Payload-Größe berechnen
        size_t total_size = 0;

        // Größe für normale Sensoren
        for (auto &item : self->sensor_payload_items_) {
            total_size += item.bytes_;
        }

        // Größe für Binary-Sensoren
        size_t num_binary_sensors = self->binary_payload_items_.size();
        size_t binary_bytes = (num_binary_sensors + 7) / 8; // Aufrunden auf das nächste Byte
        total_size += binary_bytes;

        // Größe für Text-Sensoren
        size_t text_bytes = 0;
        for (auto &item : self->text_payload_items_) {
            std::string text = item.text_sensor_->get_state();
            size_t len = text.length();
            if (len > 255) len = 255;  // Maximale Länge
            text_bytes += 1 + len;     // 1 Byte für die Länge + String Bytes
        }
        total_size += text_bytes;

        // Optional: Maximale Payload-Größe prüfen
        const size_t MAX_PAYLOAD_SIZE = 51;
        if (total_size > MAX_PAYLOAD_SIZE) {
            ESP_LOGE(TAG, "Payload-Größe überschreitet das Maximum von %d Bytes!", MAX_PAYLOAD_SIZE);
            // Optional: Reduziere die Datenmenge oder überspringe das Senden
            vTaskDelay(pdMS_TO_TICKS(self->uplink_interval_ * 1000));
            continue;
        }

        // 2) Payload-Buffer anlegen
        std::vector<uint8_t> payload(total_size, 0);
        size_t index = 0;

        // 3) Jeden Eintrag der normalen Sensoren verarbeiten
        for (auto &item : self->sensor_payload_items_) {
            float raw_val = 0.0f;
            if (item.sensor_ != nullptr) {
                raw_val = item.sensor_->state;
                ESP_LOGD(TAG, "Sensor %s Rohwert: %f", item.sensor_->get_name().c_str(), raw_val);
            } else {
                ESP_LOGW(TAG, "Ein Sensor ist nicht vorhanden.");
            }

            float scaled_val = raw_val * item.multiplier_ + item.offset_;
            int32_t i_val = static_cast<int32_t>(scaled_val);
            
            // Big-Endian
            for (int b = 0; b < item.bytes_; b++) {
                payload[index + b] = (i_val >> (8 * (item.bytes_ - 1 - b))) & 0xFF;
            }
            index += item.bytes_;
        }

        // 4) Binary-Sensoren verarbeiten und hinzufügen
        if (binary_bytes > 0) {
            std::vector<uint8_t> bin_bytes = self->pack_binary_sensors();
            for (size_t b = 0; b < bin_bytes.size(); ++b) {
                payload[index + b] = bin_bytes[b];
            }
            index += bin_bytes.size();
        }

        // 5) Text-Sensoren verarbeiten und hinzufügen
        if (text_bytes > 0) {
            std::vector<uint8_t> txt_bytes = self->pack_text_sensors();
            for (size_t b = 0; b < txt_bytes.size(); ++b) {
                payload[index + b] = txt_bytes[b];
            }
            index += txt_bytes.size();
        }

      // 6) Sende den Payload über LoRaWAN
      int16_t send_state = self->node.sendReceive(payload.data(), payload.size(), 2);
      if (send_state > 0) {
        ESP_LOGI(TAG, "Downlink empfangen");
      }

      // 7) Warte das Uplink-Intervall ab
      vTaskDelay(self->uplink_interval_ * 1000UL / portTICK_PERIOD_MS);
    }
  }
}

String LoRaBridge::stateDecode(const int16_t result) {
  switch (result) {
    case RADIOLIB_ERR_NONE:
      return "ERR_NONE";
    // ... [restliche Fälle]
    default:
      return "Unbekannter Fehler. Siehe https://jgromes.github.io/RadioLib/group__status__codes.html";
  }
}

}  // namespace lorabridge
}  // namespace esphome
