#include "lorabridge.h"

namespace esphome {
namespace lorabridge {

void Lorabridge::setup() {
  ESP_LOGI("lorabridge", "Starte Setup...");

  // EEPROM initialisieren
  #if defined(ESP8266) || defined(ESP32)
    EEPROM.begin(512); // Größe anpassen, falls nötig
  #endif

  // DevNonce aus EEPROM lesen
  devNonce = readDevNonce();
  ESP_LOGI("lorabridge", "Geladener DevNonce: %u", devNonce);

  // Radio initialisieren
  int16_t state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    ESP_LOGE("lorabridge", "Radio-Initialisierung fehlgeschlagen, Zustand: %d", state);
    while (1) { delay(1); } // Endlosschleife zur Fehlerbehandlung
  }

  // OTAA-Sitzungsinformationen einrichten
  state = node.beginOTAA(joinEUI, devEUI, NULL, appKey);
  if (state != RADIOLIB_ERR_NONE) {
    ESP_LOGE("lorabridge", "Node-Initialisierung fehlgeschlagen, Zustand: %d", state);
    while (1) { delay(1); } // Endlosschleife zur Fehlerbehandlung
  }

  ESP_LOGI("lorabridge", "Beitreten zum LoRaWAN-Netzwerk");

  bool joined = false;
  int attempt = 0;

  // Schleife zum Versuch, dem Netzwerk beizutreten
  while (!joined) {
    ESP_LOGI("lorabridge", "Versuche beizutreten... Versuch %d", attempt + 1);
    if (MAX_JOIN_ATTEMPTS > 0) {
      ESP_LOGI("lorabridge", "von %d", MAX_JOIN_ATTEMPTS);
    } else {
      ESP_LOGI("lorabridge", "(unbegrenzte Versuche)");
    }

    state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
      ESP_LOGI("lorabridge", "Beitritt erfolgreich");

      // DevNonce nur bei erfolgreichem Beitritt inkrementieren und speichern
      devNonce++;
      writeDevNonce(devNonce);
      ESP_LOGI("lorabridge", "Neuer DevNonce gespeichert: %u", devNonce);

      joined = true;
    } else {
      ESP_LOGE("lorabridge", "Beitritt fehlgeschlagen, Zustand: %d", state);
      attempt++;

      if (MAX_JOIN_ATTEMPTS > 0 && attempt >= MAX_JOIN_ATTEMPTS) {
        ESP_LOGE("lorabridge", "Maximale Anzahl der Join-Versuche erreicht. Neustart...");
        ESP.restart(); // Neustart des Geräts oder andere Fehlerbehandlung
      }

      ESP_LOGI("lorabridge", "Warte %d Sekunden vor dem nächsten Versuch.", JOIN_DELAY_MS / 1000);
      delay(JOIN_DELAY_MS);
    }
  }

  ESP_LOGI("lorabridge", "Bereit!\n");
}

void Lorabridge::loop() {
  ESP_LOGI("lorabridge", "Sende Uplink");

  // Beispielhafte Sensor-Daten (hier zufällige Werte)
  uint8_t value1 = radio.random(100);
  uint16_t value2 = radio.random(2000);

  // Aufbau des Payload-Byte-Arrays
  uint8_t uplinkPayload[3];
  uplinkPayload[0] = value1;
  uplinkPayload[1] = highByte(value2);
  uplinkPayload[2] = lowByte(value2);
  
  // Durchführung eines Uplinks mit bis zu 2 Wiederholungen
  int16_t state = node.sendReceive(uplinkPayload, sizeof(uplinkPayload), 2);    
  if(state < RADIOLIB_ERR_NONE) {
    ESP_LOGE("lorabridge", "Fehler bei sendReceive, Zustand: %d", state);
  }

  // Überprüfung, ob ein Downlink empfangen wurde 
  if(state > 0) {
    ESP_LOGI("lorabridge", "Downlink empfangen");
  } else {
    ESP_LOGI("lorabridge", "Kein Downlink empfangen");
  }

  ESP_LOGI("lorabridge", "Nächstes Uplink in %u Sekunden\n", uplinkIntervalSeconds);
  
  delay(uplinkIntervalSeconds * 1000UL);  // Warten bis zum nächsten Uplink
}

}  // namespace lorabridge
}  // namespace esphome
