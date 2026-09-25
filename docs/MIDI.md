# BLE MIDI (ESP32 firmware)

The `esp32-blemidi` firmware advertises as **"AV Controller"** using the
standard BLE MIDI service (`03B80E5A-EDE8-4B33-A751-6CE34EC4C700`). Pair it as a
Bluetooth MIDI device on the host; on macOS through Audio MIDI Setup → MIDI
Studio → Bluetooth.

## Map

| Control | CC | Value |
|---------|:--:|-------|
| Joystick X | 1 | Grid position 0–8 scaled to 0–127 |
| Joystick Y | 2 | Grid position 0–8 scaled to 0–127 |
| Potentiometer | 3 | 0–127 |
| Slider 1 | 4 | 0–127 (inverted in firmware to match block size) |
| Slider 2 | 5 | 0–127 (inverted in firmware to match strobe rate) |

**Channel = colour mode + 1.** A joystick press moves to the next mode, and all
five CCs move to the next channel with it. On a channel change all five CCs are
sent at once, so the host receives the current state on the new channel.

## Timing

- CCs are checked every **100 ms** and only sent when they change: X and Y on
  any change, the potentiometer and sliders when they move by more than 1.
- Each CC is sent as its own BLE notification, the most widely compatible form.
- Joystick X/Y are quantised to the 9 grid positions, so they step
  (0, 15, 31, 47 … 127) rather than sweep.

## Known behaviour

- **Mode 7 is off:** the matrix goes dark and no MIDI is sent. Channels 1–7
  carry the CCs.
- Joystick centre is calibrated at power-up. Do not touch the joystick while
  the board boots.

## Library

Written against **NimBLE-Arduino 1.x**. Version 2.x changed the server callback
signatures (`onConnect`, `onDisconnect`), so the callbacks here will not compile
against 2.x without updating them. `platformio.ini` pins 1.x.
