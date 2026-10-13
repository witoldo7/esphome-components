#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/button/button.h"
#include "inverter_iap.h"

namespace esphome {
namespace inverter_iap {

// Akcje przycisku.
enum IapButtonAction : uint8_t {
  ACTION_UPDATE = 0,  // pełna aktualizacja (zapis tylko gdy allow_update: true)
  ACTION_ABORT = 1,   // przerwij trwającą aktualizację
  ACTION_QUERY = 2,   // test łączności (QueryInfo) — UWAGA: wchodzi w bootloader
  ACTION_RESET = 3,   // czysty Reset (0x05) — wyjście z bootloadera do aplikacji
};

class InverterIAPButton : public button::Button, public Parented<InverterIAP> {
 public:
  void set_action(uint8_t action) { this->action_ = action; }

 protected:
  void press_action() override {
    switch (this->action_) {
      case ACTION_ABORT:
        this->parent_->abort_update();
        break;
      case ACTION_QUERY:
        this->parent_->start_update(true);
        break;
      case ACTION_RESET:
        this->parent_->reset_to_app();
        break;
      case ACTION_UPDATE:
      default:
        this->parent_->start_update(false);
        break;
    }
  }

  uint8_t action_{ACTION_UPDATE};
};

}  // namespace inverter_iap
}  // namespace esphome
