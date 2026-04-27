import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_PRECIPITATION,
    ICON_COUNTER,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_HOUR,
    UNIT_MILLIMETER,
)

CODEOWNERS = ["@witoldo7"]
DEPENDENCIES = ["i2c"]

sen0575_ns = cg.esphome_ns.namespace("sen0575")
SEN0575Component = sen0575_ns.class_(
    "SEN0575Component", cg.PollingComponent, i2c.I2CDevice
)

CONF_RAINFALL = "rainfall"
CONF_RAINFALL_HOUR = "rainfall_hour"
CONF_HOUR = "hour"
CONF_RAW_DATA = "raw_data"
CONF_WORKING_TIME = "working_time"
CONF_RAIN_ACCUMULATED_VALUE = "rain_accumulated_value"

ICON_WEATHER_RAINY = "mdi:weather-rainy"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SEN0575Component),
            cv.Optional(CONF_RAINFALL): sensor.sensor_schema(
                unit_of_measurement=UNIT_MILLIMETER,
                accuracy_decimals=4,
                device_class=DEVICE_CLASS_PRECIPITATION,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon=ICON_WEATHER_RAINY,
            ),
            cv.Optional(CONF_RAINFALL_HOUR): sensor.sensor_schema(
                unit_of_measurement=UNIT_MILLIMETER,
                accuracy_decimals=4,
                device_class=DEVICE_CLASS_PRECIPITATION,
                icon=ICON_WEATHER_RAINY,
            ).extend(
                {
                    cv.Optional(CONF_HOUR, default=1): cv.int_range(min=1, max=24),
                }
            ),
            cv.Optional(CONF_RAW_DATA): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon=ICON_COUNTER,
            ),
            cv.Optional(CONF_WORKING_TIME): sensor.sensor_schema(
                unit_of_measurement=UNIT_HOUR,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_RAIN_ACCUMULATED_VALUE): cv.float_range(
                min=0.0001, max=6.5535
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x1D))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    if rainfall_conf := config.get(CONF_RAINFALL):
        sens = await sensor.new_sensor(rainfall_conf)
        cg.add(var.set_rainfall_sensor(sens))

    if hour_conf := config.get(CONF_RAINFALL_HOUR):
        sens = await sensor.new_sensor(hour_conf)
        cg.add(var.set_rainfall_hour_sensor(sens))
        cg.add(var.set_rainfall_hour(hour_conf[CONF_HOUR]))

    if raw_conf := config.get(CONF_RAW_DATA):
        sens = await sensor.new_sensor(raw_conf)
        cg.add(var.set_raw_data_sensor(sens))

    if wt_conf := config.get(CONF_WORKING_TIME):
        sens = await sensor.new_sensor(wt_conf)
        cg.add(var.set_working_time_sensor(sens))

    if (val := config.get(CONF_RAIN_ACCUMULATED_VALUE)) is not None:
        cg.add(var.set_rain_accumulated_value(val))
