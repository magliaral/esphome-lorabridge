#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace lorabridge {

class LoRaBridge : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
};


}  // namespace lorabridge
}  // namespace esphome