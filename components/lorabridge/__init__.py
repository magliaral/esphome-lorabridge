import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

lorabridge_ns = cg.esphome_ns.namespace("lorabridge")
LoRaBridge = lorabridge_ns.class_("LoRaBridge", cg.Component)

CONF_REGION = "region"
CONF_SUB_BAND = "sub_band"
CONF_JOIN_EUI = "join_eui"
CONF_DEV_EUI = "dev_eui"
CONF_APP_KEY = "app_key"
CONF_NWK_KEY = "nwk_key"
CONF_UPLINK_INTERVAL = "uplink_interval"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LoRaBridge),
        cv.Required(CONF_REGION): cv.one_of("EU868", "US915", "AU915", "AS923", "AS923_2", "AS923_3", "AS923_4", "IN865", "KR920", "CN500", upper=True),
        cv.Optional(CONF_SUB_BAND, default=0): cv.uint8_t,
        cv.Required(CONF_JOIN_EUI): cv.All(cv.string, lambda value: validate_hex_length(value, 16, "join_eui")),
        cv.Required(CONF_DEV_EUI): cv.All(cv.string, lambda value: validate_hex_length(value, 16, "dev_eui")),
        cv.Required(CONF_APP_KEY): cv.All(cv.string, lambda value: validate_hex_length(value, 32, "app_key")),
        cv.Optional(CONF_NWK_KEY, default="00000000000000000000000000000000"): cv.All(cv.string, lambda value: validate_hex_length(value, 32, "nwk_key")),
        cv.Optional(CONF_UPLINK_INTERVAL, default=60): cv.uint32_t,
    }
).extend(cv.COMPONENT_SCHEMA)

def validate_hex_length(value, length, name):
    if len(value) != length:
        raise cv.Invalid(f"{name} muss genau {length} Zeichen lang sein")
    try:
        int(value, 16)
    except ValueError:
        raise cv.Invalid(f"{name} muss ein gültiger HEX-String sein")
    return value

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # **Setze die Region basierend auf der YAML-Konfiguration**
    region_str = config[CONF_REGION]
    region_expr = cg.RawExpression(f"{region_str}")
    cg.add(var.set_region(region_expr))

    # sub_band
    cg.add(var.set_sub_band(config[CONF_SUB_BAND]))

    # join_eui
    join_eui_str = config[CONF_JOIN_EUI]
    join_eui_int = int(join_eui_str, 16)
    cg.add(var.set_join_eui(join_eui_int))
    
    # dev_eui
    dev_eui_str = config[CONF_DEV_EUI]
    dev_eui_int = int(dev_eui_str, 16)
    cg.add(var.set_dev_eui(dev_eui_int))
    
    # app_key
    app_key_str = config[CONF_APP_KEY]
    try:
        app_key_bytes = bytes.fromhex(app_key_str)
    except ValueError:
        raise cv.Invalid("app_key muss ein gültiger HEX-String sein")
    
    if len(app_key_bytes) != 16:
        raise cv.Invalid("app_key muss 32 HEX-Zeichen enthalten (16 Bytes)")
    
    # Erstellen einer C++ Initializer-Liste für std::array<uint8_t, 16>
    app_key_expr = "std::array<uint8_t, 16>{" + ", ".join(f"0x{b:02X}" for b in app_key_bytes) + "}"
    app_key_initializer = cg.RawExpression(app_key_expr)
    cg.add(var.set_app_key(app_key_initializer))

    # nwk_key
    nwk_key_str = config[CONF_NWK_KEY]
    try:
        nwk_key_bytes = bytes.fromhex(nwk_key_str)
    except ValueError:
        raise cv.Invalid("nwk_key muss ein gültiger HEX-String sein")
    
    if len(nwk_key_bytes) != 16:
        raise cv.Invalid("nwk_key muss 32 HEX-Zeichen enthalten (16 Bytes)")
    
    # Erstellen einer C++ Initializer-Liste für std::array<uint8_t, 16>
    nwk_key_expr = "std::array<uint8_t, 16>{" + ", ".join(f"0x{b:02X}" for b in nwk_key_bytes) + "}"
    nwk_key_initializer = cg.RawExpression(nwk_key_expr)
    cg.add(var.set_nwk_key(nwk_key_initializer))

    # uplink_interval
    cg.add(var.set_uplink_interval(config[CONF_UPLINK_INTERVAL]))
