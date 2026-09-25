# Wiring

Pins are set as constants at the top of each `main.cpp`. Match the wiring to
them, or change the constants and rebuild.

## ESP32-C3 beta (`firmware/esp32c3-oled`)

| Function | GPIO | Notes |
|----------|:----:|-------|
| WS2812B data | 7 | |
| Joystick X | 0 | ADC1 |
| Joystick Y | 1 | ADC1 |
| Joystick push | 8 | `INPUT_PULLUP`, switch to GND |
| Potentiometer | 2 | ADC1 |
| Slider 1 | 3 | ADC1 |
| Slider 2 | 4 | ADC1 |
| OLED SDA | 5 | Onboard 0.42" 72×40 SSD1306 |
| OLED SCL | 6 | Onboard |

All five analog inputs are on ADC1, read at 12 bit with 11 dB attenuation
(0–4095 over roughly the full 3.3 V swing).

## ESP32 BLE MIDI variant (`firmware/esp32-blemidi`)

| Function | GPIO | Notes |
|----------|:----:|-------|
| WS2812B data | 0 | |
| Joystick X | 34 | ADC1, input-only pin |
| Joystick Y | 35 | ADC1, input-only pin |
| Joystick push | 25 | `INPUT_PULLUP`, switch to GND |
| Potentiometer | 27 | ADC2 |
| Slider 1 | 26 | ADC2 |
| Slider 2 | 33 | ADC1 |

The firmware does a discarded read and a 100 µs settle after every ADC channel
switch before averaging four samples.

ADC2 (GPIO 25–27) cannot be read while Wi-Fi is active on the classic ESP32.
This firmware uses Bluetooth only, so it is fine as is; keep it in mind before
adding Wi-Fi.

## Potentiometers and joystick

Wire each potentiometer and each joystick axis as a voltage divider between
3V3 and GND, wiper to the GPIO. Slider direction is handled in firmware: at the
low-ADC end slider 1 gives the largest block and slider 2 the highest strobe
rate. If yours runs the other way, swap the outer legs.

## Matrix layout

`xyToIndex()` assumes **non-serpentine** wiring: every row runs right to left,
row 0 first. A serpentine matrix will show every other row mirrored; change
`xyToIndex()` rather than rewiring.

## Power

The unit runs from USB. Current at the matrix is the thing to watch:

- **ESP32-C3 firmware:** the potentiometer maps straight to FastLED brightness
  0–255 and there is no current cap. A full 8×8 white block at maximum
  brightness can ask for far more than a USB port or the board's regulator
  supplies. Keep brightness moderate, or add
  `FastLED.setMaxPowerInVoltsAndMilliamps()` as in the BLE variant.
- **BLE MIDI firmware:** brightness is scaled down per colour mode and block
  size, and FastLED is capped with `setMaxPowerInVoltsAndMilliamps(5, 50)`,
  which leaves headroom for BLE radio bursts.

Tie all grounds together.
