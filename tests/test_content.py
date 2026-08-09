"""Entry content round-trips: seal -> base64 -> wire -> DB and back."""
from harness import DiaryTest


class ContentTests(DiaryTest):

    def roundtrip(self, text):
        c = self.server.client()
        eid = c.post(text, ts=1720137600)
        got = c.entry(eid)["text"]
        c.quit()
        self.assertEqual(got, text)

    def test_utf8(self):
        self.roundtrip("café, naïve, 日記, здравствуйте, 🌒🖤")

    def test_embedded_newlines(self):
        self.roundtrip("dear diary,\n\ntoday I wrote\nseveral lines\n")

    def test_empty_entry(self):
        self.roundtrip("")

    def test_large_entry(self):
        # ~100 KB: its base64 exceeds MAX_LINE (16384), exercising the
        # unbounded proto_recv_line_dyn path on both sides
        self.roundtrip("A long day. " * 8500)
