import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID

CODEOWNERS = ["@witoldo7"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

as5048b_ns = cg.esphome_ns.namespace("as5048b")
AS5048BComponent = as5048b_ns.class_(
    "AS5048BComponent", cg.PollingComponent, i2c.I2CDevice
)

CONF_AS5048B_ID = "as5048b_id"
CONF_OFFSET = "offset"
CONF_DIRECTION_LEVELS = "direction_levels"
CONF_INVERT = "invert"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AS5048BComponent),
            cv.Optional(CONF_OFFSET, default=0.0): cv.float_range(
                min=-360.0, max=360.0
            ),
            cv.Optional(CONF_DIRECTION_LEVELS, default=16): cv.one_of(
                8, 16, int=True
            ),
            cv.Optional(CONF_INVERT, default=False): cv.boolean,
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x40))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_offset(config[CONF_OFFSET]))
    cg.add(var.set_direction_levels(config[CONF_DIRECTION_LEVELS]))
    cg.add(var.set_invert(config[CONF_INVERT]))
