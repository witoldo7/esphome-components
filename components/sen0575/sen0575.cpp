#include "sen0575.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace sen0575 {

static const char *const TAG = "sen0575";

static const uint8_t REG_PID = 0x00;
static const uint8_t REG_VERSION = 0x0A;
static const uint8_t REG_TIME_RAINFALL = 0x0C;
static const uint8_t REG_CUMULATIVE_RAINFALL = 0x10;
static const uint8_t REG_RAW_DATA = 0x14;
static const uint8_t REG_SYS_TIME = 0x18;
static const uint8_t REG_RAIN_HOUR = 0x26;
static const uint8_t REG_BASE_RAINFALL = 0x28;

static const uint16_t EXPECTED_VID = 0x3343;
static const uint32_t EXPECTED_PID = 0x100C0;

static const uint32_t WRITE_COMMIT_DELAY_MS = 30;

void SEN0575Component::setup() {
  uint32_t pid = 0;
  uint32_t vid = 0;
  if (!read_pid_vid_(&pid, &vid)) {
    ESP_LOGE(TAG, "I2C read of PID/VID failed");
    this->mark_failed();
    return;
  }
  if (vid != EXPECTED_VID || pid != EXPECTED_PID) {
    ESP_LOGE(TAG, "Unexpected VID/PID (vid=0x%04X pid=0x%05X) — not a SEN0575",
             (unsigned) vid, (unsigned) pid);
    this->mark_failed();
    return;
  }
  if (has_rain_accumulated_value_) {
    uint16_t raw = (uint16_t) (rain_accumulated_value_ * 10000.0f);
    if (!write_u16_le_(REG_BASE_RAINFALL, raw)) {
      ESP_LOGW(TAG, "Failed to set rain_accumulated_value");
    }
  }
}

void SEN0575Component::update() {
  if (rainfall_sensor_ != nullptr) {
    uint32_t v = 0;
    if (read_u32_le_(REG_CUMULATIVE_RAINFALL, &v)) {
      rainfall_sensor_->publish_state(v / 10000.0f);
    } else {
      rainfall_sensor_->publish_state(NAN);
    }
  }

  if (rainfall_hour_sensor_ != nullptr) {
    if (write_u8_(REG_RAIN_HOUR, rainfall_hour_)) {
      uint32_t v = 0;
      if (read_u32_le_(REG_TIME_RAINFALL, &v)) {
        rainfall_hour_sensor_->publish_state(v / 10000.0f);
      } else {
        rainfall_hour_sensor_->publish_state(NAN);
      }
    } else {
      rainfall_hour_sensor_->publish_state(NAN);
    }
  }

  if (raw_data_sensor_ != nullptr) {
    uint32_t v = 0;
    if (read_u32_le_(REG_RAW_DATA, &v)) {
      raw_data_sensor_->publish_state((float) v);
    } else {
      raw_data_sensor_->publish_state(NAN);
    }
  }

  if (working_time_sensor_ != nullptr) {
    uint16_t v = 0;
    if (read_u16_le_(REG_SYS_TIME, &v)) {
      working_time_sensor_->publish_state(v / 60.0f);
    } else {
      working_time_sensor_->publish_state(NAN);
    }
  }
}

void SEN0575Component::dump_config() {
  ESP_LOGCONFIG(TAG, "DFRobot SEN0575 Rainfall Sensor:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication with SEN0575 failed!");
  }
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Rainfall", this->rainfall_sensor_);
  if (this->rainfall_hour_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Rainfall window: %u h", (unsigned) this->rainfall_hour_);
  }
  LOG_SENSOR("  ", "Rainfall (window)", this->rainfall_hour_sensor_);
  LOG_SENSOR("  ", "Raw Data", this->raw_data_sensor_);
  LOG_SENSOR("  ", "Working Time", this->working_time_sensor_);
}

bool SEN0575Component::read_pid_vid_(uint32_t *pid, uint32_t *vid) {
  uint8_t buf[4] = {0};
  if (this->read_register(REG_PID, buf, 4) != i2c::ERROR_OK)
    return false;
  *pid = (uint32_t) buf[0] | ((uint32_t) buf[1] << 8) | (((uint32_t) (buf[3] & 0xC0)) << 10);
  *vid = (uint32_t) buf[2] | (((uint32_t) (buf[3] & 0x3F)) << 8);
  return true;
}

bool SEN0575Component::read_u32_le_(uint8_t reg, uint32_t *out) {
  uint8_t buf[4] = {0};
  if (this->read_register(reg, buf, 4) != i2c::ERROR_OK)
    return false;
  *out = (uint32_t) buf[0] | ((uint32_t) buf[1] << 8) | ((uint32_t) buf[2] << 16) | ((uint32_t) buf[3] << 24);
  return true;
}

bool SEN0575Component::read_u16_le_(uint8_t reg, uint16_t *out) {
  uint8_t buf[2] = {0};
  if (this->read_register(reg, buf, 2) != i2c::ERROR_OK)
    return false;
  *out = (uint16_t) buf[0] | ((uint16_t) buf[1] << 8);
  return true;
}

bool SEN0575Component::write_u16_le_(uint8_t reg, uint16_t value) {
  uint8_t buf[2] = {(uint8_t) (value & 0xFF), (uint8_t) ((value >> 8) & 0xFF)};
  if (this->write_register(reg, buf, 2) != i2c::ERROR_OK)
    return false;
  delay(WRITE_COMMIT_DELAY_MS);
  return true;
}

bool SEN0575Component::write_u8_(uint8_t reg, uint8_t value) {
  if (this->write_register(reg, &value, 1) != i2c::ERROR_OK)
    return false;
  delay(WRITE_COMMIT_DELAY_MS);
  return true;
}

}  // namespace sen0575
}  // namespace esphome
