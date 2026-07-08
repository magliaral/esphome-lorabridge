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
  region: EU868                # EU868, US915, AU915, AS923, AS923_2, AS923_3, AS923_4, IN865, KR920, CN500
  sub_band: 0                  # Optional, default 0. For US915/AU915 set the sub-band used by your network (e.g. 2 for TTN US915)
  join_eui: ----------------   # 16 hex characters
  dev_eui: ----------------    # 16 hex characters
  app_key: --------------------------------  # 32 hex characters
  # nwk_key: ------------------------------  # Optional, only for LoRaWAN 1.1 networks. Omit for LoRaWAN 1.0.x (TTN)
  uplink_interval: 60          # Optional, default 60 seconds
  join_dr: 0                   # Optional, default 0. Data rate for the OTAA join (EU868: 0 = SF12/max range ... 5 = SF7)
  scan_guard: 50               # Optional, default 50 ms. How much earlier RX windows open; increase if downlinks are missed
  payload:
    sensors:
      - sensor: sensor1_id
        multiplier: 1          # Optional, default 1
        offset: 0              # Optional, default 0
        bytes: 2               # Optional, default 2 (range 1 to 4)
      - sensor: sensor2_id
    binary_sensors:
      - binary_sensor: binary_sensor1_id
      - binary_sensor: binary_sensor2_id
    text_sensors:
      - text_sensor: text_sensor1_id
      - text_sensor: text_sensor2_id
```

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
