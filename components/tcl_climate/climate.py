import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, uart, select, switch
from esphome.const import CONF_ID
from esphome.components.tcl_climate import tcl_climate_ns

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["select", "switch"]

TCLClimate = tcl_climate_ns.class_(
    "TCLClimate", climate.Climate, uart.UARTDevice, cg.PollingComponent
)

CONF_VSWING_SELECT  = "vswing_select"
CONF_HSWING_SELECT  = "hswing_select"
CONF_SLEEP_SELECT   = "sleep_select"
CONF_PRESET_SELECT  = "preset_select"
CONF_BEEP_SWITCH    = "beep_switch"
CONF_DISPLAY_SWITCH = "display_switch"
CONF_ECO_SWITCH     = "eco_switch"
CONF_8DEG_SWITCH    = "8deg_switch"
CONF_SUPPORTED_MODE = "supported_swing_modes"

CONFIG_SCHEMA = climate.climate_schema(TCLClimate).extend({
    cv.Optional(CONF_VSWING_SELECT):  cv.use_id(select.Select),
    cv.Optional(CONF_HSWING_SELECT):  cv.use_id(select.Select),
    cv.Optional(CONF_SLEEP_SELECT):   cv.use_id(select.Select),
    cv.Optional(CONF_PRESET_SELECT):  cv.use_id(select.Select),
    cv.Optional(CONF_BEEP_SWITCH):    cv.use_id(switch.Switch),
    cv.Optional(CONF_DISPLAY_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_ECO_SWITCH):     cv.use_id(switch.Switch),
    cv.Optional(CONF_8DEG_SWITCH):    cv.use_id(switch.Switch),
    cv.Optional(CONF_SUPPORTED_MODE): cv.ensure_list(climate.validate_climate_swing_mode),
}).extend(uart.UART_DEVICE_SCHEMA).extend(cv.polling_component_schema("450ms"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await uart.register_uart_device(var, config)

    if CONF_SUPPORTED_MODE in config:
        modes = config[CONF_SUPPORTED_MODE]
        cg.add(var.set_supported_swing_modes(modes))

    for conf_key, setter in [
        (CONF_VSWING_SELECT,  "set_vswing_select"),
        (CONF_HSWING_SELECT,  "set_hswing_select"),
        (CONF_SLEEP_SELECT,   "set_sleep_select"),
        (CONF_PRESET_SELECT,  "set_preset_select"),
        (CONF_BEEP_SWITCH,    "set_beep_switch"),
        (CONF_DISPLAY_SWITCH, "set_display_switch"),
        (CONF_ECO_SWITCH,     "set_eco_switch"),
        (CONF_8DEG_SWITCH,    "set_8deg_switch"),
    ]:
        if conf_key in config:
            obj = await cg.get_variable(config[conf_key])
            cg.add(getattr(var, setter)(obj))
