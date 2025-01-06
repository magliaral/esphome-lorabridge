# esphome-lorabridge

ESPHome LoRaBridge is a custom component for transmitting sensor values over LoRaWAN using the RadioLib library.

## Usage with ESPHome

To use this component in ESPHome, add the following configuration to your `esphome.yaml`:

```yaml
esphome:
  name: "esphome-lorabridge"
  libraries:
    - SPI
  platformio_options:
    lib_ldf_mode: chain+
    lib_deps:
      - jgromes/RadioLib@^7.1.1
      - https://github.com/radiolib-org/RadioBoards.git

external_components:
  - source:
      type: git
      url: https://github.com/magliaral/esphome-lorabridge.git
      ref: 0.1.0

lorabridge:
  region: EU868
  sub_band: 0                                 # Is optional. Default set by 0. For US915, change this to 2, otherwise leave on 0
  join_eui: ----------------
  dev_eui: ----------------
  app_key: --------------------------------
  uplink_interval: 60                         # Is optional. Default set 60 seconds
  payload:
    sensors:
      - sensor: id sensor1
        multiplier: 1                         # Is optional. Default set by 1
        offset: 0                             # Is optional. Default set by 0
        bytes: 2                              # Is optional. Default set set by 2 (range 1 to 4)
      - sensor: id sensor2
    binary_sensors:
      - binary_sensor: id binary_sensor1
      - binary_sensor: id binary_sensor2
    text_sensors:
      - text_sensor: id text_sensor1
      - text_sensor: id text_sensor2
```

## License
This project is licensed under the [MIT License](LICENSE).
This project uses the following libraries:
1. **RadioLib**
   - License: [LGPL-3.0](https://opensource.org/licenses/LGPL-3.0)  
   - For more details, see the [RadioLib GitHub repository](https://github.com/jgromes/RadioLib).

2. **RadioBoards**  
   - License: [MIT](https://opensource.org/licenses/MIT)  
   - For more information, see the [RadioBoards GitHub repository](https://github.com/radiolib-org/RadioBoards).
