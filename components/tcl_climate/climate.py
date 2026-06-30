import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, uart, select, switch, sensor
from esphome.const import (
    CONF_ID,
    UNIT_CELSIUS,
    UNIT_WATT,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_POWER,
    STATE_CLASS_MEASUREMENT,
)
from esphome.components.tcl_climate import tcl_climate_ns

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["select", "switch", "sensor"]

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

# ── Externe Sensor-Regelung (proportionaler Thermostat) ──────────────────────
CONF_EXT_TEMP_SENSOR     = "ext_temp_sensor"
CONF_EXT_TEMP_SENSOR_2   = "ext_temp_sensor_2"
CONF_POWER_SENSOR        = "power_sensor"
CONF_INTERNAL_TEMP       = "internal_temp"
CONF_EXT_TEMP            = "ext_temp"
CONF_EXT_TEMP_2          = "ext_temp_2"
CONF_POWER               = "power"
CONF_SENT_TEMP           = "sent_temp"
CONF_FALLBACK_TEMP       = "fallback_temperature"
CONF_REGULATION_INTERVAL = "regulation_interval"
CONF_EXT_TEMP_METHOD     = "ext_temp_method"

TEMP_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
)

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

    # External sensor regulation (all optional; regulation activates only when
    # ext_temp_sensor is set):
    cv.Optional(CONF_EXT_TEMP_SENSOR):   cv.use_id(sensor.Sensor),
    cv.Optional(CONF_EXT_TEMP_SENSOR_2): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_POWER_SENSOR):      cv.use_id(sensor.Sensor),
    cv.Optional(CONF_INTERNAL_TEMP):     TEMP_SENSOR_SCHEMA,
    cv.Optional(CONF_EXT_TEMP):          TEMP_SENSOR_SCHEMA,
    cv.Optional(CONF_EXT_TEMP_2):        TEMP_SENSOR_SCHEMA,
    cv.Optional(CONF_SENT_TEMP):         TEMP_SENSOR_SCHEMA,
    cv.Optional(CONF_POWER): sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_POWER,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    cv.Optional(CONF_FALLBACK_TEMP, default=23.0): cv.temperature,
    cv.Optional(CONF_REGULATION_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_EXT_TEMP_METHOD, default="Primary"): cv.one_of(
        "Primary", "Primary with fallback", "Average"
    ),
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

    # ── Externe Sensor-Regelung ──────────────────────────────────────────────
    # Input sensors referenced by id (defined elsewhere under `sensor:`)
    for conf_key, setter in [
        (CONF_EXT_TEMP_SENSOR,   "set_ext_temp_sensor"),
        (CONF_EXT_TEMP_SENSOR_2, "set_ext_temp_sensor_2"),
        (CONF_POWER_SENSOR,      "set_power_sensor"),
    ]:
        if conf_key in config:
            obj = await cg.get_variable(config[conf_key])
            cg.add(getattr(var, setter)(obj))

    # Output sensors created and owned by this component
    for conf_key, setter in [
        (CONF_INTERNAL_TEMP, "set_int_temp_output"),
        (CONF_EXT_TEMP,      "set_ext_temp_output"),
        (CONF_EXT_TEMP_2,    "set_ext_temp_2_output"),
        (CONF_POWER,         "set_power_output"),
        (CONF_SENT_TEMP,     "set_sent_temp_output"),
    ]:
        if conf_key in config:
            sens = await sensor.new_sensor(config[conf_key])
            cg.add(getattr(var, setter)(sens))

    cg.add(var.set_fallback_temperature(config[CONF_FALLBACK_TEMP]))
    cg.add(var.set_regulation_interval(config[CONF_REGULATION_INTERVAL]))
    cg.add(var.set_ext_temp_method(config[CONF_EXT_TEMP_METHOD]))
