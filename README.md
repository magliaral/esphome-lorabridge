# esphome-lorabridge

ESPHome LoRaBridge is a custom component for transmitting sensor values over LoRaWAN using the RadioLib library.

## Usage with ESPHome

To use this component in ESPHome, add the following configuration to your `esphome.yaml`:

```yaml
external_components:
  # LoRaBridge Component
  - source:
      type: git
      url: https://github.com/magliaral/esphome-lorabridge
    requirements:
      platformio_options:
        lib_deps: 
          - jgromes/RadioLib@^7.1.1

  # Example sensor configuration
sensor:
  - platform: lorabridge
    name: "LoRa Sensor"

## License
This project is licensed under the MIT License.
For more details, see the [LICENSE](LICENSE) file.

This project uses the RadioLib library, which is licensed under the LGPL-3.0.
For more details, see the [RadioLib GitHub repository](https://github.com/jgromes/RadioLib).
