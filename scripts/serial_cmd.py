#!/usr/bin/env python3
"""Send serial commands to the Sticky and capture the reply, stdlib-only.
Usage: python3 scripts/serial_cmd.py PORT TOTAL_SECONDS CMD1 [CMD2 ...]
Opens the port passively (no DTR/RTS toggling), waits 2.5 s for boot, sends
each command 1.2 s apart, keeps reading until TOTAL_SECONDS elapse.
"""
import os, select, sys, termios, time

port, total = sys.argv[1], float(sys.argv[2])
cmds = sys.argv[3:]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    st = termios.tcgetattr(fd)
    st[0] = st[1] = st[3] = 0
    st[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    st[4] = st[5] = termios.B115200
    st[6][termios.VMIN] = st[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, st)
    end = time.monotonic() + total
    next_cmd = time.monotonic() + 2.5   # let the board finish booting
    ci = 0
    buf = bytearray()
    while time.monotonic() < end:
        now = time.monotonic()
        if ci < len(cmds) and now >= next_cmd:
            os.write(fd, (cmds[ci] + "\n").encode())
            print(f">>> {cmds[ci]}", flush=True)
            ci += 1
            next_cmd = now + 1.2
        if select.select([fd], [], [], 0.2)[0]:
            chunk = os.read(fd, 65536)
            if not chunk:
                raise RuntimeError("serial device disconnected")
            buf.extend(chunk)
            sys.stdout.write(chunk.decode(errors="replace"))
            sys.stdout.flush()
finally:
    os.close(fd)
print(f"\n--- captured {len(buf)} bytes ---")
