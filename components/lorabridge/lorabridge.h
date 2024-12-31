#pragma once

#include "esphome.h"
#include "config.h"

namespace esphome {
namespace lorabridge {

class Lorabridge : public Component {
 public:
  void setup() override;
  void loop() override;

  // Optional: Fügen Sie hier weitere Methoden oder Eigenschaften hinzu
};

}  // namespace lorabridge
}  // namespace esphome
