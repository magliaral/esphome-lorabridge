import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
)
from esphome.components import sensor, binary_sensor, text_sensor

DEPENDENCIES = ["spi"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor"]

lorabridge_ns = cg.esphome_ns.namespace("lorabridge")
LoRaBridge = lorabridge_ns.class_("LoRaBridge", cg.Component)

CONF_RADIO = "radio"
CONF_CHIP = "chip"
CONF_REGION = "region"
CONF_SUB_BAND = "sub_band"
CONF_NSS_PIN = "nss_pin"
CONF_RST_PIN = "rst_pin"
CONF_IRQ_PIN = "irq_pin"
CONF_BUSY_PIN = "busy_pin"
CONF_GPIO_PIN = "gpio_pin"
CONF_JOIN_DR = "join_dr"
CONF_SCAN_GUARD = "scan_guard"
CONF_NETWORK = "network"
CONF_JOIN_EUI = "join_eui"
CONF_DEV_EUI = "dev_eui"
CONF_APP_KEY = "app_key"
CONF_NWK_KEY = "nwk_key"
CONF_UPLINK_INTERVAL = "uplink_interval"
CONF_PAYLOAD = "payload"

# Must match the chips handled in LoRaBridge::createRadio()
SUPPORTED_CHIPS = [
    "SX1261", "SX1262", "SX1268",
    "SX1272", "SX1276", "SX1277", "SX1278", "SX1279",
    "LR1110", "LR1120", "LR1121",
]


def validate_hex_length(value, length, name):
    if len(value) != length:
        raise cv.Invalid(f"{name} must be exactly {length} characters")
    try:
        int(value, 16)
    except ValueError:
        raise cv.Invalid(f"{name} must be a valid hex string")
    return value


SENSOR_PAYLOAD_ITEM_SCHEMA = cv.Schema({
    cv.Required("sensor"): cv.use_id(sensor.Sensor),
    cv.Optional("multiplier", default=1): cv.float_,
    cv.Optional("offset", default=0): cv.float_,
    cv.Optional("bytes", default=2): cv.int_range(min=1, max=4),
})

BINARY_PAYLOAD_ITEM_SCHEMA = cv.Schema({
    cv.Required("binary_sensor"): cv.use_id(binary_sensor.BinarySensor),
})

PAYLOAD_TEXT_ITEM_SCHEMA = cv.Schema({
    cv.Required("text_sensor"): cv.use_id(text_sensor.TextSensor),
})

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LoRaBridge),
        cv.Required(CONF_RADIO): cv.Schema({
            cv.Required(CONF_CHIP): cv.one_of(*SUPPORTED_CHIPS, upper=True),
            cv.Required(CONF_REGION): cv.one_of(
                "EU868", "US915", "AU915", "AS923", "AS923_2", "AS923_3",
                "AS923_4", "IN865", "KR920", "CN500", upper=True),
            cv.Optional(CONF_SUB_BAND, default=0): cv.uint8_t,
            cv.Required(CONF_NSS_PIN): cv.int_,
            cv.Required(CONF_RST_PIN): cv.int_,
            cv.Required(CONF_IRQ_PIN): cv.int_,
            cv.Optional(CONF_BUSY_PIN, default=-1): cv.int_,
            cv.Optional(CONF_GPIO_PIN, default=-1): cv.int_,
            cv.Optional(CONF_JOIN_DR, default=0): cv.int_range(min=0, max=15),
            cv.Optional(CONF_SCAN_GUARD, default=50): cv.uint16_t,
        }),
        cv.Required(CONF_NETWORK): cv.Schema({
            cv.Required(CONF_JOIN_EUI): cv.All(
                cv.string, lambda value: validate_hex_length(value, 16, "join_eui")),
            cv.Required(CONF_DEV_EUI): cv.All(
                cv.string, lambda value: validate_hex_length(value, 16, "dev_eui")),
            cv.Required(CONF_APP_KEY): cv.All(
                cv.string, lambda value: validate_hex_length(value, 32, "app_key")),
            cv.Optional(CONF_NWK_KEY, default="00000000000000000000000000000000"): cv.All(
                cv.string, lambda value: validate_hex_length(value, 32, "nwk_key")),
        }),
        cv.Optional(CONF_UPLINK_INTERVAL, default=300): cv.uint32_t,
        cv.Optional(CONF_PAYLOAD, default={}): cv.Schema({
            cv.Optional("sensors", default=[]): cv.ensure_list(SENSOR_PAYLOAD_ITEM_SCHEMA),
            cv.Optional("binary_sensors", default=[]): cv.ensure_list(BINARY_PAYLOAD_ITEM_SCHEMA),
            cv.Optional("text_sensors", default=[]): cv.ensure_list(PAYLOAD_TEXT_ITEM_SCHEMA),
        }),
    }
).extend(cv.COMPONENT_SCHEMA)


def key_initializer(hex_str):
    # Build a C++ initializer list for std::array<uint8_t, 16>
    key_bytes = bytes.fromhex(hex_str)
    return cg.RawExpression(
        "std::array<uint8_t, 16>{" + ", ".join(f"0x{b:02X}" for b in key_bytes) + "}"
    )


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    radio = config[CONF_RADIO]

    # Chip type
    cg.add(var.set_chip(radio[CONF_CHIP]))

    # Region (RadioLib band constant, e.g. EU868)
    region_expr = cg.RawExpression(f"{radio[CONF_REGION]}")
    cg.add(var.set_region(region_expr))

    # Sub-band
    cg.add(var.set_sub_band(radio[CONF_SUB_BAND]))

    # Radio pins
    cg.add(var.set_nss_pin(radio[CONF_NSS_PIN]))
    cg.add(var.set_rst_pin(radio[CONF_RST_PIN]))
    cg.add(var.set_irq_pin(radio[CONF_IRQ_PIN]))
    cg.add(var.set_busy_pin(radio[CONF_BUSY_PIN]))
    cg.add(var.set_gpio_pin(radio[CONF_GPIO_PIN]))

    # Join data rate + RX scan guard
    cg.add(var.set_join_dr(radio[CONF_JOIN_DR]))
    cg.add(var.set_scan_guard(radio[CONF_SCAN_GUARD]))

    # Network credentials
    network = config[CONF_NETWORK]
    cg.add(var.set_join_eui(int(network[CONF_JOIN_EUI], 16)))
    cg.add(var.set_dev_eui(int(network[CONF_DEV_EUI], 16)))
    cg.add(var.set_app_key(key_initializer(network[CONF_APP_KEY])))
    cg.add(var.set_nwk_key(key_initializer(network[CONF_NWK_KEY])))

    # Uplink interval
    cg.add(var.set_uplink_interval(config[CONF_UPLINK_INTERVAL]))

    # Sensor payload
    for item in config[CONF_PAYLOAD].get("sensors", []):
        sens_var = await cg.get_variable(item["sensor"])
        cg.add(var.add_sensor_payload_item(
            sens_var, item["multiplier"], item["offset"], item["bytes"]))

    # Binary sensor payload
    for item in config[CONF_PAYLOAD].get("binary_sensors", []):
        bin_sens_var = await cg.get_variable(item["binary_sensor"])
        cg.add(var.add_binary_payload_item(bin_sens_var))

    # Text sensor payload
    for item in config[CONF_PAYLOAD].get("text_sensors", []):
        text_sens_var = await cg.get_variable(item["text_sensor"])
        cg.add(var.add_text_payload_item(text_sens_var))
