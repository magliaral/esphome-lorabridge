#pragma once

#include "esphome.h"
#include "config.h"

namespace esphome {
namespace lorabridge {

class Lorabridge : public Component {
 public:
  void setup() override;
  void loop() override;

  // Fügen Sie hier weitere Methoden oder Eigenschaften hinzu, falls benötigt
};

}  // namespace lorabridge
}  // namespace esphome
