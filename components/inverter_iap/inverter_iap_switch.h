#pragma once

#include "esphome/core/helpers.h"
#include "esphome/components/switch/switch.h"
#include "inverter_iap.h"

namespace esphome {
namespace inverter_iap {

// Przełącznik "force" — gdy ON, bramka wersji jest pomijana (dopuszcza downgrade/tę samą wersję).
class InverterIAPForceSwitch : public switch_::Switch, public Parented<InverterIAP> {
 protected:
  void write_state(bool state) override {
    this->parent_->set_force(state);
    this->publish_state(state);
  }
};

}  // namespace inverter_iap
}  // namespace esphome
