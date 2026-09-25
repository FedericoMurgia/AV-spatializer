# Serial stream (ESP32-C3 firmware)

USB serial, **115200 baud**. After boot the board prints `ready`, then one line
per frame (50 Hz):

```
sx-2051-sy-2040-x-4-y-4-p-3120-s1-812-s2-3990-m-0
```

| Tag | Value | Range |
|-----|-------|-------|
| `sx` | Joystick X, smoothed raw ADC | 0–4095 |
| `sy` | Joystick Y, smoothed raw ADC | 0–4095 |
| `x` | Joystick X as grid position | 0–8, 4 = centre |
| `y` | Joystick Y as grid position | 0–8, 4 = centre |
| `p` | Potentiometer, smoothed raw ADC | 0–4095 |
| `s1` | Slider 1, smoothed raw ADC | 0–4095 |
| `s2` | Slider 2, smoothed raw ADC | 0–4095 |
| `m` | Colour mode | 0–7 |

Notes read from the firmware:

- `sx`/`sy` are exponentially smoothed (α 0.4); `p`, `s1`, `s2` use α 0.8.
- `x`/`y` include a dead zone of ±250 ADC counts around the centre calibrated
  at boot, so small movements report 4.
- `s1` and `s2` are raw. The firmware inverts them internally: block size is
  `map(s1, 0, 4095, 8, 1)` and strobe is `1 + (4095 - s2) / 4095 × 19` Hz.
- In mode 7 (off) the matrix is blanked and **no lines are sent** until the
  mode changes.

A minimal reader is in [`../examples/serial_reader.py`](../examples/serial_reader.py).

## OLED

The onboard 72×40 screen shows whichever control moved last:

| Moved | Screen |
|-------|--------|
| Potentiometer | `Bright` and 0–100 % |
| Slider 1 | `Size` and 1–8 |
| Slider 2 | `Strobe` and Hz |
| Joystick | `X  Y` and offset from centre, −4 to +4 |

In the two strobe modes it shows `STROBE` once and then stops redrawing. After
two seconds without activity it alternates between two idle pages.
