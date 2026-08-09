"""Several simultaneous connections: fork-per-client + SQLite WAL."""
import threading

from harness import DiaryTest


class ConcurrencyTests(DiaryTest):

    def test_parallel_clients_all_stored(self):
        # register first: simultaneous FIRST contacts race on REGISTER
        # (all forked children see an unregistered user and all try the
        # insert; only one wins). Known server limitation — this test
        # targets concurrent posting on a registered account.
        self.server.client().quit()

        n_clients, n_posts = 4, 5
        errors = []

        def worker(k):
            try:
                c = self.server.client()
                for i in range(n_posts):
                    c.post(f"client{k} entry{i}", ts=1720137600 + k * 100 + i)
                c.quit()
            except Exception as exc:          # noqa: BLE001 — report in test
                errors.append(exc)

        threads = [threading.Thread(target=worker, args=(k,))
                   for k in range(n_clients)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=30)

        self.assertEqual(errors, [])
        c = self.server.client()
        texts = {e["text"] for e in c.entries()}
        c.quit()
        expected = {f"client{k} entry{i}"
                    for k in range(n_clients) for i in range(n_posts)}
        self.assertEqual(texts, expected)

    def test_two_open_sessions_interleaved(self):
        a = self.server.client()
        b = self.server.client()
        ida = a.post("from a", ts=1720137600)
        idb = b.post("from b", ts=1720137601)
        # each sees both, ids distinct
        self.assertNotEqual(ida, idb)
        self.assertEqual(a.entry(idb)["text"], "from b")
        self.assertEqual(b.entry(ida)["text"], "from a")
        a.quit()
        b.quit()
