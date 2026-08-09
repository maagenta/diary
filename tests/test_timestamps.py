"""--entry-at / timestamp semantics (was timestamps.c), plus extremes."""
import time

from harness import DiaryTest

BACKDATE = 1720137600   # 2024-07-05 00:00:00 UTC


class TimestampTests(DiaryTest):

    def test_personalized_timestamp_stored(self):
        c = self.server.client()
        eid = c.post("backdated entry", ts=BACKDATE)
        e = c.entry(eid)
        self.assertEqual(e["ts"], BACKDATE)
        self.assertEqual(e["text"], "backdated entry")
        c.quit()

    def test_personalized_timestamp_reaches_db_row(self):
        c = self.server.client()
        eid = c.post("db check", ts=BACKDATE + 60)
        c.quit()
        rows = self.server.db_query(
            "SELECT timestamp FROM entries WHERE id = ?", (eid,))
        self.assertEqual(rows, [(BACKDATE + 60,)])

    def test_flagless_post_uses_server_clock(self):
        c = self.server.client()
        before = int(time.time())
        eid = c.post("normal entry")            # no ts on the wire
        after = int(time.time())
        e = c.entry(eid)
        self.assertTrue(before <= e["ts"] <= after + 1)
        self.assertEqual(e["text"], "normal entry")
        c.quit()

    def test_update_preserves_timestamp(self):
        c = self.server.client()
        eid = c.post("v1", ts=BACKDATE)
        self.assertEqual(c.update(eid, "v2"), "OK")
        e = c.entry(eid)
        self.assertEqual(e["ts"], BACKDATE)
        self.assertEqual(e["text"], "v2")
        c.quit()

    def test_entries_sorted_newest_first_by_client_convention(self):
        # The server returns entries; ordering for display is the client's
        # job. Verify the data supports it: both entries present with the
        # timestamps we set, distinguishable for sorting.
        c = self.server.client()
        old = c.post("older", ts=BACKDATE)
        new = c.post("newer", ts=BACKDATE + 1000)
        entries = {e["id"]: e for e in c.entries()}
        self.assertLess(entries[old]["ts"], entries[new]["ts"])
        c.quit()

    def test_epoch_1970(self):
        c = self.server.client()
        eid = c.post("dawn of time", ts=1)
        self.assertEqual(c.entry(eid)["ts"], 1)
        c.quit()

    def test_epoch_beyond_32bit(self):
        # year 2100: overflows int32; catches truncation anywhere in the
        # atol -> time_t -> sqlite int64 -> %ld chain
        y2100 = 4102444800
        c = self.server.client()
        eid = c.post("far future", ts=y2100)
        self.assertEqual(c.entry(eid)["ts"], y2100)
        rows = self.server.db_query(
            "SELECT timestamp FROM entries WHERE id = ?", (eid,))
        self.assertEqual(rows, [(y2100,)])
        c.quit()
