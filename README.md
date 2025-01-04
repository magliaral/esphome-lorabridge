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
  # LoRaBridge Component
  - source:
      type: git
      url: https://github.com/magliaral/esphome-lorabridge@^1.0.0

lorabridge:
  region: EU868
  join_eui: ----------------
  dev_eui: ----------------
  app_key: --------------------------------
```

## License
This project is licensed under the MIT License.<br>
For more details, see the [LICENSE](LICENSE) file.

This project uses the RadioLib library, which is licensed under the LGPL-3.0.<br>
For more details, see the [RadioLib GitHub repository](https://github.com/jgromes/RadioLib).
