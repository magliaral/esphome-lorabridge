#include "esphome/core/log.h"
#include "lorabridge.h"
#include "Arduino.h"

namespace esphome {
namespace lorabridge {

static const char *TAG = "lorabridge.component";

void LoRaBridge::setup() {
    ESP_LOGI(TAG, "Setup der LoRaBridge online gestartet");
    // Ihr Setup-Code hier
}

void LoRaBridge::loop() {
    static unsigned long last_log_time = 0; // Speichert die Zeit des letzten Log-Eintrags
    unsigned long current_time = millis();   // Aktuelle Zeit in Millisekunden seit dem Start

    // Prüfen, ob seit dem letzten Log-Eintrag mehr als 15000 ms (15 Sekunden) vergangen sind
    if (current_time - last_log_time > 15000) { // 15000 ms = 15 Sekunden
        ESP_LOGI(TAG, "Loop der LoRaBridge online läuft seit %lu ms", current_time);
        last_log_time = current_time; // Aktualisieren der letzten Log-Zeit
    }

    // Beispiel für zusätzliche Logik
    if (state > 0) {
        // Ihre Logik hier
    } else {
        // Ihre Logik hier
    }
}

void LoRaBridge::dump_config(){
    ESP_LOGCONFIG(TAG, "LoRaBridge");
}

}  // namespace lorabridge
}  // namespace esphome
