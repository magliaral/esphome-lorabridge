#include "lorabridge.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cmath>

namespace esphome {
namespace lorabridge {

static const char *TAG = "lorabridge.component";

// --- Payload management ---

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
      ESP_LOGW(TAG, "Text sensor '%s' exceeds 255 bytes, truncating",
               item.text_sensor_->get_name().c_str());
      len = 255;
      text = text.substr(0, 255);
    }

    text_bytes.push_back(static_cast<uint8_t>(len));
    for (size_t i = 0; i < len; ++i) {
      text_bytes.push_back(static_cast<uint8_t>(text[i]));
    }
  }

  return text_bytes;
}

// --- Nonce persistence ---

void LoRaBridge::save_nonces_() {
  NoncesBuffer buf;
  memcpy(buf.data(), this->node_->getBufferNonces(), buf.size());
  this->nonces_pref_.save(&buf);
  global_preferences->sync();
}

// --- Factory: creates the correct RadioLib type and calls begin() ---

#define TRY_CHIP(cls) \
  if (chip_ == #cls) { \
    auto *r = new cls(mod); \
    state = r->begin(); \
    return r; \
  }

PhysicalLayer *LoRaBridge::createRadio(Module *mod, int16_t &state) {
  TRY_CHIP(SX1261)
  TRY_CHIP(SX1262)
  TRY_CHIP(SX1268)
  TRY_CHIP(SX1276)
  TRY_CHIP(SX1277)
  TRY_CHIP(SX1278)
  TRY_CHIP(SX1279)
  TRY_CHIP(SX1272)
  TRY_CHIP(LR1110)
  TRY_CHIP(LR1120)
  TRY_CHIP(LR1121)
  ESP_LOGE(TAG, "Unknown chip: %s", chip_.c_str());
  state = -1;
  return nullptr;
}

#undef TRY_CHIP

// --- Setup: start LoRaWAN task ---

