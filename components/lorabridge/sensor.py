import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC, STATE_CLASS_TOTAL_INCREASING

from . import LoRaBridge, CONF_LORABRIDGE_ID, vgw_enabled

# Diagnostic entity: uplink copies confirmed by the server via PUSH_ACK.
# Says nothing about whether the network server used the frame or dropped
# it as a duplicate.
CONFIG_SCHEMA = sensor.sensor_schema(
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    state_class=STATE_CLASS_TOTAL_INCREASING,
    icon="mdi:upload-network",
    accuracy_decimals=0,
).extend({
    cv.GenerateID(CONF_LORABRIDGE_ID): cv.use_id(LoRaBridge),
})


def _final_validate(config):
    full = fv.full_config.get()
    if not vgw_enabled(full.get("lorabridge", {})):
        raise cv.Invalid(
            "The lorabridge sensor platform requires virtual_gateway to be enabled"
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LORABRIDGE_ID])
    sens = await sensor.new_sensor(config)
    cg.add(parent.set_uplinks_forwarded_sensor(sens))
