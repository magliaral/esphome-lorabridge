#pragma once

#include <RadioLib.h>
#include <Arduino.h>
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lorabridge {

static const char *HAL_TAG = "lorabridge.hal";

// Custom RadioLib HAL that uses the ESP-IDF SPI driver directly.
// This ensures proper bus arbitration with ESPHome's display SPI driver,
// which also uses the IDF SPI master on the same SPI2 bus.
class ESPHomeSPIHal : public RadioLibHal {
 public:
  ESPHomeSPIHal(uint32_t cs_pin, spi_host_device_t host = SPI2_HOST, uint32_t spi_speed = 8000000)
      : RadioLibHal(INPUT, OUTPUT, LOW, HIGH, RISING, FALLING),
        cs_pin_(cs_pin), host_(host), spi_speed_(spi_speed) {}

  void init() override {
    // RadioLib calls init() internally — guard against double registration
    if (dev_) return;

    spi_device_interface_config_t cfg = {};
    cfg.mode = 0;
    cfg.clock_speed_hz = spi_speed_;
    cfg.spics_io_num = -1;  // CS managed manually by RadioLib
    cfg.queue_size = 1;
    cfg.flags = 0;
    esp_err_t ret = spi_bus_add_device(host_, &cfg, &dev_);
    if (ret != ESP_OK) {
      ESP_LOGE(HAL_TAG, "spi_bus_add_device failed: %s", esp_err_to_name(ret));
    } else {
      ESP_LOGI(HAL_TAG, "SPI device added at %u Hz", spi_speed_);
    }
  }

  void term() override {
    if (dev_) {
      spi_bus_remove_device(dev_);
      dev_ = nullptr;
    }
  }

  void pinMode(uint32_t pin, uint32_t mode) override {
    if (pin == RADIOLIB_NC) return;
    ::pinMode(pin, mode);
  }

  void digitalWrite(uint32_t pin, uint32_t value) override {
    if (pin == RADIOLIB_NC) return;
    // Acquire/release bus when CS toggles to prevent display SPI conflicts
    if (pin == cs_pin_ && dev_) {
      if (value == LOW && !bus_acquired_) {
        spi_device_acquire_bus(dev_, portMAX_DELAY);
        bus_acquired_ = true;
      }
    }
    ::digitalWrite(pin, value);
    if (pin == cs_pin_ && dev_) {
      if (value == HIGH && bus_acquired_) {
        spi_device_release_bus(dev_);
        bus_acquired_ = false;
      }
    }
  }

  uint32_t digitalRead(uint32_t pin) override {
    if (pin == RADIOLIB_NC) return 0;
    return ::digitalRead(pin);
  }

  void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override {
    if (interruptNum == RADIOLIB_NC) return;
    ::attachInterrupt(interruptNum, interruptCb, mode);
  }

  void detachInterrupt(uint32_t interruptNum) override {
    if (interruptNum == RADIOLIB_NC) return;
    ::detachInterrupt(interruptNum);
  }

  void delay(unsigned long ms) override {
    vTaskDelay(pdMS_TO_TICKS(ms));
  }

  void delayMicroseconds(unsigned long us) override {
    ::delayMicroseconds(us);
  }

  unsigned long millis() override {
    return ::millis();
  }

  unsigned long micros() override {
    return ::micros();
  }

  long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override {
    return ::pulseIn(pin, state, timeout);
  }

  void spiBegin() override {}

  void spiBeginTransaction() override {}

  void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override {
    if (len == 0 || !dev_) return;

    // Byte-by-byte transfer using internal 4-byte buffer (no DMA).
    // This guarantees correct data regardless of DMA/PSRAM issues.
    for (size_t i = 0; i < len; i++) {
      spi_transaction_t t = {};
      t.length = 8;
      t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
      t.tx_data[0] = out ? out[i] : 0x00;

      esp_err_t ret = spi_device_polling_transmit(dev_, &t);
      if (ret != ESP_OK) {
        ESP_LOGE(HAL_TAG, "SPI byte transmit failed: %s", esp_err_to_name(ret));
        return;
      }
      if (in) in[i] = t.rx_data[0];
    }
  }

  void spiEndTransaction() override {}

  void spiEnd() override {}

  void tone(uint32_t pin, unsigned int frequency, unsigned long duration = 0) override {}
  void noTone(uint32_t pin) override {}
  void yield() override { ::yield(); }

 private:
  uint32_t cs_pin_;
  spi_host_device_t host_;
  uint32_t spi_speed_;
  spi_device_handle_t dev_{nullptr};
  bool bus_acquired_{false};
};

}  // namespace lorabridge
}  // namespace esphome
