#include "esphome/core/log.h"
#include "lorabridge.h"

namespace esphome {
namespace lorabridge {

static const char *TAG = "lorabridge.component";

void LoRaBridge::setup() {
    ESP_LOGI(TAG, "Setup der LoRaBridge gestartet");
    // Ihr Setup-Code hier
}

void LoRaBridge::loop() {
    static unsigned long last_log_time = 0; // Speichert die Zeit des letzten Log-Eintrags
    unsigned long current_time = millis();   // Aktuelle Zeit in Millisekunden seit dem Start

    // Prüfen, ob seit dem letzten Log-Eintrag mehr als 5000 ms (5 Sekunden) vergangen sind
    if (current_time - last_log_time > 15000) { // 15000 ms = 5 Sekunden
        ESP_LOGI(TAG, "Loop der LoRaBridge läuft seit %lu ms", current_time);
        last_log_time = current_time; // Aktualisieren der letzten Log-Zeit
    }
}

void LoRaBridge::dump_config(){
    ESP_LOGCONFIG(TAG, "LoRaBridge");
}


}  // namespace lorabridge
}  // namespace esphome

  // Überprüfung, ob ein Downlink empfangen wurde 
  if(state > 0) {
    ESP_LOGI(TAG, "Downlink empfangen");
  } else {
    ESP_LOGI(TAG, "Kein Downlink empfangen");
  }

  ESP_LOGI(TAG, "Nächstes Uplink in %u Sekunden\n", uplinkIntervalSeconds);
  
  delay(uplinkIntervalSeconds * 1000UL);  // Warten bis zum nächsten Uplink
}

}  // namespace lorabridge
}  // namespace esphome