void LoRaBridge::setup() {
  ESP_LOGI(TAG, "LoRaBridge: chip=%s, NSS=%d, RST=%d, IRQ=%d, BUSY=%d, GPIO=%d",
           chip_.c_str(), nss_pin_, rst_pin_, irq_pin_, busy_pin_, gpio_pin_);

  // Set all SPI device CS pins HIGH
  pinMode(nss_pin_, OUTPUT);
  digitalWrite(nss_pin_, HIGH);
  pinMode(10, OUTPUT);   // W5500 Ethernet CS
  digitalWrite(10, HIGH);

  // IMPORTANT: No dedicated Arduino SPI bus! The display (ESPHome/IDF) and
  // the radio physically share pins 11/12/13. A second SPI host (HSPI) on
  // the same pins would remap the GPIO matrix and disconnect the display.
  // Instead, a custom HAL registers as a device on the existing IDF bus
  // (SPI2_HOST, initialized by ESPHome's spi: component).
  this->hal_ = new ESPHomeSPIHal(nss_pin_);
  Module *mod = new Module(this->hal_, nss_pin_, irq_pin_, rst_pin_, busy_pin_);

  int16_t state;
  radio_ = createRadio(mod, state);
  if (!radio_ || state != RADIOLIB_ERR_NONE) {
    ESP_LOGE(TAG, "Radio init failed: chip=%s, state=%d", chip_.c_str(), state);
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "%s initialized successfully", chip_.c_str());

  // Configure DIO2 as RF switch (SX126x only)
  if (chip_ == "SX1262" || chip_ == "SX1261" || chip_ == "SX1268") {
    SX126x *sx126x = static_cast<SX126x *>(radio_);
    sx126x->setDio2AsRfSwitch(true);
    ESP_LOGI(TAG, "DIO2 configured as RF switch");
  }

  // LoRaWAN Node
  node_ = new LoRaWANNode(radio_, &region_, sub_band_);

  // LoRaWAN 1.0.x: nwkKey MUST be nullptr, otherwise RadioLib switches to
  // 1.1 key derivation and the session keys won't match a 1.0.4 device in TTN.
  // Only pass nwk_key when it was explicitly configured (LoRaWAN 1.1).
  bool has_nwk_key = false;
  for (uint8_t b : nwk_key_) {
    if (b != 0) { has_nwk_key = true; break; }
  }
  const uint8_t *nwk_key_ptr = has_nwk_key ? nwk_key_.data() : nullptr;
  state = node_->beginOTAA(join_eui_, dev_eui_, nwk_key_ptr, app_key_.data());
  if (state != RADIOLIB_ERR_NONE) {
    ESP_LOGE(TAG, "LoRaWAN node init failed: %d", state);
    this->mark_failed();
    return;
  }

  // Open RX windows earlier: the HAL's byte-wise polling SPI makes radio
  // configuration a few ms slower than RadioLib assumes internally.
  // Without this guard the (join) downlink preamble is missed -> ERR -1116.
  node_->scanGuard = scan_guard_;

  // Restore persisted nonces (DevNonce must increase monotonically across
  // reboots, otherwise TTN rejects the join requests)
  this->nonces_pref_ = global_preferences->make_preference<NoncesBuffer>(
      fnv1_hash("lorabridge_nonces"));
  NoncesBuffer stored;
  if (this->nonces_pref_.load(&stored)) {
    int16_t ns = node_->setBufferNonces(stored.data());
    if (ns == RADIOLIB_ERR_NONE) {
      ESP_LOGI(TAG, "Restored LoRaWAN nonces from flash");
    } else {
      ESP_LOGW(TAG, "Stored nonces rejected (state=%d), starting fresh", ns);
    }
  } else {
    ESP_LOGI(TAG, "No stored nonces found, starting fresh");
  }

  ESP_LOGI(TAG, "LoRaWAN node initialized, starting join task");
  init_done_ = true;

  // Join + uplink in separate task (activateOTAA blocks for ~7s)
  xTaskCreatePinnedToCore(
    [](void *param) {
      LoRaBridge *self = static_cast<LoRaBridge *>(param);

      // Join loop
      while (!self->joined_) {
        ESP_LOGI(TAG, "Join attempt...");
        // Join at the configured data rate. The default DR0 (EU868:
        // SF12/125kHz) maximizes the link budget, adding margin for both
        // the join request AND the join-accept downlink.
        int16_t s = self->node_->activateOTAA(self->join_dr_);
        // Save nonces after EVERY attempt - the DevNonce increments per
        // join request, not only on success.
        self->save_nonces_();
        if (s == RADIOLIB_LORAWAN_NEW_SESSION || s == RADIOLIB_ERR_NONE) {
          ESP_LOGI(TAG, "LoRaWAN join successful! (state=%d)", s);
          self->joined_ = true;
        } else {
          ESP_LOGD(TAG, "Join failed, state: %d", s);
          vTaskDelay(pdMS_TO_TICKS(JOIN_DELAY_MS));
        }
      }

      // Uplink loop
      while (true) {
        vTaskDelay(pdMS_TO_TICKS(self->uplink_interval_ * 1000UL));

        size_t total_size = 0;
        for (auto &item : self->sensor_payload_items_) total_size += item.bytes_;
        size_t binary_bytes = (self->binary_payload_items_.size() + 7) / 8;
        total_size += binary_bytes;
        size_t text_bytes = 0;
        for (auto &item : self->text_payload_items_) {
          size_t len = item.text_sensor_->get_state().length();
          if (len > 255) len = 255;
          text_bytes += 1 + len;
        }
        total_size += text_bytes;

        if (total_size > 51) { ESP_LOGE(TAG, "Payload too large: %d bytes", total_size); continue; }

        std::vector<uint8_t> payload(total_size, 0);
        size_t index = 0;
        for (auto &item : self->sensor_payload_items_) {
          // Encodable range for this width; the minimum (bit pattern
          // 0x80 00..00) is reserved as the "invalid value" sentinel. The
          // decoder drops sentinel fields so HA keeps the last known state.
          const int64_t enc_min = -(int64_t(1) << (8 * item.bytes_ - 1));
          const int64_t enc_max = -enc_min - 1;

          float raw = item.sensor_ ? item.sensor_->state : NAN;
          int64_t val;
          if (!std::isfinite(raw)) {
            val = enc_min;
            ESP_LOGD(TAG, "Sensor '%s' has no valid state, sending invalid marker",
                     item.sensor_ ? item.sensor_->get_name().c_str() : "?");
          } else {
            double scaled = double(raw) * item.multiplier_ + item.offset_;
            val = int64_t(llround(scaled));
            // Clamp instead of wrapping around (e.g. +337 W must not turn
            // into -318 W in a 2-byte field)
            if (val <= enc_min || val > enc_max) {
              ESP_LOGW(TAG, "Sensor '%s' scaled value %.1f exceeds %u-byte range, clamping",
                       item.sensor_->get_name().c_str(), scaled, item.bytes_);
              val = (val <= enc_min) ? enc_min + 1 : enc_max;
            }
          }
          for (int b = 0; b < item.bytes_; b++)
            payload[index + b] = uint8_t((val >> (8 * (item.bytes_ - 1 - b))) & 0xFF);
          index += item.bytes_;
        }
        if (binary_bytes > 0) { auto bin = self->pack_binary_sensors(); for (size_t b = 0; b < bin.size(); ++b) payload[index + b] = bin[b]; index += bin.size(); }
        if (text_bytes > 0) { auto txt = self->pack_text_sensors(); for (size_t b = 0; b < txt.size(); ++b) payload[index + b] = txt[b]; }

        int16_t r = self->node_->sendReceive(payload.data(), payload.size(), 2);
        if (r < 0) {
          ESP_LOGW(TAG, "Uplink failed, state: %d", r);
        } else {
          ESP_LOGI(TAG, "Uplink sent (%d bytes)", payload.size());
          if (r > 0) ESP_LOGI(TAG, "Downlink received");
        }
      }
    },
    "LoRaWAN", 8192, this, 5, nullptr, 1
  );
}

void LoRaBridge::loop() {
}

void LoRaBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "LoRaBridge:");
  ESP_LOGCONFIG(TAG, "  Chip: %s", chip_.c_str());
  ESP_LOGCONFIG(TAG, "  Pins: NSS=%d, RST=%d, IRQ=%d, BUSY=%d, GPIO=%d",
                nss_pin_, rst_pin_, irq_pin_, busy_pin_, gpio_pin_);
}

}  // namespace lorabridge
}  // namespace esphome
