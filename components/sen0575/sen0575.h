#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace sen0575 {

class SEN0575Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void set_rainfall_sensor(sensor::Sensor *s) { rainfall_sensor_ = s; }
  void set_rainfall_hour_sensor(sensor::Sensor *s) { rainfall_hour_sensor_ = s; }
  void set_rainfall_hour(uint8_t hour) { rainfall_hour_ = hour; }
  void set_raw_data_sensor(sensor::Sensor *s) { raw_data_sensor_ = s; }
  void set_working_time_sensor(sensor::Sensor *s) { working_time_sensor_ = s; }
  void set_rain_accumulated_value(float v) {
    rain_accumulated_value_ = v;
    has_rain_accumulated_value_ = true;
  }

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  bool read_pid_vid_(uint32_t *pid, uint32_t *vid);
  bool read_u32_le_(uint8_t reg, uint32_t *out);
  bool read_u16_le_(uint8_t reg, uint16_t *out);
  bool write_u16_le_(uint8_t reg, uint16_t value);
  bool write_u8_(uint8_t reg, uint8_t value);

  sensor::Sensor *rainfall_sensor_{nullptr};
  sensor::Sensor *rainfall_hour_sensor_{nullptr};
  sensor::Sensor *raw_data_sensor_{nullptr};
  sensor::Sensor *working_time_sensor_{nullptr};
  uint8_t rainfall_hour_{1};
  float rain_accumulated_value_{0.0f};
  bool has_rain_accumulated_value_{false};
};

}  // namespace sen0575
}  // namespace esphome
