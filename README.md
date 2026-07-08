# esphome-lorabridge

ESPHome LoRaBridge is a custom component for transmitting sensor values over LoRaWAN (OTAA) using the [RadioLib](https://github.com/jgromes/RadioLib) library.

## Features

- LoRaWAN 1.0.4 OTAA join with DevNonce persistence in flash (joins survive reboots without enabling "resets join nonces" in TTN)
- Optional `nwk_key` for LoRaWAN 1.1 networks
- Shares ESPHome's existing SPI bus with other devices (e.g. a display) via a custom RadioLib HAL with proper bus arbitration
- Configurable payload built from ESPHome sensors, binary sensors, and text sensors

Tested on a LILYGO T-Connect-Pro (ESP32-S3, SX1262) against The Things Network, EU868.

## Usage with ESPHome

Add the following configuration to your ESPHome YAML:

```yaml
esphome:
  name: "esphome-lorabridge"
  libraries:
    - SPI
  platformio_options:
    lib_ldf_mode: chain+
    lib_deps:
      - jgromes/RadioLib@^7.1.1

external_components:
  - source:
      type: git
      url: https://github.com/magliaral/esphome-lorabridge.git
      ref: main                # or pin a release tag, e.g. ref: v0.2.0

lorabridge:
  region: EU868
  join_eui: ----------------
  dev_eui: ----------------
  app_key: --------------------------------
  uplink_interval: 300
  payload:
    sensors:
      - sensor: sensor1_id
        multiplier: 1
        offset: 0
        bytes: 2
      - sensor: sensor2_id
    binary_sensors:
      - binary_sensor: binary_sensor1_id
      - binary_sensor: binary_sensor2_id
    text_sensors:
      - text_sensor: text_sensor1_id
      - text_sensor: text_sensor2_id
```

## Configuration variables

- **region** (**Required**): LoRaWAN region/band plan. One of `EU868`, `US915`, `AU915`, `AS923`, `AS923_2`, `AS923_3`, `AS923_4`, `IN865`, `KR920`, `CN500`.
- **sub_band** (*Optional*, default `0`): Sub-band for regions with more than 8 uplink channels, e.g. `2` for TTN US915. Leave at `0` for EU868.
- **join_eui** (**Required**): JoinEUI (AppEUI), 16 hex characters.
- **dev_eui** (**Required**): DevEUI, 16 hex characters.
- **app_key** (**Required**): AppKey, 32 hex characters.
- **nwk_key** (*Optional*): NwkKey, 32 hex characters. Only for LoRaWAN 1.1 networks — omit it for LoRaWAN 1.0.x (e.g. TTN), otherwise the join uses 1.1 key derivation and fails.
- **uplink_interval** (*Optional*, default `300`): Seconds between uplinks. The default matches what the EU868 1% duty cycle allows at the default `join_dr: 0` (SF12, ~2.8 s airtime per uplink — the radio cannot legally send more often than roughly every 280 s). At higher data rates you can lower this, but mind the TTN Fair Use Policy (30 s airtime per day).
- **join_dr** (*Optional*, default `0`): Data rate for the OTAA join and the initial session. `0` (EU868: SF12/125 kHz) maximizes link budget and range; higher values (EU868: up to `5` = SF7) are faster and use far less airtime. Keep the default on marginal links.
- **scan_guard** (*Optional*, default `50`): Milliseconds by which RX windows open early. Increase if downlinks or join-accepts are missed (RadioLib error -1116).
- **payload**: Defines what is transmitted, in this order:
  - **sensors** (*Optional*): List of numeric sensors.
    - **sensor** (**Required**): The sensor ID.
    - **multiplier** (*Optional*, default `1`): Value is sent as `value * multiplier + offset`, rounded to an integer.
    - **offset** (*Optional*, default `0`): See above.
    - **bytes** (*Optional*, default `2`): Encoded size in bytes, range 1 to 4.
  - **binary_sensors** (*Optional*): List of binary sensors, packed as bits.
    - **binary_sensor** (**Required**): The binary sensor ID.
  - **text_sensors** (*Optional*): List of text sensors, each sent with a length byte prefix.
    - **text_sensor** (**Required**): The text sensor ID.

## Payload format

The uplink payload is packed in this order:

1. **Sensors:** each value is sent as a big-endian signed integer of `bytes` length, computed as `value * multiplier + offset`
2. **Binary sensors:** packed as bits, 8 per byte (LSB first)
3. **Text sensors:** each prefixed with a length byte, followed by the raw characters (max. 255 bytes)

The total payload must fit within 51 bytes (LoRaWAN DR0 limit).

[decode.ttn](decode.ttn) contains an example uplink decoder for The Things Network matching this format.

## License

This project is licensed under the [MIT License](LICENSE).

It uses the following library:

- **RadioLib** — [LGPL-3.0](https://opensource.org/licenses/LGPL-3.0), see the [RadioLib GitHub repository](https://github.com/jgromes/RadioLib)
