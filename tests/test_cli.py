"""The real diary-client binary: flag validation, and the TUI driven
through a pseudo-terminal. Assertions are made on effects (server/DB),
not on rendered screens."""
import fcntl
import os
import pty
import select
import struct
import subprocess
import termios
import time

from harness import CLIENT_BIN, DiaryTest

ENTRY_AT = "2024-07-05 00:00"
ENTRY_AT_EPOCH = int(time.mktime(time.strptime(ENTRY_AT, "%Y-%m-%d %H:%M")))


class FlagTests(DiaryTest):

    def test_entry_at_garbage_rejected(self):
        r = subprocess.run([CLIENT_BIN, "--entry-at", "garbage"],
                           capture_output=True, text=True, timeout=10)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("entry-at", r.stderr)

    def test_entry_at_wrong_format_rejected(self):
        r = subprocess.run([CLIENT_BIN, "--entry-at", "06/07/2026 14:30"],
                           capture_output=True, text=True, timeout=10)
        self.assertNotEqual(r.returncode, 0)

    def test_version_flag(self):
        r = subprocess.run([CLIENT_BIN, "-v"],
                           capture_output=True, text=True, timeout=10)
        self.assertEqual(r.returncode, 0)
        self.assertIn("diary-client", r.stdout)


class PostTests(DiaryTest):
    """--post: headless entry creation. Its path is main.c + net.c +
    protocol lib with ui.c absent, so together with the TUI tests it
    bisects client failures: --post ok + TUI failing => bug is in ui.c."""

    def run_post(self, text, *extra_args):
        return subprocess.run(
            [CLIENT_BIN, "-p", str(self.server.port), "--post", *extra_args],
            input=text, capture_output=True, text=True,
            cwd=self.server.dir, timeout=15)

    def test_post_with_entry_at(self):
        r = self.run_post("posted headless", "--entry-at", ENTRY_AT)
        self.assertEqual(r.returncode, 0, r.stderr)
        eid = int(r.stdout.strip())
        rows = self.server.db_query(
            "SELECT timestamp FROM entries WHERE id = ?", (eid,))
        self.assertEqual(rows, [(ENTRY_AT_EPOCH,)])
        c = self.server.client()
        self.assertEqual(c.entry(eid)["text"], "posted headless")
        c.quit()

    def test_post_without_entry_at_uses_server_clock(self):
        before = int(time.time())
        r = self.run_post("clocked entry")
        self.assertEqual(r.returncode, 0, r.stderr)
        eid = int(r.stdout.strip())
        rows = self.server.db_query(
            "SELECT timestamp FROM entries WHERE id = ?", (eid,))
        self.assertGreaterEqual(rows[0][0], before)

    def test_post_multiline_stdin(self):
        text = "line one\nline two\n\nline four\n"
        r = self.run_post(text, "--entry-at", ENTRY_AT)
        self.assertEqual(r.returncode, 0, r.stderr)
        eid = int(r.stdout.strip())
        c = self.server.client()
        self.assertEqual(c.entry(eid)["text"], text)
        c.quit()


class TuiTests(DiaryTest):
    """Type a real entry into the ncurses editor over a pty and verify it
    lands in the database with the --entry-at timestamp."""

    def spawn_client(self, *extra_args):
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", 24, 80, 0, 0))
        env = dict(os.environ, TERM="xterm")
        proc = subprocess.Popen(
            [CLIENT_BIN, "-p", str(self.server.port), *extra_args],
            cwd=self.server.dir,             # auth.key / enc.key live here
            stdin=slave, stdout=slave, stderr=slave,
            env=env, start_new_session=True)
        os.close(slave)
        return proc, master

    def drain(self, master, seconds):
        """Consume TUI output for a while so the client never blocks."""
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([master], [], [], 0.1)
            if r:
                try:
                    if not os.read(master, 4096):
                        return
                except OSError:
                    return

    def send(self, master, data, settle=0.4):
        os.write(master, data)
        self.drain(master, settle)

    def wait_for_entry(self, timeout=10):
        end = time.time() + timeout
        while time.time() < end:
            rows = self.server.db_query(
                "SELECT id, timestamp FROM entries")
            if rows:
                return rows
            time.sleep(0.2)
        return []

    def test_tui_creates_backdated_entry(self):
        proc, master = self.spawn_client("--entry-at", ENTRY_AT)
        try:
            self.drain(master, 1.5)              # let the list screen draw
            self.send(master, b"n")              # new entry
            self.send(master, b"written in the tui")
            self.send(master, b"\x13")           # Ctrl+S: save (POST)

            rows = self.wait_for_entry()
            self.assertEqual(len(rows), 1)
            eid, ts = rows[0]
            self.assertEqual(ts, ENTRY_AT_EPOCH)

            self.send(master, b"\x1b")           # ESC: back to the list
            self.send(master, b"q")              # quit
            proc.wait(timeout=5)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            os.close(master)

        # decrypt what the TUI stored, via the protocol
        c = self.server.client()
        self.assertEqual(c.entry(eid)["text"], "written in the tui")
        c.quit()
