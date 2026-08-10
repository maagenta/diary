# Tests

End-to-end suite in Python. Every test talks to a real `diary-server`
process over a real TCP socket; nothing is mocked. The protocol client
(`proto.py`) uses the **same libsodium** the C binaries link, bound via
`ctypes` (`sodium.py`) — so the crypto in the tests is bit-for-bit the
crypto in the product. The only third-party package is `pyte`, a
terminal emulator that turns the TUI's escape-sequence output into a
readable screen for assertions.

## Running

From the repository root:

```bash
make test
```

First run: creates a virtualenv in `tests/venv` and installs
`requirements.txt` into it. Later runs reuse the venv; the deps are
reinstalled only when `requirements.txt` changes. Exit code `0` means
all green, so it can gate a git hook or CI job.

Manual invocation (without the venv, everything runs except the
`test_tui.py` screen tests, which skip cleanly when `pyte` is absent):

```bash
python3 tests/run_tests.py            # whole suite
cd tests && python3 -m unittest test_timestamps -v   # one module
```

Requirements: `python3` (3.10+), a system libsodium (already required to
build), `make`/`cc` for the binaries.

## What a run does

`run_tests.py` builds `diary-server` and `diary-client` via make, then
runs `unittest` discovery. Each test class gets its own `ServerFixture`
(`harness.py`): a `mktemp` directory with fresh `keygen` keys, an empty
database, and a server process on a free port — torn down and deleted
afterward. Real databases and keys are never involved.

## Coverage

| Module | Checks |
|--------|--------|
| `test_timestamps.py` | `POST <epoch> <data>` stores the client's timestamp (verified on the wire **and** directly in the SQLite row); plain `POST` falls back to the server clock; `UPDATE` preserves the timestamp; epoch extremes (1970, beyond 32-bit) |
| `test_robustness.py` | unknown command; digits-only payload is data, not a timestamp; non-numeric prefix falls back safely; epoch prefix splits correctly; `UPDATE`/`DELETE` error paths; garbage instead of `HELLO`; disconnect mid-handshake; non-allowlisted key refused; **forged signature** refused; unparseable `AUTH`; `REGISTER` with invalid key rejected without corrupting registration |
| `test_content.py` | round-trips: UTF-8/emoji, embedded newlines, empty entry, ~100 KB entry (exceeds `MAX_LINE`, exercises the unbounded line reader) |
| `test_persistence.py` | entries and registration survive a server restart on the same DB |
| `test_concurrent.py` | 4 parallel clients × 5 posts all stored (found the missing SQLite busy timeout); interleaved sessions |
| `test_cli.py` | the real `diary-client` binary: `--entry-at` rejects bad input, `-v` works; `--post` saves stdin headlessly (with/without `--entry-at`, multiline) — its path skips `ui.c`, so `--post` green + TUI red pinpoints a bug in `ui.c`; the ncurses TUI driven through a pty types and saves a backdated entry, verified in the DB |
| `test_tui.py` | rendered-screen assertions via `pyte`: empty list screen and status bar; editor title shows the `--entry-at` date and the dirty `*` marker lifecycle; typed UTF-8 (accents + CJK) renders and round-trips; arrow-key cursor movement inserts mid-word; viewer header and body; delete confirmation flow, both confirmed and cancelled |

Known server limitation (documented in `test_concurrent.py`): several
*simultaneous first-ever* connections race on `REGISTER`; only one wins.
Harmless after the first successful connection.

## Layout

- `sodium.py` — ctypes bindings (sign/verify, sealed boxes, keypairs).
- `proto.py` — handshake + diary commands; `Client` for well-formed
  traffic, `raw_socket()` for hostile lines.
- `harness.py` — `ServerFixture` and the `DiaryTest` base class (one
  fresh server per test class); direct DB access via `db_query()`.
- Tests assume their class's server starts **empty**; create what you
  assert on, and prefer filtering by returned id over global counts.

## Adding a test

1. Add a method to an existing class, or a new `test_<area>.py` with a
   class extending `harness.DiaryTest`.
2. Get a session with `self.server.client()`, raw lines with
   `proto.raw_socket(self.server.port)`, DB truth with
   `self.server.db_query(...)`.
3. If it needs a pip package, add it to `requirements.txt` — `make test`
   reinstalls automatically.
