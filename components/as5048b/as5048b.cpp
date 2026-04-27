#include "as5048b.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace as5048b {

static const char *const TAG = "as5048b";

static const uint8_t REG_AGC = 0xFA;
static const uint8_t REG_MAGNITUDE_MSB = 0xFC;
static const uint8_t REG_ANGLE_MSB = 0xFE;

static const float ANGLE_LSB_DEG = 360.0f / 16384.0f;

static const char *const DIR16[16] = {
    "N",  "NNE", "NE", "ENE", "E",  "ESE", "SE", "SSE",
    "S",  "SSW", "SW", "WSW", "W",  "WNW", "NW", "NNW",
};
static const char *const DIR8[8] = {
    "N", "NE", "E", "SE", "S", "SW", "W", "NW",
};

void AS5048BComponent::setup() {
  uint8_t agc;
  if (!read_gain_raw_(&agc)) {
    ESP_LOGE(TAG, "AS5048B not responding on I2C");
    this->mark_failed();
  }
}

void AS5048BComponent::update() {
  uint16_t raw_angle = 0;
  bool angle_ok = read_angle_raw_(&raw_angle);

  float deg = NAN;
  if (angle_ok) {
    deg = raw_angle * ANGLE_LSB_DEG;
    if (invert_)
      deg = 360.0f - deg;
    deg -= offset_;
    deg = fmodf(deg, 360.0f);
    if (deg < 0)
      deg += 360.0f;
  }

  if (raw_angle_sensor_ != nullptr)
    raw_angle_sensor_->publish_state(angle_ok ? (float) raw_angle : NAN);
  if (angle_sensor_ != nullptr)
    angle_sensor_->publish_state(deg);
#ifdef USE_TEXT_SENSOR
  if (direction_text_sensor_ != nullptr)
    direction_text_sensor_->publish_state(angle_ok ? direction_for_(deg) : "");
#endif

  if (magnitude_sensor_ != nullptr) {
    uint16_t mag = 0;
    magnitude_sensor_->publish_state(read_magnitude_raw_(&mag) ? (float) mag : NAN);
  }

  if (gain_sensor_ != nullptr) {
    uint8_t agc = 0;
    gain_sensor_->publish_state(read_gain_raw_(&agc) ? (float) agc : NAN);
  }
}

bool AS5048BComponent::read_angle_raw_(uint16_t *raw) {
  uint8_t buf[2] = {0};
  if (this->read_register(REG_ANGLE_MSB, buf, 2) != i2c::ERROR_OK)
    return false;
  *raw = (uint16_t) (((uint16_t) buf[0] << 6) | (buf[1] & 0x3F));
  return true;
}

bool AS5048BComponent::read_magnitude_raw_(uint16_t *raw) {
  uint8_t buf[2] = {0};
  if (this->read_register(REG_MAGNITUDE_MSB, buf, 2) != i2c::ERROR_OK)
    return false;
  *raw = (uint16_t) (((uint16_t) buf[0] << 6) | (buf[1] & 0x3F));
  return true;
}

bool AS5048BComponent::read_gain_raw_(uint8_t *agc) {
  return this->read_register(REG_AGC, agc, 1) == i2c::ERROR_OK;
}

const char *AS5048BComponent::direction_for_(float angle_deg) const {
  uint8_t levels = direction_levels_;
  float step = 360.0f / levels;
  uint8_t idx = (uint8_t) ((angle_deg + step * 0.5f) / step) % levels;
  return (levels == 8) ? DIR8[idx] : DIR16[idx];
}

void AS5048BComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "AS5048B Magnetic Encoder:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed())
    ESP_LOGE(TAG, "  Communication with AS5048B failed");
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  Offset: %.2f°", offset_);
  ESP_LOGCONFIG(TAG, "  Invert: %s", invert_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Direction levels: %u", (unsigned) direction_levels_);
  LOG_SENSOR("  ", "Angle", angle_sensor_);
  LOG_SENSOR("  ", "Raw Angle", raw_angle_sensor_);
  LOG_SENSOR("  ", "Magnitude", magnitude_sensor_);
  LOG_SENSOR("  ", "Gain", gain_sensor_);
#ifdef USE_TEXT_SENSOR
  if (direction_text_sensor_ != nullptr)
    LOG_TEXT_SENSOR("  ", "Direction", direction_text_sensor_);
#endif
}

}  // namespace as5048b
}  // namespace esphome
