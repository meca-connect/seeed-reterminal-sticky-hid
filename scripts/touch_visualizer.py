#!/usr/bin/env python3
"""Terminal-only raw touch visualization. No HID, reset or firmware write."""
import re

FRAME = re.compile(r'TOUCH (DOWN|MOVE|UP) t=(\d+) n=(\d+) status=0x([0-9A-Fa-f]+) count=(\d+)(.*)')
POINT = re.compile(r'\[id=(\d+) x=(\d+) y=(\d+) size=(\d+)\]')

def parse_frame(line):
    match = FRAME.fullmatch(line.strip())
    if not match:
        return None
    event, timestamp, sequence, status, count, tail = match.groups()
    points = [dict(zip(('id', 'x', 'y', 'size'), map(int, p))) for p in POINT.findall(tail)]
    count, status = int(count), int(status, 16)
    if (count > 5 or count != len(points) or (status & 15) != count
            or not status & 128 or len({p['id'] for p in points}) != count
            or POINT.sub('', tail).strip()):
        return None
    return {'event': event, 't': int(timestamp), 'n': int(sequence),
            'status': status, 'count': count, 'points': points}


def render(frame, stats, age, width=480, height=800, columns=48, rows=18):
    """Raw sensor axes only; width/height are explicit provisional bounds."""
    grid = [['.'] * columns for _ in range(rows)]
    stale = age > 0.6
    lines = ['Sticky RAW MULTITOUCH | serial only - NOT HID / NOT PTP validation',
             f'Raw axes: X right / Y down; view {width}x{height} (unverified bounds/orientation)']
    if frame is None:
        lines.append('WAITING for a complete touch frame; tap/hold 1, 2, then 3 fingers')
    else:
        lines.append(f"CONTACTS {frame['count']} | {frame['event']} | device t={frame['t']}ms"
                     f" | age={age:.2f}s" + (' | STALE: last known state, not live' if stale else ''))
        if not stale:
            for p in frame['points']:
                x = min(columns-1, max(0, round(p['x']*(columns-1)/max(1,width-1))))
                y = min(rows-1, max(0, round(p['y']*(rows-1)/max(1,height-1))))
                grid[y][x] = str(p['id']) if p['id'] < 10 else '*'
    lines += ['+' + '-'*columns + '+'] + ['|' + ''.join(row) + '|' for row in grid]
    lines.append('+' + '-'*columns + '+')
    for p in (frame['points'] if frame else []):
        lines.append(f"id={p['id']}  x={p['x']:4}  y={p['y']:4}  size={p['size']:3} (not pressure)")
    lines.append(f"Valid frames={stats['frames']}  rejected={stats['rejected']}  peak contacts={stats['max']}")
    lines.append('Ctrl+C to stop. No pointer movement, keystrokes, or firmware changes.')
    return '\n'.join(lines)


def main():
    import argparse
    import collections
    import fcntl
    import os
    from pathlib import Path
    import select
    import sys
    import termios
    import time

    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--port', help='live serial port, 115200 baud')
    source.add_argument('--replay', type=Path, help='recorded raw serial log; no device access')
    parser.add_argument('--snapshot-contacts', type=int, choices=range(6), help='first replay frame with this contact count')
    parser.add_argument('--seconds', type=float, default=0, help='live duration; 0 means until Ctrl+C')
    parser.add_argument('--width', type=int, default=480, help='provisional raw X extent')
    parser.add_argument('--height', type=int, default=800, help='provisional raw Y extent')
    parser.add_argument('--log', type=Path, help='create a new raw live-capture log (never overwrite)')
    args = parser.parse_args()
    if args.width < 2 or args.height < 2 or args.seconds < 0:
        parser.error('invalid bounds/duration')
    stats = {'frames': 0, 'rejected': 0, 'max': 0}
    frame = None
    heartbeat = 'Waiting for ALIVE heartbeat'
    last_frame_time = 0.0
    last_draw = 0.0
    buffer = ''
    ansi = sys.stdout.isatty()
    log = None
    fd = None
    old = None
    exclusive = False

    def consume(line):
        nonlocal frame, last_frame_time, heartbeat
        if 'ALIVE ' in line:
            heartbeat = line[line.index('ALIVE '):][:160]
        candidate = parse_frame(line)
        if candidate is not None:
            frame = candidate
            last_frame_time = time.monotonic()
            stats['frames'] += 1
            stats['max'] = max(stats['max'], frame['count'])
            return True
        if 'TOUCH ' in line:
            stats['rejected'] += 1
        return False

    def draw(label):
        age = time.monotonic() - last_frame_time if frame else 0
        text = label + '\n' + render(frame, stats, age, args.width, args.height) + '\n' + heartbeat
        if ansi:
            sys.stdout.write('\033[H\033[J' + text + '\n')
        else:
            print(text)
        sys.stdout.flush()

    try:
        if args.replay:
            found = False
            # Old captures sometimes contain escaped CR/LF from damaged lines.
            # Split these for parsing, but require a full valid frame afterward.
            text = args.replay.read_text(errors='replace').replace('\\r\\n', '\n')
            counts = collections.Counter()
            for line in text.splitlines():
                if consume(line):
                    assert frame is not None
                    counts[frame['count']] += 1
                    if args.snapshot_contacts is not None and frame['count'] == args.snapshot_contacts:
                        found = True
                        draw('REPLAY - recorded hardware data, not live')
                        break
            if args.snapshot_contacts is not None and not found:
                raise RuntimeError('no complete frame with requested contact count')
            if args.snapshot_contacts is None:
                draw('REPLAY - recorded hardware data, not live')
                print('Parsed contact-count histogram:', dict(sorted(counts.items())))
            return
        if args.log:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            log = args.log.open('xb')
        fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        fcntl.ioctl(fd, termios.TIOCEXCL)
        exclusive = True
        old = termios.tcgetattr(fd)
        settings = termios.tcgetattr(fd)
        settings[0] = settings[1] = settings[3] = 0
        settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        settings[4] = settings[5] = termios.B115200
        settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, settings)
        # Discard stale kernel-buffered output without toggling DTR/RTS/reset.
        termios.tcflush(fd, termios.TCIFLUSH)
        start = time.monotonic()
        if ansi:
            sys.stdout.write('\033[?1049h\033[?25l')
        while args.seconds == 0 or time.monotonic()-start < args.seconds:
            if select.select([fd], [], [], 0.03)[0]:
                data = os.read(fd, 65536)
                if not data:
                    raise RuntimeError('serial disconnected')
                if log:
                    log.write(data)
                    log.flush()
                buffer += data.decode('ascii', errors='replace').replace('\\r\\n', '\n')
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    consume(line.strip('\r'))
                if len(buffer) > 8192:
                    buffer = ''
                    stats['rejected'] += 1
            now = time.monotonic()
            if now-last_draw >= (0.1 if ansi else 1.0):
                draw('LIVE - ' + args.port)
                last_draw = now
    except KeyboardInterrupt:
        pass
    except (OSError, RuntimeError) as error:
        print(f'Visualizer stopped: {error}', file=sys.stderr)
        return 1
    finally:
        if fd is not None:
            if old is not None:
                termios.tcsetattr(fd, termios.TCSANOW, old)
            if exclusive:
                fcntl.ioctl(fd, termios.TIOCNXCL)
            os.close(fd)
        if log:
            log.close()
        if ansi and args.port:
            sys.stdout.write('\033[?25h\033[?1049l')
            sys.stdout.flush()
    print(f"Stopped: valid frames={stats['frames']} rejected={stats['rejected']} peak contacts={stats['max']}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
