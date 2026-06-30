#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "tcl_climate.h"
#include <map>
#include <cmath>

namespace esphome {
namespace tcl_climate {

static constexpr uint8_t REQ_CMD[] = {0xBB, 0x00, 0x01, 0x04, 0x02, 0x01, 0x00, 0xBD};
static constexpr int MAX_LINE_LENGTH = 100;
static constexpr int UPDATE_INTERVAL_MS = 450;

// ─────────────────────────────────────────────────────────────────────────────
// State setters (detect changes, set is_changed for publish_state)
// ─────────────────────────────────────────────────────────────────────────────

void TCLClimate::set_current_temperature(float current_temperature) {
  if (std::abs(this->current_temperature - current_temperature) < 0.1f) return;
  ESP_LOGD("TCL", "Current temperature updated to: %.1f°C", current_temperature);
  this->is_changed = true;
  this->current_temperature = current_temperature;
}

void TCLClimate::set_custom_fan_mode(StringRef fan_mode) {
  StringRef current(this->get_custom_fan_mode());
  if (!current.empty() && fan_mode == current.c_str()) return;
  ESP_LOGI("TCL", "Fan mode changed to: %s", fan_mode.c_str());
  this->is_changed = true;
  this->set_custom_fan_mode_(fan_mode.c_str());
}

void TCLClimate::set_mode(climate::ClimateMode mode) {
  if (this->mode == mode) return;
  const char* mode_str = "";
  switch (mode) {
    case climate::CLIMATE_MODE_OFF:      mode_str = "OFF";      break;
    case climate::CLIMATE_MODE_COOL:     mode_str = "COOL";     break;
    case climate::CLIMATE_MODE_HEAT:     mode_str = "HEAT";     break;
    case climate::CLIMATE_MODE_FAN_ONLY: mode_str = "FAN ONLY"; break;
    case climate::CLIMATE_MODE_DRY:      mode_str = "DRY";      break;
    case climate::CLIMATE_MODE_HEAT_COOL: mode_str = "HEAT_COOL"; break;
    default:                             mode_str = "UNKNOWN";  break;
  }
  ESP_LOGI("TCL", "Climate mode changed to: %s", mode_str);
  this->is_changed = true;
  this->mode = mode;
}

void TCLClimate::set_swing_mode(climate::ClimateSwingMode swing_mode) {
  if (this->swing_mode == swing_mode) return;
  const char* swing_str = "";
  switch (swing_mode) {
    case climate::CLIMATE_SWING_OFF:        swing_str = "OFF";        break;
    case climate::CLIMATE_SWING_BOTH:       swing_str = "BOTH";       break;
    case climate::CLIMATE_SWING_VERTICAL:   swing_str = "VERTICAL";   break;
    case climate::CLIMATE_SWING_HORIZONTAL: swing_str = "HORIZONTAL"; break;
    default:                                swing_str = "UNKNOWN";    break;
  }
  ESP_LOGI("TCL", "Swing mode changed to: %s", swing_str);
  this->is_changed = true;
  this->swing_mode = swing_mode;
}

void TCLClimate::set_target_temperature(float target_temperature) {
  if (std::abs(this->target_temperature - target_temperature) < 0.1f) return;
  ESP_LOGI("TCL", "Target temperature changed to: %.1f°C", target_temperature);
  this->is_changed = true;
  this->target_temperature = target_temperature;
}

void TCLClimate::set_hswing_pos(const std::string &pos) {
  if (this->hswing_pos == pos) return;
  ESP_LOGI("TCL", "Horizontal swing position: %s", pos.c_str());
  this->hswing_pos = pos;
  if (this->hswing_select_ != nullptr)
    this->hswing_select_->publish_state(pos);
}

void TCLClimate::set_vswing_pos(const std::string &pos) {
  if (this->vswing_pos == pos) return;
  ESP_LOGI("TCL", "Vertical swing position: %s", pos.c_str());
  this->vswing_pos = pos;
  if (this->vswing_select_ != nullptr)
    this->vswing_select_->publish_state(pos);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_set_cmd: converts get_cmd_resp_t (AC state) into the set command
// ─────────────────────────────────────────────────────────────────────────────

void TCLClimate::build_set_cmd(get_cmd_resp_t *get_cmd_resp) {
  memcpy(m_set_cmd.raw, set_cmd_base, sizeof(m_set_cmd.raw));

  m_set_cmd.data.power        = get_cmd_resp->data.power;
  m_set_cmd.data.off_timer_en = 0;
  m_set_cmd.data.on_timer_en  = 0;

  // Beep, display, eco from internal state
  m_set_cmd.data.beep      = beep_enabled_    ? 1 : 0;
  m_set_cmd.data.disp      = display_enabled_ ? 1 : 0;
  m_set_cmd.data.eco       = eco_enabled_     ? 1 : 0;
  m_set_cmd.data.turbo     = get_cmd_resp->data.turbo;
  m_set_cmd.data.mute      = get_cmd_resp->data.mute;

  // 8°C heater
  m_set_cmd.data.heater_8deg = deg8_enabled_ ? 1 : 0;

  // Sleep mode (bits[6:7] in byte 19)
  m_set_cmd.data.sleep_mode = sleep_mode_;

  static constexpr uint8_t MODE_MAP[] = {
    0x00, // 0x00 - unused
    0x03, // 0x01 -> cool
    0x02, // 0x02 -> fan only
    0x07, // 0x03 -> dry
    0x01, // 0x04 -> heat
    0x08  // 0x05 -> auto
  };
  if (get_cmd_resp->data.mode < sizeof(MODE_MAP))
    m_set_cmd.data.mode = MODE_MAP[get_cmd_resp->data.mode];

  // Temperature: in the set command temp is inverted too: temp = 31 - T
  // When external regulation is active, use the computed send value without
  // changing the user's UI setpoint (target_temperature).
  float target = this->reg_temp_override_ ? this->reg_sent_temp_ : this->target_temperature;
  uint8_t temp_int  = static_cast<uint8_t>(target);
  bool    half_deg  = (target - temp_int) >= 0.4f;
  m_set_cmd.data.temp        = 31 - temp_int;
  m_set_cmd.data.half_degree = half_deg ? 1 : 0;

  static constexpr uint8_t FAN_MAP[] = {
    0x00, // auto
    0x02, // 1
    0x03, // 3
    0x05, // 5
    0x06, // 2
    0x07  // 4
  };
  if (get_cmd_resp->data.fan < sizeof(FAN_MAP))
    m_set_cmd.data.fan = FAN_MAP[get_cmd_resp->data.fan];

  // Vertical swing
  if (get_cmd_resp->data.vswing_mv) {
    m_set_cmd.data.vswing     = 0x07;
    m_set_cmd.data.vswing_fix = 0;
    m_set_cmd.data.vswing_mv  = get_cmd_resp->data.vswing_mv;
  } else if (get_cmd_resp->data.vswing_fix) {
    m_set_cmd.data.vswing     = 0;
    m_set_cmd.data.vswing_fix = get_cmd_resp->data.vswing_fix;
    m_set_cmd.data.vswing_mv  = 0;
  } else {
    m_set_cmd.data.vswing     = get_cmd_resp->data.vswing;
    m_set_cmd.data.vswing_fix = 0;
    m_set_cmd.data.vswing_mv  = 0;
  }

  // Horizontal swing
  if (get_cmd_resp->data.hswing_mv) {
    m_set_cmd.data.hswing     = 0x01;
    m_set_cmd.data.hswing_fix = 0;
    m_set_cmd.data.hswing_mv  = get_cmd_resp->data.hswing_mv;
  } else if (get_cmd_resp->data.hswing_fix) {
    m_set_cmd.data.hswing     = 0;
    m_set_cmd.data.hswing_fix = get_cmd_resp->data.hswing_fix;
    m_set_cmd.data.hswing_mv  = 0;
  } else {
    m_set_cmd.data.hswing     = get_cmd_resp->data.hswing;
    m_set_cmd.data.hswing_fix = 0;
    m_set_cmd.data.hswing_mv  = 0;
  }

  // XOR checksum
  uint8_t xor_byte = 0;
  for (size_t i = 0; i < sizeof(m_set_cmd.raw) - 1; i++)
    xor_byte ^= m_set_cmd.raw[i];
  m_set_cmd.raw[sizeof(m_set_cmd.raw) - 1] = xor_byte;
}

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────

void TCLClimate::setup() {
  set_update_interval(UPDATE_INTERVAL_MS);
  this->set_supported_custom_fan_modes({"Turbo", "Mute", "Automatic", "1", "2", "3", "4", "5"});
  // Custom presets must be registered on the entity (traits version is deprecated)
  this->set_supported_custom_presets({"Sleep Elderly", "Sleep Young"});

  // ── Select callbacks ──────────────────────────────────────────────────────
  if (this->vswing_select_ != nullptr) {
    this->vswing_select_->add_on_state_callback([this](uint32_t) {
      const std::string &val = this->vswing_select_->current_option();
      if (val == this->vswing_pos) return;  // no change vs AC state
      this->control_vertical_swing(val);
    });
  }
  if (this->hswing_select_ != nullptr) {
    this->hswing_select_->add_on_state_callback([this](uint32_t) {
      const std::string &val = this->hswing_select_->current_option();
      if (val == this->hswing_pos) return;  // no change vs AC state
      this->control_horizontal_swing(val);
    });
  }
  if (this->sleep_select_ != nullptr) {
    this->sleep_select_->add_on_state_callback([this](uint32_t) {
      const std::string &val = this->sleep_select_->current_option();
      uint8_t new_mode = 0;
      if      (val == "Default") new_mode = 0x01;
      else if (val == "Elderly") new_mode = 0x02;
      else if (val == "Young")   new_mode = 0x03;
      if (new_mode == sleep_mode_) return;  // no change
      this->control_sleep_mode(val);
    });
  }
  if (this->preset_select_ != nullptr) {
    this->preset_select_->add_on_state_callback([this](uint32_t) {
      // Guard: only send when the value actually changed vs the AC state
      const std::string &val = this->preset_select_->current_option();
      bool ac_turbo = (m_get_cmd_resp.data.turbo == 1);
      bool ac_mute  = (m_get_cmd_resp.data.mute  == 1);
      bool want_turbo = (val == "Turbo");
      bool want_mute  = (val == "Mute");
      if (want_turbo == ac_turbo && want_mute == ac_mute) return;  // no change
      get_cmd_resp_t gcr = {0};
      memcpy(gcr.raw, m_get_cmd_resp.raw, sizeof(gcr.raw));
      gcr.data.turbo = want_turbo ? 1 : 0;
      gcr.data.mute  = want_mute  ? 1 : 0;
      ESP_LOGI("TCL", "Preset changed to: %s", val.c_str());
      build_set_cmd(&gcr);
      ready_to_send_set_cmd_flag = true;
    });
  }

  // ── Switch callbacks ──────────────────────────────────────────────────────
  if (this->beep_switch_ != nullptr) {
    this->beep_switch_->add_on_state_callback([this](bool state) {
      if (state == beep_enabled_) return;
      beep_enabled_ = state;
      ESP_LOGI("TCL", "Beep %s", state ? "ON" : "OFF");
      build_set_cmd(&m_get_cmd_resp);
      ready_to_send_set_cmd_flag = true;
    });
  }
  if (this->display_switch_ != nullptr) {
    this->display_switch_->add_on_state_callback([this](bool state) {
      if (state == display_enabled_) return;
      display_enabled_ = state;
      ESP_LOGI("TCL", "Display %s", state ? "ON" : "OFF");
      build_set_cmd(&m_get_cmd_resp);
      ready_to_send_set_cmd_flag = true;
    });
  }
  if (this->eco_switch_ != nullptr) {
    this->eco_switch_->add_on_state_callback([this](bool state) {
      if (state == eco_enabled_) return;
      eco_enabled_ = state;
      ESP_LOGI("TCL", "Eco mode %s", state ? "ON" : "OFF");
      build_set_cmd(&m_get_cmd_resp);
      ready_to_send_set_cmd_flag = true;
    });
  }
  if (this->deg8_switch_ != nullptr) {
    this->deg8_switch_->add_on_state_callback([this](bool state) {
      if (state == deg8_enabled_) return;
      deg8_enabled_ = state;
      ESP_LOGI("TCL", "8° Heater %s", state ? "ON" : "OFF");
      build_set_cmd(&m_get_cmd_resp);
      ready_to_send_set_cmd_flag = true;
    });
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Direct louver and sleep control
// ─────────────────────────────────────────────────────────────────────────────

void TCLClimate::control_vertical_swing(const std::string &swing_mode) {
  get_cmd_resp_t gcr = {0};
  memcpy(gcr.raw, m_get_cmd_resp.raw, sizeof(gcr.raw));
  gcr.data.vswing_mv  = 0;
  gcr.data.vswing_fix = 0;

  if      (swing_mode == "Move full")   gcr.data.vswing_mv  = 0x01;
  else if (swing_mode == "Move upper")  gcr.data.vswing_mv  = 0x02;
  else if (swing_mode == "Move lower")  gcr.data.vswing_mv  = 0x03;
  else if (swing_mode == "Fix top")     gcr.data.vswing_fix = 0x01;
  else if (swing_mode == "Fix upper")   gcr.data.vswing_fix = 0x02;
  else if (swing_mode == "Fix mid")     gcr.data.vswing_fix = 0x03;
  else if (swing_mode == "Fix lower")   gcr.data.vswing_fix = 0x04;
  else if (swing_mode == "Fix bottom")  gcr.data.vswing_fix = 0x05;

  gcr.data.vswing = gcr.data.vswing_mv ? 0x01 : 0;
  build_set_cmd(&gcr);
  ready_to_send_set_cmd_flag = true;
}

void TCLClimate::control_horizontal_swing(const std::string &swing_mode) {
  get_cmd_resp_t gcr = {0};
  memcpy(gcr.raw, m_get_cmd_resp.raw, sizeof(gcr.raw));
  gcr.data.hswing_mv  = 0;
  gcr.data.hswing_fix = 0;

  if      (swing_mode == "Move full")      gcr.data.hswing_mv  = 0x01;
  else if (swing_mode == "Move left")      gcr.data.hswing_mv  = 0x02;
  else if (swing_mode == "Move mid")       gcr.data.hswing_mv  = 0x03;
  else if (swing_mode == "Move right")     gcr.data.hswing_mv  = 0x04;
  else if (swing_mode == "Fix left")       gcr.data.hswing_fix = 0x01;
  else if (swing_mode == "Fix mid left")   gcr.data.hswing_fix = 0x02;
  else if (swing_mode == "Fix mid")        gcr.data.hswing_fix = 0x03;
  else if (swing_mode == "Fix mid right")  gcr.data.hswing_fix = 0x04;
  else if (swing_mode == "Fix right")      gcr.data.hswing_fix = 0x05;

  gcr.data.hswing = gcr.data.hswing_mv ? 0x01 : 0;
  build_set_cmd(&gcr);
  ready_to_send_set_cmd_flag = true;
}

void TCLClimate::control_sleep_mode(const std::string &mode) {
  if      (mode == "Off")         sleep_mode_ = 0x00;
  else if (mode == "Default")     sleep_mode_ = 0x01;
  else if (mode == "Elderly")     sleep_mode_ = 0x02;
  else if (mode == "Young")       sleep_mode_ = 0x03;
  ESP_LOGI("TCL", "Sleep mode: %s (0x%02X)", mode.c_str(), sleep_mode_);
  build_set_cmd(&m_get_cmd_resp);
  ready_to_send_set_cmd_flag = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// External sensor regulation (proportional thermostat)
//
// Instead of sending the user setpoint to the AC, a virtual setpoint is
// computed so the AC modulates proportionally to the real room temperature:
//   sent = internal_temp + user_target - external_temp
// The AC then sees gap = internal - sent = external - user_target and drives
// its compressor RPM proportionally to the deviation from the target.
// ─────────────────────────────────────────────────────────────────────────────

bool TCLClimate::can_send_regulation() {
  uint32_t now = millis();
  if (now - last_regulation_send_ms_ < regulation_interval_ms_) return false;
  last_regulation_send_ms_ = now;
  return true;
}

void TCLClimate::run_regulation(float curr_temp, float ext_temp) {
  float target = this->target_temperature;

  // Sends a regulation command with the computed temperature without changing
  // the UI setpoint (target_temperature).
  auto send_reg = [this](float sent_temp, uint8_t ac_mode) {
    this->m_get_cmd_resp.data.power = 0x01;
    this->m_get_cmd_resp.data.mode  = ac_mode;
    this->reg_sent_temp_     = sent_temp;
    this->reg_temp_override_ = true;
    this->build_set_cmd(&this->m_get_cmd_resp);
    this->reg_temp_override_ = false;
    this->ready_to_send_set_cmd_flag = true;
  };

  // Temperature field currently reported by the AC (displayed - 16) and half-degree bit
  uint8_t cur_field = m_get_cmd_resp.data.temp;
  bool    cur_half  = (m_get_cmd_resp.data.half_degree == 1);

  if (this->mode == climate::CLIMATE_MODE_COOL) {
    if (ext_temp > target) {
      float sent = curr_temp + target - ext_temp;
      if (sent < 16.0f) sent = 16.0f;
      if (sent > 30.0f) sent = 30.0f;
      last_sent_temp_ = sent;
      uint8_t field  = static_cast<uint8_t>(floorf(sent)) - 16;
      bool want_half = (sent - floorf(sent)) >= 0.4f;
      if ((cur_field != field || cur_half != want_half || m_get_cmd_resp.data.power == 0) && can_send_regulation()) {
        send_reg(sent, 0x01);
        ESP_LOGD("TCL", "Cool reg: ext=%.1f int=%.1f tgt=%.1f -> sent=%.1f", ext_temp, curr_temp, target, sent);
      }
    } else {
      // Setpoint reached: send the highest value so the AC stops cooling
      last_sent_temp_ = 31.0f;
      if (cur_field != 15 && can_send_regulation()) {
        send_reg(31.0f, 0x01);
        ESP_LOGD("TCL", "Cool reg: ext=%.1f <= tgt=%.1f -> stop", ext_temp, target);
      }
    }
  } else if (this->mode == climate::CLIMATE_MODE_HEAT) {
    if (ext_temp < target) {
      float sent = curr_temp + target - ext_temp;
      if (sent < 16.0f) sent = 16.0f;
      if (sent > 30.0f) sent = 30.0f;
      last_sent_temp_ = sent;
      uint8_t field  = static_cast<uint8_t>(floorf(sent)) - 16;
      bool want_half = (sent - floorf(sent)) >= 0.4f;
      if ((cur_field != field || cur_half != want_half || m_get_cmd_resp.data.power == 0) && can_send_regulation()) {
        send_reg(sent, 0x04);
        ESP_LOGD("TCL", "Heat reg: ext=%.1f int=%.1f tgt=%.1f -> sent=%.1f", ext_temp, curr_temp, target, sent);
      }
    } else {
      // Setpoint reached: send the lowest value so the AC stops heating
      last_sent_temp_ = 16.0f;
      if (cur_field != 0 && can_send_regulation()) {
        send_reg(16.0f, 0x04);
        ESP_LOGD("TCL", "Heat reg: ext=%.1f >= tgt=%.1f -> stop", ext_temp, target);
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// HA climate presets  (Eco / Sleep / 8°C heater)
//
// Native presets:  ECO -> eco bit, SLEEP -> sleep mode 0x01, AWAY -> 8°C heater
// Custom presets:  "Sleep Elderly" -> 0x02, "Sleep Young" -> 0x03
// The legacy entities (eco_switch/8deg_switch/sleep_select) stay usable and
// are kept in sync with the preset state.
// ─────────────────────────────────────────────────────────────────────────────

void TCLClimate::apply_preset(climate::ClimatePreset preset) {
  switch (preset) {
    case climate::CLIMATE_PRESET_ECO:   eco_enabled_ = true;  deg8_enabled_ = false; sleep_mode_ = 0x00; break;
    case climate::CLIMATE_PRESET_SLEEP: eco_enabled_ = false; deg8_enabled_ = false; sleep_mode_ = 0x01; break;
    case climate::CLIMATE_PRESET_AWAY:  eco_enabled_ = false; deg8_enabled_ = true;  sleep_mode_ = 0x00; break;
    case climate::CLIMATE_PRESET_NONE:
    default:                            eco_enabled_ = false; deg8_enabled_ = false; sleep_mode_ = 0x00; break;
  }
  this->preset = preset;
  this->clear_custom_preset_();
  this->sync_feature_entities();
}

void TCLClimate::apply_custom_preset(const std::string &custom_preset) {
  eco_enabled_ = false;
  deg8_enabled_ = false;
  if      (custom_preset == "Sleep Elderly") sleep_mode_ = 0x02;
  else if (custom_preset == "Sleep Young")   sleep_mode_ = 0x03;
  else                                       sleep_mode_ = 0x00;
  this->set_custom_preset_(custom_preset.c_str());
  this->preset.reset();
  this->sync_feature_entities();
}

void TCLClimate::sync_feature_entities() {
  // Push the internal state onto the legacy control entities. Their callbacks
  // bail out on an unchanged value, so no extra command is generated.
  if (this->eco_switch_ != nullptr && this->eco_switch_->state != eco_enabled_)
    this->eco_switch_->publish_state(eco_enabled_);
  if (this->deg8_switch_ != nullptr && this->deg8_switch_->state != deg8_enabled_)
    this->deg8_switch_->publish_state(deg8_enabled_);
  if (this->sleep_select_ != nullptr) {
    const char *opt = (sleep_mode_ == 0x01) ? "Default"
                    : (sleep_mode_ == 0x02) ? "Elderly"
                    : (sleep_mode_ == 0x03) ? "Young"
                                            : "Off";
    if (this->sleep_select_->current_option() != opt)
      this->sleep_select_->publish_state(opt);
  }
}

void TCLClimate::update_preset_state() {
  // Derive the preset to display from the internal state (priority:
  // sleep sub-modes > Sleep > 8°C > Eco > no preset).
  climate::ClimatePreset np = climate::CLIMATE_PRESET_NONE;
  const char *nc = nullptr;
  if      (sleep_mode_ == 0x02) nc = "Sleep Elderly";
  else if (sleep_mode_ == 0x03) nc = "Sleep Young";
  else if (sleep_mode_ == 0x01) np = climate::CLIMATE_PRESET_SLEEP;
  else if (deg8_enabled_)       np = climate::CLIMATE_PRESET_AWAY;
  else if (eco_enabled_)        np = climate::CLIMATE_PRESET_ECO;

  if (nc != nullptr) {
    if (!this->has_custom_preset() || this->get_custom_preset() != nc) {
      this->set_custom_preset_(nc);
      this->preset.reset();
      this->is_changed = true;
    }
  } else {
    if (this->has_custom_preset() || !this->preset.has_value() || this->preset.value() != np) {
      this->preset = np;
      this->clear_custom_preset_();
      this->is_changed = true;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// control() - called by HA
// ─────────────────────────────────────────────────────────────────────────────

void TCLClimate::control(const climate::ClimateCall &call) {
  get_cmd_resp_t gcr = {0};
  memcpy(gcr.raw, m_get_cmd_resp.raw, sizeof(gcr.raw));
  bool should_build = false;

  if (call.get_mode().has_value()) {
    climate::ClimateMode climate_mode = *call.get_mode();
    if (climate_mode == climate::CLIMATE_MODE_OFF) {
      gcr.data.power = 0x00;
    } else {
      gcr.data.power = 0x01;
      switch (climate_mode) {
        case climate::CLIMATE_MODE_COOL:      gcr.data.mode = 0x01; break;
        case climate::CLIMATE_MODE_DRY:       gcr.data.mode = 0x03; break;
        case climate::CLIMATE_MODE_FAN_ONLY:  gcr.data.mode = 0x02; break;
        case climate::CLIMATE_MODE_HEAT:      gcr.data.mode = 0x04; break;
        case climate::CLIMATE_MODE_HEAT_COOL: gcr.data.mode = 0x05; break;
        default: break;
      }
    }
    should_build = true;
  }

  if (call.get_target_temperature().has_value()) {
    float temp = *call.get_target_temperature();
    this->target_temperature = temp;
    this->user_has_set_target_ = true;
    if (this->regulation_enabled()) {
      // With external regulation, loop() computes the value actually sent;
      // the user setpoint is only stored, not sent to the AC directly.
    } else {
      // AC replies with temp = T - 16 (direct, not inverted)
      uint8_t temp_int = static_cast<uint8_t>(temp);
      m_get_cmd_resp.data.temp = (temp_int - 16) & 0x0F;
      temp_cmd_sent_ms_ = millis();
      should_build = true;
    }
  }

  if (call.get_swing_mode().has_value()) {
    climate::ClimateSwingMode swing = *call.get_swing_mode();
    switch (swing) {
      case climate::CLIMATE_SWING_OFF:
        gcr.data.hswing = 0; gcr.data.vswing = 0;
        gcr.data.vswing_fix = 0x03; gcr.data.hswing_fix = 0x03;
        gcr.data.vswing_mv = 0; gcr.data.hswing_mv = 0;
        break;
      case climate::CLIMATE_SWING_BOTH:
        gcr.data.hswing = 1; gcr.data.vswing = 1;
        gcr.data.vswing_mv = 0x01; gcr.data.hswing_mv = 0x01;
        gcr.data.vswing_fix = 0; gcr.data.hswing_fix = 0;
        break;
      case climate::CLIMATE_SWING_VERTICAL:
        gcr.data.vswing = 1; gcr.data.hswing = 0;
        gcr.data.vswing_mv = 0x01; gcr.data.vswing_fix = 0;
        gcr.data.hswing_mv = 0; gcr.data.hswing_fix = 0x03;
        break;
      case climate::CLIMATE_SWING_HORIZONTAL:
        gcr.data.hswing = 1; gcr.data.vswing = 0;
        gcr.data.hswing_mv = 0x01; gcr.data.hswing_fix = 0;
        gcr.data.vswing_mv = 0; gcr.data.vswing_fix = 0x03;
        break;
      default: break;
    }
    should_build = true;
  }

  StringRef custom_fan_mode(call.get_custom_fan_mode());
  if (!custom_fan_mode.empty()) {
    std::string fan_mode(custom_fan_mode.c_str());
    gcr.data.turbo = 0; gcr.data.mute = 0;
    static const std::map<std::string, uint8_t> FAN_MAP = {
      {"Automatic", 0x00}, {"1", 0x01}, {"2", 0x04},
      {"3", 0x02}, {"4", 0x05}, {"5", 0x03}
    };
    auto it = FAN_MAP.find(fan_mode);
    if (it != FAN_MAP.end()) gcr.data.fan = it->second;
    if (fan_mode == "Turbo") gcr.data.turbo = 0x01;
    if (fan_mode == "Mute")  gcr.data.mute  = 0x01;
    should_build = true;
  }

  // Presets (Eco / Sleep / 8°C). apply_* sets eco_enabled_/deg8_enabled_/
  // sleep_mode_, which build_set_cmd then writes into the command.
  if (call.get_preset().has_value()) {
    this->apply_preset(*call.get_preset());
    should_build = true;
  }
  if (call.has_custom_preset()) {
    this->apply_custom_preset(std::string(call.get_custom_preset().c_str()));
    should_build = true;
  }

  if (should_build) {
    build_set_cmd(&gcr);
    ready_to_send_set_cmd_flag = true;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// traits()
// ─────────────────────────────────────────────────────────────────────────────

climate::ClimateTraits TCLClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_supported_modes({
    climate::CLIMATE_MODE_OFF,
    climate::CLIMATE_MODE_COOL,
    climate::CLIMATE_MODE_HEAT,
    climate::CLIMATE_MODE_FAN_ONLY,
    climate::CLIMATE_MODE_DRY,
    climate::CLIMATE_MODE_HEAT_COOL
  });
  if (!this->custom_swing_modes_.empty()) {
    for (auto mode : this->custom_swing_modes_) {
      traits.add_supported_swing_mode(mode);
    }
  } else {
    // Keep original repository fallback behaviors intact 
    traits.add_supported_swing_mode(climate::CLIMATE_SWING_OFF);
    traits.add_supported_swing_mode(climate::CLIMATE_SWING_VERTICAL);
    traits.add_supported_swing_mode(climate::CLIMATE_SWING_HORIZONTAL);
    traits.add_supported_swing_mode(climate::CLIMATE_SWING_BOTH);
  }
  // Presets: Eco / Sleep / 8°C (Away) native, sleep sub-modes as custom presets
  traits.set_supported_presets({
    climate::CLIMATE_PRESET_NONE,
    climate::CLIMATE_PRESET_ECO,
    climate::CLIMATE_PRESET_SLEEP,
    climate::CLIMATE_PRESET_AWAY,
  });
  traits.set_visual_min_temperature(16.0);
  traits.set_visual_max_temperature(31.0);
  traits.set_visual_target_temperature_step(1.0);  // device does not support 0.5°C steps
  return traits;
}

// ─────────────────────────────────────────────────────────────────────────────
// update() - periodic
// ─────────────────────────────────────────────────────────────────────────────

void TCLClimate::update() {
  if (ready_to_send_set_cmd_flag) {
    ready_to_send_set_cmd_flag = false;
    write_array(m_set_cmd.raw, sizeof(m_set_cmd.raw));
  } else {
    write_array(REQ_CMD, sizeof(REQ_CMD));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// UART helpers
// ─────────────────────────────────────────────────────────────────────────────

int TCLClimate::read_data_line(int readch, uint8_t *buffer, int len) {
  static int pos = 0;
  static bool wait_len = false;
  static int skipch = 0;

  if (readch < 0) return -1;

  if (readch == 0xBB && skipch == 0 && !wait_len) {
    pos = 0; skipch = 3; wait_len = true;
    if (pos < len) buffer[pos++] = static_cast<uint8_t>(readch);
  } else if (skipch == 0 && wait_len) {
    if (pos < len) buffer[pos++] = static_cast<uint8_t>(readch);
    skipch = readch + 1;
    wait_len = false;
  } else if (skipch > 0) {
    if (pos < len) buffer[pos++] = static_cast<uint8_t>(readch);
    if (--skipch == 0 && !wait_len) return pos;
  }
  return -1;
}

bool TCLClimate::is_valid_xor(uint8_t *buffer, int len) {
  if (len < 1) return false;
  uint8_t xor_byte = 0;
  for (int i = 0; i < len - 1; i++) xor_byte ^= buffer[i];
  return xor_byte == buffer[len - 1];
}

void TCLClimate::print_hex_str(uint8_t *buffer, int len) {
  if (len <= 0) return;
  char str[MAX_LINE_LENGTH * 3] = {0};
  char *pstr = str;
  for (int i = 0; i < len && (pstr - str) < static_cast<int>(sizeof(str)) - 3; i++)
    pstr += sprintf(pstr, "%02X ", buffer[i]);
  ESP_LOGD("TCL", "Received: %s", str);
}

// ─────────────────────────────────────────────────────────────────────────────
// loop() - receive UART and evaluate state
// ─────────────────────────────────────────────────────────────────────────────

void TCLClimate::loop() {
  static uint8_t buffer[MAX_LINE_LENGTH];

  while (available()) {
    int len = read_data_line(read(), buffer, MAX_LINE_LENGTH);
    if (len == sizeof(m_get_cmd_resp) && buffer[3] == 0x04) {
      memcpy(m_get_cmd_resp.raw, buffer, len);

      if (is_valid_xor(buffer, len)) {
        print_hex_str(buffer, len);

        float curr_temp = ((static_cast<uint16_t>(buffer[17] << 8) | buffer[18]) / 374.0f - 32.0f) / 1.8f;

        // Debounce: while we recently sent our own temperature command, do NOT
        // take the AC's returned temperature into m_get_cmd_resp.
        // All other fields (mode, fan, swing...) are always taken over.
        bool temp_settled = (millis() - temp_cmd_sent_ms_) > TEMP_DEBOUNCE_MS;
        if (temp_settled) {
          memcpy(m_get_cmd_resp.raw, buffer, len);
        } else {
          // temp lives in raw[8] bits[3:0] - keep that nibble,
          // take all other fields (mode, fan, swing...) from the AC reply.
          uint8_t saved_temp_nibble = m_get_cmd_resp.raw[8] & 0x0F;  // keep temp[3:0]
          memcpy(m_get_cmd_resp.raw, buffer, len);
          m_get_cmd_resp.raw[8] = (buffer[8] & 0xF0) | saved_temp_nibble;  // fan nibble from AC, temp from us
        }

        this->is_changed = false;

        // ── Operating mode ────────────────────────────────────────────────────
        if (m_get_cmd_resp.data.power == 0x00) {
          this->set_mode(climate::CLIMATE_MODE_OFF);
        } else {
          static const std::map<uint8_t, climate::ClimateMode> MODE_MAP = {
            {0x01, climate::CLIMATE_MODE_COOL},
            {0x03, climate::CLIMATE_MODE_DRY},
            {0x02, climate::CLIMATE_MODE_FAN_ONLY},
            {0x04, climate::CLIMATE_MODE_HEAT},
            {0x05, climate::CLIMATE_MODE_HEAT_COOL}
          };
          auto it = MODE_MAP.find(m_get_cmd_resp.data.mode);
          if (it != MODE_MAP.end()) this->set_mode(it->second);
        }

        // ── Fan / preset ───────────────────────────────────────────────────
        static const std::map<uint8_t, std::string> FAN_MAP = {
          {0x00, "Automatic"}, {0x01, "1"}, {0x04, "2"},
          {0x02, "3"},         {0x05, "4"}, {0x03, "5"}
        };
        if (m_get_cmd_resp.data.turbo) {
          this->set_custom_fan_mode(StringRef("Turbo"));
          if (this->preset_select_ != nullptr && this->preset_select_->current_option() != "Turbo")
            this->preset_select_->publish_state("Turbo");
        } else if (m_get_cmd_resp.data.mute) {
          this->set_custom_fan_mode(StringRef("Mute"));
          if (this->preset_select_ != nullptr && this->preset_select_->current_option() != "Mute")
            this->preset_select_->publish_state("Mute");
        } else {
          auto it = FAN_MAP.find(m_get_cmd_resp.data.fan);
          if (it != FAN_MAP.end()) {
            StringRef current(this->get_custom_fan_mode());
            if (current.empty() || current != it->second)
              this->set_custom_fan_mode(StringRef(it->second.c_str(), it->second.size()));
          }
          if (this->preset_select_ != nullptr && this->preset_select_->current_option() != "Normal")
            this->preset_select_->publish_state("Normal");
        }

        // ── Sync display switch back ───────────────────────────────
        bool disp_on = (m_get_cmd_resp.data.disp == 1);
        if (this->display_switch_ != nullptr && this->display_switch_->state != disp_on) {
          display_enabled_ = disp_on;
          this->display_switch_->publish_state(disp_on);
        }

        // ── Take eco from the AC reply (switch and preset follow) ────────
        // (Sleep mode and 8°C heater are not encoded in the AC reply, so they
        //  are derived from the internal state only.)
        bool eco_on = (m_get_cmd_resp.data.eco == 1);
        eco_enabled_ = eco_on;
        if (this->eco_switch_ != nullptr && this->eco_switch_->state != eco_on)
          this->eco_switch_->publish_state(eco_on);

        // ── Swing mode ─────────────────────────────────────────────────────
        if      (m_get_cmd_resp.data.hswing && m_get_cmd_resp.data.vswing)
          this->set_swing_mode(climate::CLIMATE_SWING_BOTH);
        else if (!m_get_cmd_resp.data.hswing && !m_get_cmd_resp.data.vswing)
          this->set_swing_mode(climate::CLIMATE_SWING_OFF);
        else if (m_get_cmd_resp.data.vswing)
          this->set_swing_mode(climate::CLIMATE_SWING_VERTICAL);
        else
          this->set_swing_mode(climate::CLIMATE_SWING_HORIZONTAL);

        // ── Vertical louver position ────────────────────────────────────────
        if      (m_get_cmd_resp.data.vswing_mv == 0x01) set_vswing_pos("Move full");
        else if (m_get_cmd_resp.data.vswing_mv == 0x02) set_vswing_pos("Move upper");
        else if (m_get_cmd_resp.data.vswing_mv == 0x03) set_vswing_pos("Move lower");
        else if (m_get_cmd_resp.data.vswing_fix == 0x01) set_vswing_pos("Fix top");
        else if (m_get_cmd_resp.data.vswing_fix == 0x02) set_vswing_pos("Fix upper");
        else if (m_get_cmd_resp.data.vswing_fix == 0x03) set_vswing_pos("Fix mid");
        else if (m_get_cmd_resp.data.vswing_fix == 0x04) set_vswing_pos("Fix lower");
        else if (m_get_cmd_resp.data.vswing_fix == 0x05) set_vswing_pos("Fix bottom");
        else set_vswing_pos("Last position");

        // ── Horizontal louver position ──────────────────────────────────────
        if      (m_get_cmd_resp.data.hswing_mv == 0x01) set_hswing_pos("Move full");
        else if (m_get_cmd_resp.data.hswing_mv == 0x02) set_hswing_pos("Move left");
        else if (m_get_cmd_resp.data.hswing_mv == 0x03) set_hswing_pos("Move mid");
        else if (m_get_cmd_resp.data.hswing_mv == 0x04) set_hswing_pos("Move right");
        else if (m_get_cmd_resp.data.hswing_fix == 0x01) set_hswing_pos("Fix left");
        else if (m_get_cmd_resp.data.hswing_fix == 0x02) set_hswing_pos("Fix mid left");
        else if (m_get_cmd_resp.data.hswing_fix == 0x03) set_hswing_pos("Fix mid");
        else if (m_get_cmd_resp.data.hswing_fix == 0x04) set_hswing_pos("Fix mid right");
        else if (m_get_cmd_resp.data.hswing_fix == 0x05) set_hswing_pos("Fix right");
        else set_hswing_pos("Last position");

        // ── Publish internal AC temperature (debug) ───────────────────
        if (this->int_temp_output_ != nullptr)
          this->int_temp_output_->publish_state(curr_temp);

        if (this->regulation_enabled()) {
          // ── External sensor regulation ────────────────────────────────────────
          // Determine the external reading per the selected method
          float ext_temp = 0.0f;
          bool  ext_ok   = false;
          bool  s1 = this->ext_temp_sensor_->has_state();
          bool  s2 = (this->ext_temp_sensor_2_ != nullptr) && this->ext_temp_sensor_2_->has_state();

          if (this->ext_temp_method_ == "Average" && this->ext_temp_sensor_2_ != nullptr) {
            if (s1 && s2) { ext_temp = (this->ext_temp_sensor_->get_state() + this->ext_temp_sensor_2_->get_state()) / 2.0f; ext_ok = true; }
            else if (s1)  { ext_temp = this->ext_temp_sensor_->get_state();   ext_ok = true; }
            else if (s2)  { ext_temp = this->ext_temp_sensor_2_->get_state(); ext_ok = true; }
          } else if (this->ext_temp_method_ == "Primary with fallback" && this->ext_temp_sensor_2_ != nullptr) {
            if (s1)      { ext_temp = this->ext_temp_sensor_->get_state();   ext_ok = true; }
            else if (s2) { ext_temp = this->ext_temp_sensor_2_->get_state(); ext_ok = true; }
          } else {
            if (s1)      { ext_temp = this->ext_temp_sensor_->get_state();   ext_ok = true; }
          }

          // Initialize the setpoint if the user has not set one yet
          if (!this->user_has_set_target_) {
            this->set_target_temperature(this->fallback_temperature_);
            this->user_has_set_target_ = true;
          }

          if (ext_ok) {
            if (this->ext_temp_output_ != nullptr) this->ext_temp_output_->publish_state(ext_temp);
            this->set_current_temperature(ext_temp);
            this->run_regulation(curr_temp, ext_temp);
          } else {
            // No external reading available: fall back to the AC's internal reading
            this->set_current_temperature(curr_temp);
          }

          if (this->ext_temp_2_output_ != nullptr && s2)
            this->ext_temp_2_output_->publish_state(this->ext_temp_sensor_2_->get_state());
          if (this->sent_temp_output_ != nullptr)
            this->sent_temp_output_->publish_state(this->last_sent_temp_);
        } else {
          // ── Original behavior (no external regulation) ────────────────────
          this->set_current_temperature(curr_temp);

          // Take the target temperature from the AC.
          // The AC encodes temp as T - 16 in the reply (direct, not inverted)
          float ac_target = static_cast<float>(m_get_cmd_resp.data.temp + 16);
          if (m_get_cmd_resp.data.half_degree) ac_target += 0.5f;
          this->set_target_temperature(ac_target);
        }

        // ── Pass through power sensor (smart plug) ────────────────────────
        if (this->power_sensor_ != nullptr && this->power_sensor_->has_state() && this->power_output_ != nullptr)
          this->power_output_->publish_state(this->power_sensor_->get_state());

        // ── Derive active preset from internal state ─────────────────────
        this->update_preset_state();

        if (this->is_changed) this->publish_state();
      }
    }
  }
}

}  // namespace tcl_climate
}  // namespace esphome
