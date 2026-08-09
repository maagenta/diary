"""Hostile and malformed input (was robustness.c, now broader):
POST parsing edge cases, UPDATE/DELETE errors, handshake abuse."""
import base64
import time

import proto
import sodium
from harness import DiaryTest, ServerFixture

BACKDATE = 1720137600


class PostParsingTests(DiaryTest):

    def test_unknown_command_rejected(self):
        c = self.server.client()
        c.send_line("BOGUS")
        self.assertTrue(c.recv_line().startswith("FAIL"))
        c.quit()

    def test_digits_only_payload_is_data_not_timestamp(self):
        c = self.server.client()
        before = int(time.time())
        reply = c.post_raw("12345")
        self.assertTrue(reply.startswith("OK "))
        e = c.entry(int(reply[3:]))
        self.assertEqual(e["data"], "12345")
        self.assertGreaterEqual(e["ts"], before)
        c.quit()

    def test_nonnumeric_prefix_falls_back_to_server_clock(self):
        c = self.server.client()
        before = int(time.time())
        reply = c.post_raw("notanumber rest")
        self.assertTrue(reply.startswith("OK "))
        eid = int(reply[3:])
        # data contains a space, so parse the GET line manually
        c.send_line("GET")
        hdr = c.recv_line()
        found = None
        for _ in range(int(hdr[8:])):
            line = c.recv_line()
            lid, ts, data = line.split(" ", 2)
            if int(lid) == eid:
                found = (int(ts), data)
        self.assertIsNotNone(found)
        self.assertGreaterEqual(found[0], before)
        self.assertEqual(found[1], "notanumber rest")
        c.quit()

    def test_epoch_prefix_split_correctly(self):
        c = self.server.client()
        reply = c.post_raw(f"{BACKDATE} QUJD")
        self.assertTrue(reply.startswith("OK "))
        e = c.entry(int(reply[3:]))
        self.assertEqual(e["ts"], BACKDATE)
        self.assertEqual(e["data"], "QUJD")
        c.quit()

    def test_update_without_data_rejected(self):
        c = self.server.client()
        c.send_line("UPDATE 1")
        self.assertTrue(c.recv_line().startswith("FAIL"))
        c.quit()

    def test_update_nonexistent_id_rejected(self):
        c = self.server.client()
        self.assertTrue(c.update(99999, "ghost").startswith("FAIL"))
        c.quit()

    def test_delete_nonexistent_id_rejected(self):
        c = self.server.client()
        self.assertTrue(c.delete(99999).startswith("FAIL"))
        c.quit()


class HandshakeTests(DiaryTest):

    def test_garbage_instead_of_hello(self):
        s = proto.raw_socket(self.server.port)
        s.send_line("GARBAGE")
        self.assertEqual(s.recv_line(), "FAIL expected HELLO")
        s.close()

    def test_disconnect_mid_handshake_leaves_server_alive(self):
        s = proto.raw_socket(self.server.port)
        s.send_line("HELLO")
        self.assertTrue(s.recv_line().startswith("CHALLENGE "))
        s.close()                       # vanish without answering
        c = self.server.client()        # server must still serve
        c.quit()

    def test_nonallowlisted_key_refused(self):
        with self.assertRaises(proto.ProtoError) as ctx:
            proto.Client(self.server.port, proto.fresh_keys())
        self.assertIn("access denied", str(ctx.exception))

    def test_forged_signature_refused(self):
        # claim the allowlisted pubkey but sign with a different key
        impostor = dict(proto.fresh_keys())
        impostor["auth_pk"] = self.server.keys["auth_pk"]
        with self.assertRaises(proto.ProtoError) as ctx:
            proto.Client(self.server.port, impostor)
        self.assertIn("signature verification failed", str(ctx.exception))

    def test_unparseable_auth_line(self):
        s = proto.raw_socket(self.server.port)
        s.send_line("HELLO")
        s.recv_line()
        s.send_line("AUTH not-base64!!")
        self.assertTrue(s.recv_line().startswith("FAIL"))
        s.close()


class RegistrationTests(DiaryTest):
    # Needs a server whose user has never registered, so it gets its own
    # fixture (DiaryTest gives one per class) and a single ordered test.

    def test_invalid_enc_key_rejected_then_valid_accepted(self):
        keys = self.server.keys
        s = proto.raw_socket(self.server.port)
        s.send_line("HELLO")
        challenge = base64.b64decode(s.recv_line()[10:])
        sig = sodium.sign_detached(challenge, keys["auth_sk"])
        s.send_line("AUTH %s %s" % (
            base64.b64encode(keys["auth_pk"]).decode(),
            base64.b64encode(sig).decode()))
        self.assertEqual(s.recv_line(), "REGISTER")
        s.send_line("REGISTER not-valid-base64!!")
        self.assertTrue(s.recv_line().startswith("FAIL"))
        s.close()

        # the failed attempt must not have registered anything
        c = self.server.client()        # full handshake incl. REGISTER
        eid = c.post("first entry")
        self.assertEqual(c.entry(eid)["text"], "first entry")
        c.quit()
