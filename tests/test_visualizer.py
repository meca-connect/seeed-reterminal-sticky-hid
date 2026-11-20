"""Synthetic parser fixtures; not measured hardware output."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import touch_visualizer as tv

class VisualizerTests(unittest.TestCase):
    def test_parse_three_contacts_and_release(self):
        frame = tv.parse_frame('TOUCH MOVE t=294914 n=14717 status=0x83 count=3 [id=0 x=117 y=293 size=13] [id=1 x=266 y=269 size=8] [id=2 x=400 y=215 size=9]')
        self.assertEqual(frame['t'], 294914)
        self.assertEqual(frame['count'], 3)
        self.assertEqual([p['id'] for p in frame['points']], [0, 1, 2])
        self.assertEqual(frame['points'][2]['x'], 400)
        self.assertEqual(tv.parse_frame('TOUCH UP t=100 n=5 status=0x80 count=0')['points'], [])

    def test_reject_incomplete_duplicate_and_over_capacity_frames(self):
        for line in (
            'TOUCH MOVE t=1 n=1 status=0x82 count=2 [id=0 x=1 y=1 size=1]',
            'TOUCH MOVE t=1 n=1 status=0x82 count=2 [id=0 x=1 y=1 size=1] [id=0 x=2 y=2 size=2]',
            'TOUCH MOVE t=1 n=1 status=0x86 count=6',
            'TOUCH MOVE t=1 n=1 status=0x81 count=0',
            'TOUCH UP t=1 n=1 status=0x00 count=0',
            'junk TOUCH MOVE t=1 n=1 status=0x80 count=0',
        ):
            self.assertIsNone(tv.parse_frame(line), line)

    def test_grid_and_stale_state_do_not_invent_contact_release(self):
        frame = tv.parse_frame('TOUCH MOVE t=20 n=1 status=0x83 count=3 [id=0 x=0 y=0 size=4] [id=1 x=479 y=799 size=5] [id=2 x=240 y=400 size=6]')
        view = tv.render(frame, stats={'frames': 1, 'rejected': 0, 'max': 3}, age=0.01,
                         width=480, height=800, columns=20, rows=8)
        self.assertIn('CONTACTS 3', view)
        self.assertIn('id=2', view)
        self.assertIn('0...................', view)
        self.assertIn('...................1', view)
        stale = tv.render(frame, stats={'frames': 1, 'rejected': 0, 'max': 3}, age=2,
                          width=480, height=800, columns=20, rows=8)
        self.assertIn('STALE', stale)
        self.assertNotIn('CONTACTS 0', stale)

    def test_command_line_replays_a_recording(self):
        import subprocess
        import tempfile
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / 'build') as directory:
            log = Path(directory) / 'synthetic-fixture.log'
            log.write_text('TOUCH MOVE t=20 n=1 status=0x83 count=3 [id=0 x=0 y=0 size=4] [id=1 x=479 y=799 size=5] [id=2 x=240 y=400 size=6]' + chr(10))
            result = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / 'scripts' / 'touch_visualizer.py'), '--replay', str(log), '--snapshot-contacts', '3'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('REPLAY', result.stdout)
            self.assertIn('CONTACTS 3', result.stdout)
            self.assertIn('id=2', result.stdout)

    def test_live_command_reads_a_serial_stream(self):
        import os
        import pty
        import subprocess
        import threading
        import time
        master, slave = pty.openpty()
        name = os.ttyname(slave)
        line = b'TOUCH MOVE t=20 n=1 status=0x83 count=3 [id=0 x=0 y=0 size=4] [id=1 x=479 y=799 size=5] [id=2 x=240 y=400 size=6]\r\n'
        def writer():
            for _ in range(12):
                time.sleep(0.1)
                os.write(master, line)
        worker = threading.Thread(target=writer)
        worker.start()
        try:
            result = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / 'scripts' / 'touch_visualizer.py'), '--port', name, '--seconds', '1.5'], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('CONTACTS 3', result.stdout)
            self.assertIn('id=2', result.stdout)
        finally:
            worker.join()
            os.close(master)
            os.close(slave)

if __name__ == '__main__':
    unittest.main()
