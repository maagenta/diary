"""Entries must survive a server restart on the same database file."""
from harness import DiaryTest

BACKDATE = 1720137600


class PersistenceTests(DiaryTest):

    def test_entries_survive_restart(self):
        c = self.server.client()
        id1 = c.post("before restart", ts=BACKDATE)
        id2 = c.post("also before")
        ts2 = c.entry(id2)["ts"]
        c.quit()

        self.server.restart()

        c = self.server.client()
        entries = {e["id"]: e for e in c.entries()}
        self.assertEqual(entries[id1]["text"], "before restart")
        self.assertEqual(entries[id1]["ts"], BACKDATE)
        self.assertEqual(entries[id2]["text"], "also before")
        self.assertEqual(entries[id2]["ts"], ts2)
        c.quit()

    def test_registration_survives_restart(self):
        # first test already registered; after restart the server must
        # greet us with OK, not REGISTER (covered inside Client, which
        # would raise if the flow surprised it), and still serve
        self.server.restart()
        c = self.server.client()
        eid = c.post("post-restart entry")
        self.assertEqual(c.entry(eid)["text"], "post-restart entry")
        c.quit()
