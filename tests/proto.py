"""Python speaker of the diary protocol, mirroring protocol.h:

  C->S: HELLO
  S->C: CHALLENGE <b64>
  C->S: AUTH <auth_pk_b64> <sig_b64>
  S->C: OK | REGISTER | FAIL <reason>
  (REGISTER:)  C->S: REGISTER <enc_pk_b64>   S->C: OK

Entries are crypto_box_seal'd and base64'd, exactly as client/net.c does.
"""
import base64
import socket

import sodium


class ProtoError(Exception):
    pass


def load_keys(keydir):
    """Read keygen's auth.key / enc.key (one base64 line each)."""
    def b64file(name):
        with open(f"{keydir}/{name}") as f:
            return base64.b64decode(f.readline().strip())
    auth_sk = b64file("auth.key")            # seed(32) || pubkey(32)
    enc_sk = b64file("enc.key")
    return {
        "auth_sk": auth_sk,
        "auth_pk": auth_sk[32:],
        "enc_sk": enc_sk,
        "enc_pk": sodium.scalarmult_base(enc_sk),
    }


def fresh_keys():
    """A valid keypair set the server has never heard of."""
    auth_pk, auth_sk = sodium.sign_keypair()
    enc_pk, enc_sk = sodium.box_keypair()
    return {"auth_sk": auth_sk, "auth_pk": auth_pk,
            "enc_sk": enc_sk, "enc_pk": enc_pk}


def raw_socket(port, host="127.0.0.1", timeout=5):
    """A connected socket with line helpers but no handshake at all."""
    sock = socket.create_connection((host, port), timeout=timeout)
    return _LineSocket(sock)


class _LineSocket:
    def __init__(self, sock):
        self.sock = sock
        self.f = sock.makefile("rwb")

    def send_line(self, line):
        if isinstance(line, str):
            line = line.encode()
        self.f.write(line + b"\n")
        self.f.flush()

    def recv_line(self):
        raw = self.f.readline()
        if not raw:
            return None                      # EOF: peer closed
        return raw.rstrip(b"\r\n").decode(errors="replace")

    def close(self):
        try:
            self.f.close()
            self.sock.close()
        except OSError:
            pass


class Client(_LineSocket):
    """Authenticated session. Registers automatically on first contact,
    like proto_connect does."""

    def __init__(self, port, keys, host="127.0.0.1", timeout=5):
        super().__init__(socket.create_connection((host, port),
                                                  timeout=timeout))
        self.keys = keys
        self._handshake()

    def _handshake(self):
        self.send_line("HELLO")
        line = self.recv_line()
        if not line or not line.startswith("CHALLENGE "):
            raise ProtoError(f"expected CHALLENGE, got {line!r}")
        challenge = base64.b64decode(line[10:])
        sig = sodium.sign_detached(challenge, self.keys["auth_sk"])
        self.send_line("AUTH %s %s" % (
            base64.b64encode(self.keys["auth_pk"]).decode(),
            base64.b64encode(sig).decode()))
        line = self.recv_line()
        if line == "REGISTER":
            self.send_line("REGISTER %s" %
                           base64.b64encode(self.keys["enc_pk"]).decode())
            line = self.recv_line()
        if line != "OK":
            raise ProtoError(f"handshake refused: {line!r}")

    # ---- diary commands (mirror client/net.c) ----

    def seal_b64(self, text):
        return base64.b64encode(
            sodium.seal(text.encode(), self.keys["enc_pk"])).decode()

    def post(self, text, ts=None):
        """Returns the assigned id. ts=None sends the plain old form."""
        payload = self.seal_b64(text)
        if ts is not None:
            payload = f"{ts} {payload}"
        reply = self.post_raw(payload)
        if not reply.startswith("OK "):
            raise ProtoError(f"POST refused: {reply!r}")
        return int(reply[3:])

    def post_raw(self, payload):
        """POST an arbitrary payload verbatim; returns the reply line."""
        self.send_line(f"POST {payload}")
        return self.recv_line()

    def entries(self):
        """[{id, ts, data, text}] — text is None if undecryptable."""
        self.send_line("GET")
        hdr = self.recv_line()
        if not hdr or not hdr.startswith("ENTRIES "):
            raise ProtoError(f"expected ENTRIES, got {hdr!r}")
        out = []
        for _ in range(int(hdr[8:])):
            eid, ts, data = self.recv_line().split(" ", 2)
            try:
                plain = sodium.seal_open(base64.b64decode(data),
                                         self.keys["enc_pk"],
                                         self.keys["enc_sk"])
            except Exception:
                plain = None
            out.append({"id": int(eid), "ts": int(ts), "data": data,
                        "text": plain.decode() if plain is not None else None})
        return out

    def entry(self, eid):
        for e in self.entries():
            if e["id"] == eid:
                return e
        return None

    def update(self, eid, text):
        self.send_line(f"UPDATE {eid} {self.seal_b64(text)}")
        return self.recv_line()

    def delete(self, eid):
        self.send_line(f"DELETE {eid}")
        return self.recv_line()

    def quit(self):
        self.send_line("QUIT")
        reply = self.recv_line()
        self.close()
        return reply
