#include "lorabridge.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lorabridge {

static const char *TAG = "lorabridge.component";

TaskHandle_t join_lora_wan_task_handle_;

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
      ESP_LOGI(TAG, "Sende Uplink");

      uint8_t value1 = self->radio.random(100);
      uint16_t value2 = self->radio.random(2000);

      uint8_t uplinkPayload[3] = { value1, highByte(value2), lowByte(value2) };

      int16_t send_state = self->node.sendReceive(uplinkPayload, sizeof(uplinkPayload), 2);

      if (send_state > 0) {
        ESP_LOGI(TAG, "Downlink empfangen");
      } else {
        //ESP_LOGI(TAG, "Kein Downlink empfangen");
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
