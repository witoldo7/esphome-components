#include "inverter_iap.h"
#include "esphome/core/log.h"
#include <cstdio>
#include <cstring>

namespace esphome {
namespace inverter_iap {

static const char *const TAG = "inverter_iap";

static const uint8_t F8 = 0xF8;
static const uint32_t IDLE_MS = 25;  // przerwa międzyramkowa (>3.5 znaku przy 9600 ≈ 4 ms)

// --- sumy kontrolne ---------------------------------------------------------
uint8_t InverterIAP::pkt_chk_(const uint8_t *buf, uint16_t len) {
  uint32_t s = 0;
  for (uint16_t i = 0; i < len; i++)
    s += buf[i];
  return static_cast<uint8_t>((~s + 1) & 0xFF);  // uzupełnienie do dwóch
}

uint16_t InverterIAP::crc16_modbus_(const uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
  }
  return crc;
}

// --- budowa ramek -----------------------------------------------------------
uint16_t InverterIAP::build_enter_(uint8_t *out) const {
  out[0] = this->address_;
  out[1] = 0xB0;  // StartCmd
  out[2] = 0x23;  // StartCmdParam = 0x2300
  out[3] = 0x00;
  uint16_t crc = crc16_modbus_(out, 4);
  out[4] = crc & 0xFF;         // młodszy bajt pierwszy — standardowy Modbus (jak IAPTool)
  out[5] = (crc >> 8) & 0xFF;  // starszy bajt
  return 6;
}

uint16_t InverterIAP::build_cmd_(uint8_t *out, uint8_t cmd) const {
  out[0] = F8;
  out[1] = cmd;
  out[2] = 0x00;
  out[3] = 0x00;
  out[4] = pkt_chk_(out, 4);
  return 5;
}

uint16_t InverterIAP::build_qinfo_(uint8_t *out) const {
  out[0] = F8;
  out[1] = 0x00;
  out[2] = 0x00;
  out[3] = 0x04;
  out[4] = 0x23;  // devicetype hi
  out[5] = 0x00;  // devicetype lo
  out[6] = 0x00;  // obj hi
  out[7] = this->object_;
  out[8] = pkt_chk_(out, 8);
  return 9;
}

uint16_t InverterIAP::build_verify_(uint8_t *out) const {
  out[0] = F8;
  out[1] = 0x04;
  out[2] = 0x00;
  out[3] = 0x04;
  out[4] = (this->flash_sum_ >> 24) & 0xFF;
  out[5] = (this->flash_sum_ >> 16) & 0xFF;
  out[6] = (this->flash_sum_ >> 8) & 0xFF;
  out[7] = this->flash_sum_ & 0xFF;
  out[8] = pkt_chk_(out, 8);
  return 9;
}

uint16_t InverterIAP::build_program_(uint8_t *out, uint32_t block_index) const {
  uint32_t base = this->fw_addr_[block_index];
  out[0] = F8;
  out[1] = 0x03;
  out[2] = 0x01;  // Len = 256 + 4 = 0x0104
  out[3] = 0x04;
  out[4] = (base >> 24) & 0xFF;
  out[5] = (base >> 16) & 0xFF;
  out[6] = (base >> 8) & 0xFF;
  out[7] = base & 0xFF;
  memcpy(out + 8, this->fw_data_ + block_index * 256, 256);
  out[264] = pkt_chk_(out, 264);
  return 265;
}

// --- warstwa szeregowa ------------------------------------------------------
void InverterIAP::send_frame_(const uint8_t *buf, uint16_t len) {
  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(true);
  this->write_array(buf, len);
  if (this->flow_control_pin_ != nullptr) {
    this->flush();  // czekaj na opróżnienie TX zanim opuścisz DE (RS485)
    this->flow_control_pin_->digital_write(false);
  }
  if (this->debug_)
    this->log_hex_("TX", buf, len);
  this->rx_len_ = 0;
  this->last_rx_ms_ = 0;
  this->rx_logged_ = false;
}

void InverterIAP::log_hex_(const char *dir, const uint8_t *buf, uint16_t len) const {
  char line[3 * 48 + 4];
  uint16_t show = len > 48 ? 48 : len;
  uint16_t p = 0;
  for (uint16_t i = 0; i < show; i++)
    p += snprintf(line + p, sizeof(line) - p, "%02X ", buf[i]);
  ESP_LOGI(TAG, "%s (%u B): %s%s", dir, len, line, len > show ? "..." : "");
}

void InverterIAP::drain_rx_() {
  uint8_t b;
  while (this->available()) {
    if (!this->read_byte(&b))
      break;
    if (this->rx_len_ < sizeof(this->rx_))
      this->rx_[this->rx_len_++] = b;
    this->last_rx_ms_ = millis();
  }
}

bool InverterIAP::frame_ready_() const {
  return this->rx_len_ > 0 && (millis() - this->last_rx_ms_) > IDLE_MS;
}

bool InverterIAP::resp_chk_ok_() const {
  return this->rx_len_ >= 2 && this->rx_[this->rx_len_ - 1] == pkt_chk_(this->rx_, this->rx_len_ - 1);
}

// --- sterowanie fazami ------------------------------------------------------
void InverterIAP::enter_phase_(IapPhase phase) {
  this->phase_ = phase;
  this->req_sent_ = false;
  this->attempt_ = 0;
  this->rx_len_ = 0;
  this->last_rx_ms_ = 0;
}

void InverterIAP::publish_status_(const std::string &s) {
  if (this->status_text_sensor_ != nullptr)
    this->status_text_sensor_->publish_state(s);
}

void InverterIAP::publish_progress_(float pct) {
  if (this->progress_sensor_ != nullptr)
    this->progress_sensor_->publish_state(pct);
}

void InverterIAP::fail_(const char *reason) {
  ESP_LOGE(TAG, "Aktualizacja NIEUDANA: %s", reason);
  ESP_LOGE(TAG, "Falownik moze pozostac w bootloaderze — zresetuj go zasilaniem.");
  this->publish_status_(std::string("BLAD: ") + reason);
  this->phase_ = PH_FAILED;
  if (this->modbus_ != nullptr)
    this->modbus_->enable_loop();
}

void InverterIAP::finish_(const char *reason) {
  ESP_LOGI(TAG, "%s", reason);
  this->publish_status_(reason);
  if (!this->query_only_)
    this->publish_progress_(100.0f);
  this->phase_ = PH_DONE;
  if (this->modbus_ != nullptr)
    this->modbus_->enable_loop();
}

// --- API publiczne ----------------------------------------------------------
void InverterIAP::start_update(bool query_only) {
  if (this->fw_data_ == nullptr || this->fw_blocks_ == 0) {
    ESP_LOGE(TAG, "Brak osadzonego obrazu firmware.");
    return;
  }
  if (this->is_busy()) {
    ESP_LOGW(TAG, "Aktualizacja juz trwa (faza %d).", this->phase_);
    return;
  }
  this->query_only_ = query_only;
  if (!query_only && this->object_ == 1)
    ESP_LOGW(TAG, "UWAGA: tryb BOOT nadpisuje bootloader — ryzyko trwalego uszkodzenia!");
  if (this->modbus_ != nullptr) {
    this->modbus_->disable_loop();
    ESP_LOGI(TAG, "Modbus wstrzymany na czas aktualizacji.");
  }
  // opróżnij UART po poprzednim ruchu
  uint8_t b;
  while (this->available())
    this->read_byte(&b);
  this->block_i_ = 0;
  this->resume_block_ = 0;
  this->hs_step_ = 0;
  this->handshake_end_ms_ = millis() + 30000;  // limit handshake ~30 s
  this->publish_progress_(0.0f);
  this->publish_status_(query_only ? "Test lacznosci..." : "Start aktualizacji...");
  ESP_LOGI(TAG, "%s (ID=%u, obszar=%s, blokow=%u, suma=0x%08X)",
           query_only ? "Test lacznosci" : "Start aktualizacji", this->address_,
           this->object_ == 1 ? "Boot" : "App", this->fw_blocks_, this->flash_sum_);
  this->enter_phase_(PH_ENTER);
}

void InverterIAP::abort_update() {
  if (!this->is_busy()) {
    ESP_LOGI(TAG, "Nic do przerwania.");
    return;
  }
  this->fail_("przerwano przez uzytkownika");
}

// --- cykl życia -------------------------------------------------------------
void InverterIAP::setup() { this->publish_status_("Gotowy"); }

void InverterIAP::dump_config() {
  ESP_LOGCONFIG(TAG, "Inverter IAP (aktualizacja firmware):");
  ESP_LOGCONFIG(TAG, "  Adres Modbus: %u", this->address_);
  ESP_LOGCONFIG(TAG, "  Obszar: %s", this->object_ == 1 ? "Boot" : "App");
  ESP_LOGCONFIG(TAG, "  allow_update: %s", this->allow_update_ ? "TAK" : "NIE (tylko test lacznosci)");
  ESP_LOGCONFIG(TAG, "  Blokow firmware: %u (po 128 slow / 256 B)", this->fw_blocks_);
  ESP_LOGCONFIG(TAG, "  Suma flash: 0x%08X", this->flash_sum_);
  ESP_LOGCONFIG(TAG, "  Modbus wstrzymywany: %s", this->modbus_ != nullptr ? "TAK" : "NIE");
  this->check_uart_settings(9600, 1, uart::UART_CONFIG_PARITY_NONE, 8);
}

void InverterIAP::loop() {
  if (this->phase_ == PH_IDLE || this->phase_ == PH_DONE || this->phase_ == PH_FAILED)
    return;

  this->drain_rx_();
  if (this->debug_ && this->frame_ready_() && !this->rx_logged_) {
    this->log_hex_("RX", this->rx_, this->rx_len_);
    this->rx_logged_ = true;
  }
  uint8_t f[300];

  switch (this->phase_) {
    case PH_ENTER: {
      // Handshake jak w IAPTool (timer 1,5 s): cyklicznie Start,Start,QInfo,QInfo,QInfo
      // aż do poprawnej odpowiedzi QueryInfo. Start ponawiany co cykl — urządzenie
      // może nie wejść w bootloader za pierwszym razem.
      if (this->frame_ready_()) {
        if (this->rx_len_ >= 2 && this->rx_[1] == 0x90) {
          uint16_t t = this->rx_len_ >= 4 ? (this->rx_[2] << 8) | this->rx_[3] : 0;
          char buf[64];
          snprintf(buf, sizeof(buf), "falownik zglasza niezgodny typ 0x%04X", t);
          this->fail_(buf);
          return;
        }
        if (this->resp_chk_ok_() && this->rx_len_ >= 24 && this->rx_[1] == 0x00) {
          uint8_t chip = this->rx_[4];
          uint8_t page = this->rx_[5];
          uint16_t devtype = (this->rx_[6] << 8) | this->rx_[7];
          uint16_t region = (this->rx_[10] << 8) | this->rx_[11];
          uint16_t unlocked = (this->rx_[20] << 8) | this->rx_[21];
          this->resume_block_ = (this->rx_[22] << 8) | this->rx_[23];
          ESP_LOGI(TAG, "QueryInfo: chip=0x%02X page=%u devtype=0x%04X obszar=%s unlock=%u wznow=%u", chip,
                   page, devtype, region == 1 ? "Boot" : "App", unlocked, this->resume_block_);
          std::string errs;
          if (chip != 0)
            errs += "uklad;";
          if (page != 128)
            errs += "rozmiar pakietu;";
          if (this->object_ == 0 && devtype != 0x2300)
            errs += "typ urzadzenia;";
          if (region != this->object_)
            errs += "obszar;";
          if (unlocked == 0)
            errs += "flash zablokowany;";
          if (!errs.empty()) {
            this->fail_((std::string("niezgodnosc: ") + errs).c_str());
            return;
          }
          if (this->query_only_ || !this->allow_update_) {
            if (!this->allow_update_ && !this->query_only_)
              ESP_LOGW(TAG, "allow_update=false → tylko test lacznosci, BEZ zapisu do flash.");
            this->publish_status_("QueryInfo OK");
            this->enter_phase_(PH_RESET);
            return;
          }
          if (this->resume_block_ > 0 && this->resume_block_ <= this->fw_blocks_) {
            ESP_LOGI(TAG, "Wznawianie od bloku %u (bez kasowania).", this->resume_block_);
            this->block_i_ = this->resume_block_;
            this->publish_status_("Programowanie (wznowienie)...");
            this->enter_phase_(PH_PROGRAM);
          } else {
            this->block_i_ = 0;
            this->enter_phase_(PH_ERASE_SEND);
          }
          return;
        }
        // inna/niepoprawna ramka — odrzuć i czekaj dalej w tym kroku
        this->rx_len_ = 0;
        this->rx_logged_ = false;
      }
      if (!this->req_sent_) {
        uint16_t n = (this->hs_step_ < 2) ? this->build_enter_(f) : this->build_qinfo_(f);
        this->send_frame_(f, n);
        this->req_sent_ = true;
        this->phase_deadline_ms_ = millis() + 1000;  // odstęp kroku (IAPTool: 1,5 s)
        return;
      }
      if (millis() >= this->phase_deadline_ms_) {
        this->hs_step_ = (this->hs_step_ + 1) % 5;  // Start,Start,QInfo,QInfo,QInfo,...
        this->req_sent_ = false;
        if (millis() >= this->handshake_end_ms_)
          this->fail_("brak poprawnej odpowiedzi QueryInfo (sprawdz ID / okablowanie / standby)");
      }
      break;
    }

    case PH_ERASE_SEND: {
      uint16_t n = this->build_cmd_(f, 0x01);  // Erase (fire-and-forget)
      this->send_frame_(f, n);
      this->publish_status_("Kasowanie...");
      ESP_LOGI(TAG, "Kasowanie flash...");
      this->enter_phase_(PH_ERASE_POLL);
      break;
    }

    case PH_ERASE_POLL: {
      if (!this->req_sent_) {
        uint16_t n = this->build_cmd_(f, 0x02);  // WaitErase
        this->send_frame_(f, n);
        this->req_sent_ = true;
        this->attempt_++;
        this->phase_deadline_ms_ = millis() + 300;
        return;
      }
      if (this->frame_ready_()) {
        if (this->resp_chk_ok_() && this->rx_len_ >= 5 && this->rx_[1] == 0x02 && this->rx_[4] == 0) {
          ESP_LOGI(TAG, "Kasowanie zakonczone.");
          this->block_i_ = 0;
          this->publish_status_("Programowanie...");
          this->enter_phase_(PH_PROGRAM);
          return;
        }
        this->rx_len_ = 0;
      }
      if (millis() >= this->phase_deadline_ms_) {
        if (this->attempt_ >= 200)
          this->fail_("kasowanie nie zakonczylo sie w czasie");
        else
          this->req_sent_ = false;
      }
      break;
    }

    case PH_PROGRAM: {
      if (!this->req_sent_) {
        uint16_t n = this->build_program_(f, this->block_i_);
        this->send_frame_(f, n);
        this->req_sent_ = true;
        this->attempt_++;
        this->phase_deadline_ms_ = millis() + 800;
        return;
      }
      if (this->frame_ready_()) {
        if (this->resp_chk_ok_() && this->rx_len_ >= 5 && this->rx_[1] == 0x03) {
          if (this->rx_[4] == 0x0C) {
            char buf[64];
            snprintf(buf, sizeof(buf), "blad adresu przy bloku %u", this->block_i_);
            this->fail_(buf);
            return;
          }
          if (this->rx_[4] == 0) {
            this->block_i_++;
            this->attempt_ = 0;
            if ((this->block_i_ % 32) == 0 || this->block_i_ >= this->fw_blocks_) {
              float pct = 100.0f * this->block_i_ / this->fw_blocks_;
              this->publish_progress_(pct);
              ESP_LOGI(TAG, "Programowanie %u/%u (%.0f%%)", this->block_i_, this->fw_blocks_, pct);
            }
            if (this->block_i_ >= this->fw_blocks_)
              this->enter_phase_(PH_VERIFY);
            else
              this->req_sent_ = false;  // nastepny blok
            return;
          }
        }
        this->rx_len_ = 0;  // ponow ten sam blok
      }
      if (millis() >= this->phase_deadline_ms_) {
        if (this->attempt_ >= 5) {
          char buf[64];
          snprintf(buf, sizeof(buf), "blok %u: brak potwierdzenia", this->block_i_);
          this->fail_(buf);
        } else {
          this->req_sent_ = false;
        }
      }
      break;
    }

    case PH_VERIFY: {
      if (!this->req_sent_) {
        uint16_t n = this->build_verify_(f);
        this->send_frame_(f, n);
        this->publish_status_("Weryfikacja...");
        ESP_LOGI(TAG, "Weryfikacja (suma flash=0x%08X)...", this->flash_sum_);
        this->req_sent_ = true;
        this->attempt_++;
        this->phase_deadline_ms_ = millis() + 1500;
        return;
      }
      if (this->frame_ready_()) {
        if (this->resp_chk_ok_() && this->rx_len_ >= 5 && this->rx_[1] == 0x04) {
          if (this->rx_[4] == 0)
            this->enter_phase_(PH_RESET);
          else
            this->fail_("weryfikacja nieudana (suma flash sie nie zgadza)");
          return;
        }
        this->rx_len_ = 0;
      }
      if (millis() >= this->phase_deadline_ms_) {
        if (this->attempt_ >= 5)
          this->fail_("brak odpowiedzi na Verify");
        else
          this->req_sent_ = false;
      }
      break;
    }

    case PH_RESET: {
      uint16_t n = this->build_cmd_(f, 0x05);  // Reset
      this->send_frame_(f, n);
      this->send_frame_(f, n);
      this->send_frame_(f, n);
      this->finish_(this->query_only_ ? "Test lacznosci zakonczony (reset do aplikacji)."
                                      : "Aktualizacja zakonczona. Falownik uruchamia nowy firmware.");
      break;
    }

    default:
      break;
  }
}

}  // namespace inverter_iap
}  // namespace esphome
