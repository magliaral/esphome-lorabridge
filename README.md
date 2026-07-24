# esphome-lorabridge

ESPHome LoRaBridge is a custom component for transmitting sensor values over LoRaWAN (OTAA) using the [RadioLib](https://github.com/jgromes/RadioLib) library.

## Features

- LoRaWAN 1.0.4 OTAA join with DevNonce persistence in flash (joins survive reboots without enabling "resets join nonces" in TTN)
- Optional `nwk_key` for LoRaWAN 1.1 networks
- Shares ESPHome's existing SPI bus with other devices (e.g. a display) via a custom RadioLib HAL with proper bus arbitration
- Configurable payload built from ESPHome sensors, binary sensors, and text sensors
- Sensors without a valid state (NaN) are sent as a reserved "invalid" marker instead of a wrong number; out-of-range values are clamped instead of wrapping around
- GPS position can be transmitted like any other sensor pair (see below) for a live position on the Home Assistant map
- Optional **virtual gateway** mode: while WiFi/Ethernet is up, a copy of every uplink is additionally injected into TTN over UDP — in parallel to real gateways, deduplicated by the network server (see below)

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
  radio:
    region: EU868
    chip: SX1262
    nss_pin: 14
    rst_pin: 42
    irq_pin: 45
    busy_pin: 38
  network:
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

- **radio** (**Required**): Radio chip, wiring and regional settings.
  - **region** (**Required**): LoRaWAN region/band plan. One of `EU868`, `US915`, `AU915`, `AS923`, `AS923_2`, `AS923_3`, `AS923_4`, `IN865`, `KR920`, `CN500`.
  - **chip** (**Required**): Radio chip. One of `SX1261`, `SX1262`, `SX1268`, `SX1272`, `SX1276`, `SX1277`, `SX1278`, `SX1279`, `LR1110`, `LR1120`, `LR1121`.
  - **nss_pin**, **rst_pin**, **irq_pin** (**Required**): Radio GPIO numbers (the example matches the LILYGO T-Connect-Pro).
  - **busy_pin**, **gpio_pin** (*Optional*, default `-1`): Additional radio GPIOs; `-1` means not connected. SX126x/LR11x0 chips need **busy_pin**.
  - **sub_band** (*Optional*, default `0`): Sub-band for regions with more than 8 uplink channels, e.g. `2` for TTN US915. Leave at `0` for EU868.
  - **join_dr** (*Optional*, default `0`): Data rate for the OTAA join and the initial session. `0` (EU868: SF12/125 kHz) maximizes link budget and range; higher values (EU868: up to `5` = SF7) are faster and use far less airtime. Keep the default on marginal links.
  - **scan_guard** (*Optional*, default `50`): Milliseconds by which RX windows open early. Increase if downlinks or join-accepts are missed (RadioLib error -1116).
- **network** (**Required**): LoRaWAN credentials (from your network's device registration, e.g. the TTN console).
  - **join_eui** (**Required**): JoinEUI (AppEUI), 16 hex characters.
  - **dev_eui** (**Required**): DevEUI, 16 hex characters.
  - **app_key** (**Required**): AppKey, 32 hex characters.
  - **nwk_key** (*Optional*): NwkKey, 32 hex characters. Only for LoRaWAN 1.1 networks — omit it for LoRaWAN 1.0.x (e.g. TTN), otherwise the join uses 1.1 key derivation and fails.
- **uplink_interval** (*Optional*, default `300`): Seconds between uplinks. The default matches what the EU868 1% duty cycle allows at the default `join_dr: 0` (SF12, ~2.8 s airtime per uplink — the radio cannot legally send more often than roughly every 280 s). At higher data rates you can lower this, but mind the TTN Fair Use Policy (30 s airtime per day).
- **payload** (*Optional*): Defines what is transmitted, in this order:
  - **sensors** (*Optional*): List of numeric sensors.
    - **sensor** (**Required**): The sensor ID.
    - **multiplier** (*Optional*, default `1`): Value is sent as `value * multiplier + offset`, rounded to an integer.
    - **offset** (*Optional*, default `0`): See above.
    - **bytes** (*Optional*, default `2`): Encoded size in bytes, range 1 to 4.
  - **binary_sensors** (*Optional*): List of binary sensors, packed as bits.
    - **binary_sensor** (**Required**): The binary sensor ID.
  - **text_sensors** (*Optional*): List of text sensors, each sent with a length byte prefix.
    - **text_sensor** (**Required**): The text sensor ID.

## Virtual gateway (TTN over IP)

Optionally, the bridge can act as its own **virtual gateway** running in parallel to real gateways: the radio always transmits over RF, and while the node has network connectivity (WiFi or Ethernet), a copy of every uplink is additionally injected into TTN via the Semtech UDP packet-forwarder protocol (`eu1.cloud.thethings.network:1700` by default). The network server deduplicates the frame exactly as it does when multiple real gateways report the same uplink — one device, one frame, several gateways in the metadata. When the network drops, nothing changes on the air: uplinks keep going out over RF, no rejoin, monotonic frame counters. The OTAA join always happens over RF and is never copied.

```yaml
wifi:            # or ethernet: — required for the virtual gateway
  # ...

lorabridge:
  # ... radio / network / payload as above ...
  virtual_gateway:
    enabled: true
    server: eu1.cloud.thethings.network   # optional, default shown
    port: 1700                            # optional, default shown
    keepalive_interval: 10s               # optional, default shown

# Optional diagnostics (only valid with virtual_gateway enabled):
binary_sensor:
  - platform: lorabridge
    name: "TTN Gateway Connected"     # PULL_ACK seen recently
sensor:
  - platform: lorabridge
    name: "Uplinks Forwarded"         # PUSH_ACK-confirmed uplink copies
```

- **enabled** (*Optional*, default `true`): Enables the feature. When the `virtual_gateway:` block is absent or `enabled: false`, none of the virtual-gateway code is compiled into the binary.
- **server**/**port** (*Optional*): The network server's Semtech-UDP endpoint. Use the cluster your application lives on (e.g. `eu1`/`nam1`/`au1.cloud.thethings.network`).
- **keepalive_interval** (*Optional*, default `10s`): PULL_DATA keepalive rate while the network is up; keeps the NAT/CGNAT mapping open.

**TTN setup (required):** TTN ignores traffic from unknown gateways. Register a new gateway in the TTN console **on the same cluster as `server`** with the EUI printed in the boot log (`Virtual gateway EUI: …`, derived from the ESP's MAC with `FFFE` padding), frequency plan EU868. Do not enable "Require authenticated connection" — the Semtech UDP protocol is unauthenticated by design.

How it works / notes:

- RadioLib remains the only LoRaWAN stack: session, frame counters, MIC and MAC state are untouched — the finished PHYPayload is copied at the radio boundary **after** the RF transmission completes, so the UDP copy arrives within the server's deduplication window together with the reports of real gateways.
- The `rxpk` metadata is deliberately poor and jittered per uplink (RSSI −121…−117 dBm, SNR −16.0…−12.5 dB, correlated): ADR never sees a "good" link, and downlink routing prefers real gateways over the virtual one.
- Each copy is sent as one PUSH_DATA datagram; if no PUSH_ACK arrives within 500 ms it is retransmitted exactly once (same token), then dropped (unconfirmed uplinks — the next one comes anyway).
- The "Uplinks Forwarded" sensor counts copies **confirmed by the server via PUSH_ACK** — it says nothing about whether the network server used the frame or dropped it as a duplicate.
- Downlinks are not emulated: PULL_RESP messages are logged, acknowledged with TX_ACK and discarded. Downlinks reach the device natively over RF, since the RX windows always run on the real radio. Use unconfirmed uplinks only.
- Recommended: pin RadioLib to an exact version (`jgromes/RadioLib@7.1.2`) — the TX-tee wrapper is verified against that release's internals.

Independent behavior notes (also active without the virtual gateway): the component disables ADR explicitly (RadioLib's default is on), and the first uplink is sent ~2 s after a successful join instead of waiting one full `uplink_interval`; the regular interval counts from that first uplink.

## Payload format

The uplink payload is packed in this order:

1. **Sensors:** each value is sent as a big-endian signed integer of `bytes` length, computed as `round(value * multiplier + offset)`
2. **Binary sensors:** packed as bits, 8 per byte (LSB first)
3. **Text sensors:** each prefixed with a length byte, followed by the raw characters (max. 255 bytes)

The total payload must fit within 51 bytes (LoRaWAN DR0 limit) — mind this when adding fields; oversized uplinks are skipped with an error log.

**Invalid values:** a sensor without a valid state (NaN, e.g. its data source is offline) is encoded as the reserved bit pattern `0x80 00…00` (the most negative value of the field width). The decoder omits such fields from the decoded payload, so downstream consumers (e.g. Home Assistant) keep the last known state instead of receiving a wrong number. Consequently the usable range of an n-byte field is `[-(2^(8n-1))+1, 2^(8n-1)-1]`; values outside are clamped. Binary sensors (single bits) and text sensors cannot signal "invalid".

**GPS:** transmit latitude/longitude as two regular 4-byte sensors with `multiplier: 10000000` (≈1 cm resolution), e.g. with ESPHome's `gps:` component:

```yaml
gps:
  latitude:
    id: gps_latitude
  longitude:
    id: gps_longitude

lorabridge:
  # ...
  payload:
    sensors:
      # ... other sensors ...
      - sensor: gps_latitude
        multiplier: 10000000
        bytes: 4
      - sensor: gps_longitude
        multiplier: 10000000
        bytes: 4
```

Before the first GPS fix the values are NaN and therefore sent as "invalid" (see above); an exact 0/0 position is dropped by the decoder as well.

## TTN payload decoder

[decoder/decode.js](decoder/decode.js) contains the uplink decoder for The Things Network (console → *Payload formatters* → *Uplink* → *Custom Javascript formatter*). It auto-detects known payload layouts, omits invalid-marker fields, and annotates fields with Home Assistant metadata (`_sensor_attr`) consumed by a matching `thethingsnetwork` custom integration. When the payload composition changes, add a new layout entry to `LAYOUTS` (longest first) and redeploy the formatter in the TTN console.

## License

This project is licensed under the [MIT License](LICENSE).

It uses the following library:

- **RadioLib** — [LGPL-3.0](https://opensource.org/licenses/LGPL-3.0), see the [RadioLib GitHub repository](https://github.com/jgromes/RadioLib)
