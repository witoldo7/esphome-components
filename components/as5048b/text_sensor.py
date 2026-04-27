import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import AS5048BComponent, CONF_AS5048B_ID

DEPENDENCIES = ["as5048b"]

CONF_DIRECTION = "direction"

ICON_COMPASS = "mdi:compass"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_AS5048B_ID): cv.use_id(AS5048BComponent),
        cv.Optional(CONF_DIRECTION): text_sensor.text_sensor_schema(
            icon=ICON_COMPASS,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AS5048B_ID])
    if conf := config.get(CONF_DIRECTION):
        ts = await text_sensor.new_text_sensor(conf)
        cg.add(parent.set_direction_text_sensor(ts))
