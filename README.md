# air-monitor-ota

STM32G474 air monitor with ESP32-AT WiFi/MQTT and AWS IoT MQTT Stream OTA (MCUboot overwrite-only).
Side note: the ESP32 is flashed with the oficial ESP AT firmware and I only included the root & device certificate, and the private key from AWS.

## Install

```bash
git clone --recursive https://github.com/CarlosT25-png/air-monitor-ota.git
cd air-monitor-ota

cp Components/esp32-at/Inc/configuration.h.example Components/esp32-at/Inc/configuration.h
# Here you can edit the configuration of this application, feel free to adjust it

cmake --preset Release
cmake --build build/Release
```

Flash (requires [stlink](https://github.com/stlink-org/stlink)):

```bash
st-flash write build/Release/bootloader/mcuboot.bin 0x08000000
st-flash write build/Release/air-monitor-ota.signed.bin 0x0800C000
st-flash reset
```

Debug console: USART3 @ 115200.

Build outputs:

- `air-monitor-ota.signed.bin` — slot 0 (USB flash)
- `air-monitor-ota-ota.signed.bin` — slot 1 (AWS OTA)

## AWS OTA

Copy [`aws.md.example`](aws.md.example) to `aws.md`  and fill in your account details.

GitHub Actions:

- CI: builds firmware on every push/PR
- OTA Release: on `v*.*.*` tags or manual dispatch; deploys via OIDC to S3 + IoT OTA

I included a `aws.md.example` for GitHub Variables, OIDC IAM setup and manual CLI commands.

## Hardware

- Board: NUCLEO-G474RE
- ESP32: UART4 (AT firmware with MQTT/AWS)
- DHT11 sensor
- MCUboot @ `0x08000000`, app slot 0 @ `0x0800C000`, OTA staging slot 1 @ `0x08040000`
