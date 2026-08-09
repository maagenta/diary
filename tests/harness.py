"""Scratch-server fixture shared by all test modules.

Each ServerFixture owns a throwaway directory (fresh keys, empty DB) and
one diary-server process on a free port. Nothing in the repo is touched.
"""
import os
import shutil
import socket
import sqlite3
import subprocess
import tempfile
import time
import unittest

import proto

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
SERVER_BIN = os.path.join(BUILD, "diary-server")
CLIENT_BIN = os.path.join(BUILD, "diary-client")
KEYGEN_BIN = os.path.join(BUILD, "keygen")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class ServerFixture:
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="diary-test-")
        subprocess.run([KEYGEN_BIN], cwd=self.dir, check=True,
                       capture_output=True)
        self.keys = proto.load_keys(self.dir)
        self.db = os.path.join(self.dir, "test.db")
        self.log = os.path.join(self.dir, "server.log")
        self.proc = None
        self.start()

    def start(self):
        self.port = free_port()
        with open(self.log, "ab") as log:
            self.proc = subprocess.Popen(
                [SERVER_BIN, "-p", str(self.port),
                 "-k", "auth.pub", "-db", self.db],
                cwd=self.dir, stdout=log, stderr=log)
        for _ in range(50):
            try:
                socket.create_connection(("127.0.0.1", self.port),
                                         timeout=0.2).close()
                return
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("server did not start; log:\n" + self.read_log())

    def stop_server(self):
        if self.proc:
            self.proc.terminate()
            self.proc.wait(timeout=5)
            self.proc = None

    def restart(self):
        """Same keys and database, new process (persistence tests)."""
        self.stop_server()
        self.start()

    def close(self):
        self.stop_server()
        shutil.rmtree(self.dir, ignore_errors=True)

    def client(self, keys=None):
        return proto.Client(self.port, keys or self.keys)

    def db_query(self, sql, params=()):
        """Read the SQLite file directly, bypassing server and wire."""
        con = sqlite3.connect(self.db)
        try:
            return con.execute(sql, params).fetchall()
        finally:
            con.close()

    def read_log(self):
        try:
            with open(self.log) as f:
                return f.read()
        except OSError:
            return ""


class DiaryTest(unittest.TestCase):
    """Base class: one fresh server per TestCase class."""
    server: ServerFixture

    @classmethod
    def setUpClass(cls):
        cls.server = ServerFixture()

    @classmethod
    def tearDownClass(cls):
        cls.server.close()
