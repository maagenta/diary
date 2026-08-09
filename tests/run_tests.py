#!/usr/bin/env python3
"""Builds the binaries, then runs the whole suite. Exit code 0 = green.
Works from any directory; needs only python3 (stdlib) and libsodium."""
import os
import subprocess
import sys
import unittest

TESTS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TESTS)


def main():
    for target in ("server", "client"):
        r = subprocess.run(["make", "-C", ROOT, target],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            return 1

    sys.path.insert(0, TESTS)
    suite = unittest.defaultTestLoader.discover(TESTS)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
