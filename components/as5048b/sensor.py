import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    STATE_CLASS_MEASUREMENT,
    UNIT_DEGREES,
)

from . import AS5048BComponent, CONF_AS5048B_ID

DEPENDENCIES = ["as5048b"]

CONF_ANGLE = "angle"
CONF_RAW_ANGLE = "raw_angle"
CONF_MAGNITUDE = "magnitude"
CONF_GAIN = "gain"

ICON_COMPASS = "mdi:compass"
ICON_MAGNET = "mdi:magnet"
ICON_SIGNAL = "mdi:signal"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_AS5048B_ID): cv.use_id(AS5048BComponent),
        cv.Optional(CONF_ANGLE): sensor.sensor_schema(
            unit_of_measurement=UNIT_DEGREES,
            accuracy_decimals=2,
            icon=ICON_COMPASS,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_RAW_ANGLE): sensor.sensor_schema(
            accuracy_decimals=0,
            icon=ICON_COMPASS,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_MAGNITUDE): sensor.sensor_schema(
            accuracy_decimals=0,
            icon=ICON_MAGNET,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_GAIN): sensor.sensor_schema(
            accuracy_decimals=0,
            icon=ICON_SIGNAL,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AS5048B_ID])
    if conf := config.get(CONF_ANGLE):
        sens = await sensor.new_sensor(conf)
        cg.add(parent.set_angle_sensor(sens))
    if conf := config.get(CONF_RAW_ANGLE):
        sens = await sensor.new_sensor(conf)
        cg.add(parent.set_raw_angle_sensor(sens))
    if conf := config.get(CONF_MAGNITUDE):
        sens = await sensor.new_sensor(conf)
        cg.add(parent.set_magnitude_sensor(sens))
    if conf := config.get(CONF_GAIN):
        sens = await sensor.new_sensor(conf)
        cg.add(parent.set_gain_sensor(sens))
