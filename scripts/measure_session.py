#!/usr/bin/env python3
"""Self-contained rate/latency session for the Sticky s10 probe.
Opens serial (board resets), waits for BLE reconnect (auto or `bluetoothctl
connect`), captures STATS, runs a WATCH window while the user gestures,
captures final STATS. Everything host-timestamped to a log file.
Usage: python3 scripts/measure_session.py PORT MAC WATCH_SECONDS LOGFILE
"""
import os, re, select, subprocess, sys, termios, time

PORT, MAC, WATCH, LOG = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
log = open(LOG, "w")

def stamp():
    return time.strftime("%H:%M:%S") + f".{int(time.time()*1000)%1000:03d}"

def logline(s):
    print(s, flush=True)
    log.write(s + "\n"); log.flush()

def drain(seconds, note=""):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if select.select([fd], [], [], 0.2)[0]:
            chunk = os.read(fd, 65536)
            if not chunk:
                raise RuntimeError("serial disconnected")
            text = chunk.decode(errors="replace")
            log.write(f"[{stamp()} RX] {text}\n"); log.flush()
            sys.stdout.write(text); sys.stdout.flush()
        else:
            continue
    if note: logline(f"--- {note} ---")

def send(cmd):
    os.write(fd, (cmd + "\n").encode())
    logline(f"[{stamp()} TX] {cmd}")

try:
    st = termios.tcgetattr(fd)
    st[0] = st[1] = st[3] = 0
    st[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    st[4] = st[5] = termios.B115200
    st[6][termios.VMIN] = st[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, st)

    logline(f"=== session start {stamp()} — board resets on port open ===")
    time.sleep(4.0)                      # boot: 800ms + touch init + BLE init
    drain(2.0, "boot captured")

    # ensure BLE reconnection
    connected = False
    subprocess.run(["bluetoothctl", "connect", MAC], capture_output=True, timeout=20)
    for _ in range(12):
        info = subprocess.run(["bluetoothctl", "info", MAC], capture_output=True, text=True, timeout=10).stdout
        if re.search(r"Connected:\s*yes", info):
            connected = True
            break
        time.sleep(1.5)
    logline(f"BLE connected={connected}")
    if not connected:
        raise SystemExit("device did not reconnect — aborting")
    time.sleep(1.0)

    send("STATS"); drain(2.0, "pre-motion STATS")
    send(f"WATCH {WATCH}")
    logline(f"=== GESTURE WINDOW {WATCH}s — move one finger in circles NOW ===")
    drain(WATCH + 2.5, "watch window done")
    send("STATS"); drain(3.0, "post-motion STATS")
    logline(f"=== session end {stamp()} ===")
finally:
    os.close(fd)
    log.close()
    print(f"log saved: {LOG}")
