#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace inverter_iap {

// Fazy maszyny stanów aktualizacji (patrz protokol_IAP.md).
enum IapPhase : uint8_t {
  PH_IDLE = 0,
  PH_ENTER,       // faza 1: przełącz aplikację w bootloader
  PH_QUERY,       // QueryInfo + walidacja
  PH_ERASE_SEND,  // wyślij Erase
  PH_ERASE_POLL,  // WaitErase
  PH_PROGRAM,     // Program (bloki po 128 słów)
  PH_VERIFY,      // Verify (suma flash)
  PH_RESET,       // Reset do nowej aplikacji
  PH_DONE,
  PH_FAILED,
};

class InverterIAP : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // --- konfiguracja (z codegen) ---
  void set_address(uint8_t address) { this->address_ = address; }
  void set_object(uint8_t object) { this->object_ = object; }
  void set_allow_update(bool allow) { this->allow_update_ = allow; }
  void set_modbus(Component *modbus) { this->modbus_ = modbus; }
  void set_flow_control_pin(GPIOPin *pin) { this->flow_control_pin_ = pin; }
  void set_progress_sensor(sensor::Sensor *s) { this->progress_sensor_ = s; }
  void set_status_text_sensor(text_sensor::TextSensor *t) { this->status_text_sensor_ = t; }
  void set_debug(bool debug) { this->debug_ = debug; }
  void set_firmware(const uint8_t *data, const uint32_t *addr, uint32_t blocks, uint32_t flash_sum) {
    this->fw_data_ = data;
    this->fw_addr_ = addr;
    this->fw_blocks_ = blocks;
    this->flash_sum_ = flash_sum;
  }

  // --- API publiczne (wywoływane przez przyciski / automatyzacje) ---
  // query_only == true: wejdź w bootloader, odczytaj QueryInfo i zresetuj (bez zapisu).
  void start_update(bool query_only);
  void abort_update();
  // Wyślij czysty Reset (0x05) do bootloadera → powrót do aplikacji (bez flashowania).
  void reset_to_app();
  bool is_busy() const { return this->phase_ != PH_IDLE && this->phase_ != PH_DONE && this->phase_ != PH_FAILED; }

 protected:
  // warstwa ramek
  void send_frame_(const uint8_t *buf, uint16_t len);
  void drain_rx_();
  bool frame_ready_() const;
  void log_hex_(const char *dir, const uint8_t *buf, uint16_t len) const;
  static uint8_t pkt_chk_(const uint8_t *buf, uint16_t len);
  static uint16_t crc16_modbus_(const uint8_t *buf, uint16_t len);
  bool resp_chk_ok_() const;

  // budowa ramek
  uint16_t build_enter_(uint8_t *out) const;
  uint16_t build_qinfo_(uint8_t *out) const;
  uint16_t build_cmd_(uint8_t *out, uint8_t cmd) const;
  uint16_t build_verify_(uint8_t *out) const;
  uint16_t build_program_(uint8_t *out, uint32_t block_index) const;

  // obsługa faz
  void enter_phase_(IapPhase phase);
  void fail_(const char *reason);
  void finish_(const char *reason);
  void publish_status_(const std::string &s);
  void publish_progress_(float pct);

  // konfiguracja
  uint8_t address_{1};
  uint8_t object_{0};  // 0 = App, 1 = Boot
  bool allow_update_{false};
  bool debug_{false};
  Component *modbus_{nullptr};
  GPIOPin *flow_control_pin_{nullptr};
  sensor::Sensor *progress_sensor_{nullptr};
  text_sensor::TextSensor *status_text_sensor_{nullptr};

  // osadzony obraz
  const uint8_t *fw_data_{nullptr};
  const uint32_t *fw_addr_{nullptr};
  uint32_t fw_blocks_{0};
  uint32_t flash_sum_{0};

  // stan maszyny
  IapPhase phase_{PH_IDLE};
  bool query_only_{false};
  bool req_sent_{false};
  uint32_t phase_deadline_ms_{0};
  uint32_t last_rx_ms_{0};
  uint16_t attempt_{0};
  uint32_t block_i_{0};
  uint16_t resume_block_{0};
  uint8_t hs_step_{0};            // krok handshake: 0,1=Start; 2,3,4=QInfo (jak IAPTool)
  uint32_t handshake_end_ms_{0};  // twardy limit czasu handshake

  // bufor odbiorczy
  uint8_t rx_[300];
  uint16_t rx_len_{0};
  bool rx_logged_{false};
};

}  // namespace inverter_iap
}  // namespace esphome
