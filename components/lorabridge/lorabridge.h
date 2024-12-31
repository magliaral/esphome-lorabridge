#pragma once

#include "esphome.h"
#include "config.h"

namespace esphome {
namespace lorabridge {

class Lorabridge : public Component {
 public:
  void setup() override;
  void loop() override;

  // Optional: Fügen Sie hier Konfigurationsparameter hinzu, z.B.:
  // std::string some_parameter_;
  // void set_some_parameter(std::string parameter) { this->some_parameter_ = parameter; }
};

}  // namespace lorabridge
}  // namespace esphome

// Definieren des CONFIG_SCHEMA für die Komponente
#include "lorabridge.h"
#include "esphome/core/component.h"

namespace esphome {
namespace lorabridge {

static const auto LORABRIDGE_SCHEMA = 
    esphome::config::make_schema<lorabridge::Lorabridge>();

}  // namespace lorabridge
}  // namespace esphome
