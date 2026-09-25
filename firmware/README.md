# Firmware

Two PlatformIO projects. Open either folder in VS Code with the PlatformIO
extension, or build from the command line:

```sh
cd firmware/esp32c3-oled      # or firmware/esp32-blemidi
pio run -t upload
pio device monitor            # 115200 baud
```

| Folder | Board | Output | Libraries |
|--------|-------|--------|-----------|
| [`esp32c3-oled/`](esp32c3-oled/) | ESP32-C3 with onboard 0.42" 72×40 SSD1306 | USB serial, see [`../docs/SERIAL.md`](../docs/SERIAL.md) | FastLED, U8g2 |
| [`esp32-blemidi/`](esp32-blemidi/) | Classic ESP32 | BLE MIDI, see [`../docs/MIDI.md`](../docs/MIDI.md) | FastLED, NimBLE-Arduino 1.x |

The `platformio.ini` files are reference configs. The C3 build enables USB CDC
on boot so `Serial` appears on the native USB port; if your C3 board has a
separate USB-UART chip, remove those two flags.

## Settings worth knowing

All at the top of each `main.cpp`:

| Constant | Value | Effect |
|----------|-------|--------|
| `DEADZONE` | 250 | ADC counts around the joystick centre that read as centre |
| `AXIS_HALF_RANGE` | 1500 | Joystick travel mapped to the full grid, kept under the ADC limit so full deflection reaches the edge |
| `STROBE_HZ_MIN` / `MAX` | 1 / 20 | Strobe range on slider 2 |
| `FRAME_INTERVAL_MS` | 20 | 50 fps |
| `DEBOUNCE_MS` | 50 | Joystick push debounce |

## Roadmap

- Bring BLE MIDI into the ESP32-C3 firmware, so the pictured unit sends MIDI
  and keeps its screen.
- Send CCs in mode 7 (off) so channel 8 is usable.
