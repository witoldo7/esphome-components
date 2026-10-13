import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button

from . import CONF_INVERTER_IAP_ID, InverterIAP, inverter_iap_ns

DEPENDENCIES = ["inverter_iap"]

InverterIAPButton = inverter_iap_ns.class_("InverterIAPButton", button.Button)

CONF_ACTION = "action"
ACTIONS = {"update": 0, "abort": 1, "query": 2}

CONFIG_SCHEMA = button.button_schema(InverterIAPButton).extend(
    {
        cv.GenerateID(CONF_INVERTER_IAP_ID): cv.use_id(InverterIAP),
        cv.Optional(CONF_ACTION, default="update"): cv.enum(ACTIONS, lower=True),
    }
)


async def to_code(config):
    var = await button.new_button(config)
    parent = await cg.get_variable(config[CONF_INVERTER_IAP_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_action(config[CONF_ACTION]))
