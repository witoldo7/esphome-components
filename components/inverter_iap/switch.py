import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import CONF_INVERTER_IAP_ID, InverterIAP, inverter_iap_ns

DEPENDENCIES = ["inverter_iap"]

InverterIAPForceSwitch = inverter_iap_ns.class_(
    "InverterIAPForceSwitch", switch.Switch
)

CONFIG_SCHEMA = switch.switch_schema(InverterIAPForceSwitch).extend(
    {
        cv.GenerateID(CONF_INVERTER_IAP_ID): cv.use_id(InverterIAP),
    }
)


async def to_code(config):
    var = await switch.new_switch(config)
    parent = await cg.get_variable(config[CONF_INVERTER_IAP_ID])
    cg.add(var.set_parent(parent))
