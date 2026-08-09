"""TUI screen tests: the real diary-client on a pty, its output fed to a
pyte terminal emulator so assertions run against the *rendered screen* —
titles, list rows, status bars, cursor edits — not just DB effects.
"""
import fcntl
import os
import pty
import select
import struct
import subprocess
import termios
import time
import unittest

from harness import CLIENT_BIN, DiaryTest

try:
    import pyte
    HAS_PYTE = True
except ImportError:          # suite still runs without the venv
    HAS_PYTE = False

ENTRY_AT = "2024-07-05 00:00"
ENTRY_AT_EPOCH = int(time.mktime(time.strptime(ENTRY_AT, "%Y-%m-%d %H:%M")))

LEFT = b"\x1bOD"   # application cursor-key mode (smkx), as keypad() sets
CTRL_S = b"\x13"
ESC = b"\x1b"
ENTER = b"\n"


class TuiSession:
    """diary-client on an 80x24 pty, mirrored into a pyte screen."""

    def __init__(self, server, *extra_args):
        self.screen = pyte.Screen(80, 24)
        self.stream = pyte.ByteStream(self.screen)
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", 24, 80, 0, 0))
        env = dict(os.environ, TERM="xterm",
                   LANG="en_US.UTF-8", LC_ALL="en_US.UTF-8")
        self.proc = subprocess.Popen(
            [CLIENT_BIN, "-p", str(server.port), *extra_args],
            cwd=server.dir,              # auth.key / enc.key live here
            stdin=slave, stdout=slave, stderr=slave,
            env=env, start_new_session=True)
        os.close(slave)
        self.master = master

    def pump(self, seconds=0.3):
        """Read TUI output for a while and feed it to the emulator."""
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if not r:
                continue
            try:
                data = os.read(self.master, 4096)
            except OSError:
                return
            if not data:
                return
            self.stream.feed(data)

    def text(self):
        # not screen.display: pyte's own renderer crashes on the empty
        # placeholder cells that follow wide (CJK) characters
        lines = []
        for y in range(self.screen.lines):
            row = self.screen.buffer[y]
            lines.append("".join(row[x].data
                                 for x in range(self.screen.columns)))
        return "\n".join(lines)

    def wait_for(self, needle, timeout=8):
        end = time.time() + timeout
        while time.time() < end:
            self.pump(0.2)
            if needle in self.text():
                return
        raise AssertionError(
            f"never saw {needle!r} on screen; last screen:\n{self.text()}")

    def keys(self, data, settle=0.3):
        if isinstance(data, str):
            data = data.encode()
        os.write(self.master, data)
        self.pump(settle)

    def close(self):
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        os.close(self.master)

    def abort(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        os.close(self.master)


@unittest.skipUnless(HAS_PYTE, "pyte not installed (see requirements.txt)")
class PyteTest(DiaryTest):
    """Base: spawn/teardown one TuiSession per test."""

    def tui(self, *extra_args):
        self.session = TuiSession(self.server, *extra_args)
        return self.session

    def tearDown(self):
        if getattr(self, "session", None):
            self.session.abort()
            self.session = None

    def quit_cleanly(self, t):
        t.keys("q")
        t.close()
        self.session = None


class ListScreenTests(PyteTest):

    def test_empty_list_renders(self):
        t = self.tui()
        t.wait_for("DIARY")
        t.wait_for("No entries yet. Press [N] to create one.")
        t.wait_for("[N]ew  [E]dit  [Enter]Read  [D]elete  [R]eload  [Q]uit")
        self.quit_cleanly(t)


class EditorScreenTests(PyteTest):

    def test_editor_title_shows_entry_at_date_and_dirty_marker(self):
        t = self.tui("--entry-at", ENTRY_AT)
        t.wait_for("DIARY")
        t.keys("n")
        # the cosmetic title must show the personalized date, clean
        t.wait_for(f"New entry - {ENTRY_AT}")
        t.wait_for("[Ctrl+S/F2] save  [ESC] exit")
        t.keys("x")
        t.wait_for(f"New entry - {ENTRY_AT} *")   # dirty marker
        t.keys(CTRL_S)
        t.wait_for("Saved.")
        t.pump(0.5)
        self.assertNotIn(f"New entry - {ENTRY_AT} *", t.text())  # marker gone
        t.keys(ESC)
        t.wait_for("DIARY")
        self.quit_cleanly(t)

    def test_typed_utf8_renders_and_round_trips(self):
        t = self.tui("--entry-at", ENTRY_AT)
        t.wait_for("DIARY")
        t.keys("n")
        t.wait_for("New entry")
        t.keys("café día 日記".encode())
        t.wait_for("café día")           # rendered in the editor
        t.keys(CTRL_S)
        t.wait_for("Saved.")
        t.keys(ESC)
        t.wait_for(ENTRY_AT)             # list row shows the backdated date
        t.wait_for("café día")           # and the preview
        self.quit_cleanly(t)

        c = self.server.client()
        texts = [e["text"] for e in c.entries()]
        c.quit()
        self.assertIn("café día 日記", texts)   # full text incl. wide chars

    def test_cursor_movement_inserts_mid_word(self):
        t = self.tui()
        t.wait_for("DIARY")
        t.keys("n")
        t.wait_for("New entry")
        t.keys("helo")
        t.wait_for("helo")
        t.keys(LEFT * 2)                 # cursor between "he" and "lo"
        t.keys("l")
        t.wait_for("hello")              # rendered with mid-word insert
        t.keys(CTRL_S)
        t.wait_for("Saved.")
        t.keys(ESC)
        t.wait_for("DIARY")
        self.quit_cleanly(t)

        c = self.server.client()
        texts = [e["text"] for e in c.entries()]
        c.quit()
        self.assertIn("hello", texts)    # buffer edit survived to the server


class ViewerScreenTests(PyteTest):

    def test_viewer_shows_entry_and_header(self):
        c = self.server.client()
        eid = c.post("a full entry body to read", ts=ENTRY_AT_EPOCH)
        c.quit()

        t = self.tui()
        t.wait_for(ENTRY_AT)                       # list row
        t.keys(ENTER)
        t.wait_for(f"Entry #{eid} - {ENTRY_AT}")   # viewer header
        t.wait_for("a full entry body to read")
        t.wait_for("[j/k or scroll] navigate")
        t.keys("q")                                # back to the list
        t.wait_for("DIARY")
        self.quit_cleanly(t)


class DeleteFlowTests(PyteTest):

    def test_delete_asks_confirmation_then_removes(self):
        c = self.server.client()
        c.post("doomed entry", ts=ENTRY_AT_EPOCH)
        c.quit()

        t = self.tui()
        t.wait_for("doomed entry")
        t.keys("d")
        t.wait_for("Confirm delete: [Y]es  [N/ESC] cancel")
        t.keys("y")
        t.wait_for("Entry deleted.")
        t.wait_for("No entries yet. Press [N] to create one.")
        self.quit_cleanly(t)

        self.assertEqual(self.server.db_query("SELECT COUNT(*) FROM entries"),
                         [(0,)])

    def test_delete_can_be_cancelled(self):
        c = self.server.client()
        c.post("survivor entry", ts=ENTRY_AT_EPOCH)
        c.quit()

        t = self.tui()
        t.wait_for("survivor entry")
        t.keys("d")
        t.wait_for("Confirm delete:")
        t.keys(ESC)                       # cancel
        t.pump(0.5)
        t.keys("r")                       # reload; entry must still exist
        t.wait_for("survivor entry")
        self.quit_cleanly(t)

        self.assertEqual(self.server.db_query("SELECT COUNT(*) FROM entries"),
                         [(1,)])
