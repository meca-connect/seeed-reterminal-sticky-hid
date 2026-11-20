#!/usr/bin/env python3
"""Bounded passive UART capture; does not toggle DTR/RTS or reset the board.
Usage: python3 scripts/capture_serial.py PORT SECONDS OUTPUT_LOG
Keep the port closed in other monitors during capture.
"""
import os
from pathlib import Path
import select
import sys
import termios
import time

port, duration, output = sys.argv[1], float(sys.argv[2]), Path(sys.argv[3])
output.parent.mkdir(parents=True, exist_ok=True)
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    settings = termios.tcgetattr(fd)
    settings[0] = settings[1] = settings[3] = 0
    settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    settings[4] = settings[5] = termios.B115200
    settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, settings)
    end = time.monotonic() + duration
    data = bytearray()
    with output.open('xb') as log:
        while time.monotonic() < end:
            if select.select([fd], [], [], min(0.25, max(0, end-time.monotonic())))[0]:
                chunk = os.read(fd, 65536)
                if not chunk:
                    raise RuntimeError('serial device disconnected')
                data.extend(chunk)
                log.write(chunk)
                log.flush()
    print(data.decode(errors='replace'))
    print(f'Captured {len(data)} bytes to {output}')
finally:
    os.close(fd)
