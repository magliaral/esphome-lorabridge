#include <Arduino.h>
#include <EEPROM.h>
#include <RadioLib.h>

#include "config.h"

// EEPROM-Adresse für DevNonce (2 Bytes)
#define DEVNONCE_EEPROM_ADDR 0

// Definitionen für maximale Join-Versuche und Verzögerung zwischen Versuchen
#define MAX_JOIN_ATTEMPTS    0      // Setzen Sie auf 0 für unbegrenzte Versuche
#define JOIN_DELAY_MS        30000   // 15 Sekunden

// Funktion zum Lesen des DevNonce aus EEPROM
uint16_t readDevNonce() {
  uint16_t storedDevNonce = 0;
  EEPROM.get(DEVNONCE_EEPROM_ADDR, storedDevNonce);
  return storedDevNonce;
}

// Funktion zum Schreiben des DevNonce in EEPROM
void writeDevNonce(uint16_t newDevNonce) {
  EEPROM.put(DEVNONCE_EEPROM_ADDR, newDevNonce);
  #if defined(ESP8266) || defined(ESP32)
    EEPROM.commit(); // Sicherstellen, dass die Daten geschrieben werden (nur bei ESP)
  #endif
}

// Beispielhafte Definition der `debug` Funktion
void debug(bool condition, const __FlashStringHelper* message, int16_t state, bool halt) {
  if (condition) {
    Serial.print(F("Error: "));
    Serial.println(message);
    Serial.print(F("State: "));
    Serial.println(state);
    if (halt) {
      while (1); // Endlosschleife, um das Programm anzuhalten
    }
  }
}

// Variable für DevNonce
uint16_t devNonce = 0;

void setup() {
  Serial.begin(115200);
  while(!Serial);
  delay(5000);  // Zeit zum Wechseln zum Serial Monitor
  Serial.println(F("\nSetup ... "));

  // EEPROM initialisieren
  #if defined(ESP8266) || defined(ESP32)
    EEPROM.begin(512); // Größe anpassen, falls nötig
  #endif

  // DevNonce aus EEPROM lesen
  devNonce = readDevNonce();
  Serial.print(F("Geladener DevNonce: "));
  Serial.println(devNonce);

  Serial.println(F("Initialise the radio"));
  int16_t state = radio.begin();
  debug(state != RADIOLIB_ERR_NONE, F("Initialise radio failed"), state, true);

  // Setup der OTAA-Sitzungsinformationen
  state = node.beginOTAA(joinEUI, devEUI, NULL, appKey);
  debug(state != RADIOLIB_ERR_NONE, F("Initialise node failed"), state, true);

  Serial.println(F("Join ('login') the LoRaWAN Network"));

  bool joined = false;
  int attempt = 0;

  // Schleife zum wiederholten Versuch, dem Netzwerk beizutreten
  while (!joined) {
    Serial.print(F("Attempting to join... Versuch "));
    Serial.print(attempt + 1);
    if (MAX_JOIN_ATTEMPTS > 0) {
      Serial.print(F(" von "));
      Serial.println(MAX_JOIN_ATTEMPTS);
    } else {
      Serial.println(F(" (unbegrenzte Versuche)"));
    }

    state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
      Serial.println(F("Join erfolgreich"));

      // DevNonce nur bei erfolgreichem Join inkrementieren und speichern
      devNonce++;
      writeDevNonce(devNonce);
      Serial.print(F("Neuer DevNonce gespeichert: "));
      Serial.println(devNonce);

      joined = true;
    } else {
      Serial.print(F("Join fehlgeschlagen, state: "));
      Serial.println(state);
      attempt++;

      if (MAX_JOIN_ATTEMPTS > 0 && attempt >= MAX_JOIN_ATTEMPTS) {
        Serial.println(F("Maximale Anzahl der Join-Versuche erreicht. Neustart..."));
        ESP.restart(); // Neustart des Geräts oder andere Fehlerbehandlung
      }

      Serial.print(F("Warte "));
      Serial.print(JOIN_DELAY_MS / 1000);
      Serial.println(F(" Sekunden vor dem nächsten Versuch."));
      delay(JOIN_DELAY_MS);
    }
  }

  Serial.println(F("Ready!\n"));
}

void loop() {
  Serial.println(F("Sending uplink"));

  // Beispielhafte Sensor-Daten (hier zufällige Werte)
  uint8_t value1 = radio.random(100);
  uint16_t value2 = radio.random(2000);

  // Aufbau des Payload-Byte-Arrays
  uint8_t uplinkPayload[3];
  uplinkPayload[0] = value1;
  uplinkPayload[1] = highByte(value2);   // Höheres Byte von value2
  uplinkPayload[2] = lowByte(value2);    // Niedrigeres Byte von value2
  
  // Durchführung eines Uplinks mit bis zu 2 Wiederholungen
  int16_t state = node.sendReceive(uplinkPayload, sizeof(uplinkPayload), 2);    
  debug(state < RADIOLIB_ERR_NONE, F("Error in sendReceive"), state, false);

  // Überprüfung, ob ein Downlink empfangen wurde 
  // (state 0 = kein Downlink, state 1/2 = Downlink in Fenster Rx1/Rx2)
  if(state > 0) {
    Serial.println(F("Received a downlink"));
  } else {
    Serial.println(F("No downlink received"));
  }

  Serial.print(F("Next uplink in "));
  Serial.print(uplinkIntervalSeconds);
  Serial.println(F(" seconds\n"));
  
  // Warten bis zum nächsten Uplink - unter Berücksichtigung der gesetzlichen und TTN FUP-Richtlinien
  delay(uplinkIntervalSeconds * 1000UL);  // delay benötigt Millisekunden
}