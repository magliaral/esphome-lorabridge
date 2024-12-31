import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

lorabridge_ns = cg.esphome_ns.namespace("lorabridge")
LoRaBridge = lorabridge_ns.class_("LoRaBridge", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LoRaBridge),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
