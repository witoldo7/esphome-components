#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace as5048b {

class AS5048BComponent : public PollingComponent, public i2c::I2CDevice {
 public:
  void set_angle_sensor(sensor::Sensor *s) { angle_sensor_ = s; }
  void set_raw_angle_sensor(sensor::Sensor *s) { raw_angle_sensor_ = s; }
  void set_magnitude_sensor(sensor::Sensor *s) { magnitude_sensor_ = s; }
  void set_gain_sensor(sensor::Sensor *s) { gain_sensor_ = s; }
#ifdef USE_TEXT_SENSOR
  void set_direction_text_sensor(text_sensor::TextSensor *s) { direction_text_sensor_ = s; }
#endif
  void set_offset(float deg) { offset_ = deg; }
  void set_direction_levels(uint8_t n) { direction_levels_ = n; }
  void set_invert(bool inv) { invert_ = inv; }

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  bool read_angle_raw_(uint16_t *raw);
  bool read_magnitude_raw_(uint16_t *raw);
  bool read_gain_raw_(uint8_t *agc);
  const char *direction_for_(float angle_deg) const;

  sensor::Sensor *angle_sensor_{nullptr};
  sensor::Sensor *raw_angle_sensor_{nullptr};
  sensor::Sensor *magnitude_sensor_{nullptr};
  sensor::Sensor *gain_sensor_{nullptr};
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *direction_text_sensor_{nullptr};
#endif
  float offset_{0.0f};
  uint8_t direction_levels_{16};
  bool invert_{false};
};

}  // namespace as5048b
}  // namespace esphome
