import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import LoRaBridge, CONF_LORABRIDGE_ID, vgw_enabled

# Diagnostic entity: active transport mode ("radio" / "capture")
CONFIG_SCHEMA = text_sensor.text_sensor_schema(
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    icon="mdi:swap-horizontal",
).extend({
    cv.GenerateID(CONF_LORABRIDGE_ID): cv.use_id(LoRaBridge),
})


def _final_validate(config):
    full = fv.full_config.get()
    if not vgw_enabled(full.get("lorabridge", {})):
        raise cv.Invalid(
            "The lorabridge text_sensor platform requires virtual_gateway to be enabled"
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LORABRIDGE_ID])
    sens = await text_sensor.new_text_sensor(config)
    cg.add(parent.set_transport_mode_text_sensor(sens))
