import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_CONNECTIVITY, ENTITY_CATEGORY_DIAGNOSTIC

from . import LoRaBridge, CONF_LORABRIDGE_ID, vgw_enabled

# Diagnostic entity: virtual gateway connection (PULL_ACK seen recently)
CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(
    device_class=DEVICE_CLASS_CONNECTIVITY,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend({
    cv.GenerateID(CONF_LORABRIDGE_ID): cv.use_id(LoRaBridge),
})


def _final_validate(config):
    full = fv.full_config.get()
    if not vgw_enabled(full.get("lorabridge", {})):
        raise cv.Invalid(
            "The lorabridge binary_sensor platform requires virtual_gateway to be enabled"
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LORABRIDGE_ID])
    sens = await binary_sensor.new_binary_sensor(config)
    cg.add(parent.set_gateway_connected_binary_sensor(sens))
