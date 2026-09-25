"""Minimal reader for the AV Spatializer ESP32-C3 serial stream.

    pip install pyserial
    python serial_reader.py /dev/tty.usbmodemXXXX

Prints one dict per frame, e.g.
{'sx': 2051, 'sy': 2040, 'x': 4, 'y': 4, 'p': 3120, 's1': 812, 's2': 3990, 'm': 0}
"""
import sys
import serial


def parse(line: str):
    parts = line.strip().split("-")
    if len(parts) != 16:
        return None  # "ready" or a partial line
    try:
        return {parts[i]: int(parts[i + 1]) for i in range(0, 16, 2)}
    except ValueError:
        return None


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    with serial.Serial(port, 115200, timeout=1) as ser:
        while True:
            frame = parse(ser.readline().decode(errors="ignore"))
            if frame:
                print(frame)


if __name__ == "__main__":
    main()
