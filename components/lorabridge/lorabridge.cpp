#include "lorabridge.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lorabridge {

static const char *TAG = "lorabridge.component";

TaskHandle_t join_lora_wan_task_handle_;

void LoRaBridge::add_payload_item(sensor::Sensor *sens, float multiplier, float offset, uint8_t bytes) {
  PayloadItem item;
  item.sensor_ = sens;
  item.multiplier_ = multiplier;
  item.offset_ = offset;
  item.bytes_ = bytes;
  this->payload_items_.push_back(item);
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
    "Join LoRaWAN Task",
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

      // 1) Gesamt-Payload-Größe
      size_t total_size = 0;
      for (auto &item : self->payload_items_) {
        total_size += item.bytes_;
      }

      // 2) Payload-Buffer anlegen
      std::vector<uint8_t> payload(total_size, 0);
      size_t index = 0;

      // 3) Jeden Eintrag verarbeiten
      for (auto &item : self->payload_items_) {
        float raw_val = 0.0f;
        if (item.sensor_ != nullptr) {
          raw_val = item.sensor_->state;
        } else {
          // Sensor nicht vorhanden
        }
        float scaled_val = raw_val * item.multiplier_ + item.offset_;
        int32_t i_val = static_cast<int32_t>(scaled_val);

        // Werte < 0 clampen? => if (i_val < 0) i_val = 0;

        // Big-Endian
        for (int b = 0; b < item.bytes_; b++) {
          payload[index + b] = (i_val >> (8 * (item.bytes_ - 1 - b))) & 0xFF;
        }
        index += item.bytes_;
      }

      // 4) Senden
      int16_t send_state = self->node.sendReceive(payload.data(), payload.size(), 2);
      if (send_state > 0) {
        ESP_LOGI(TAG, "Downlink empfangen");
      }

      ESP_LOGI(TAG, "Nächster Uplink in %u Sekunden", self->uplink_interval_);
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
