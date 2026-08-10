# Architecture

How the diary server is structured, why it looks this way, and where it
plugs into the [proto library](../protocol/architecture.md).

## The problem it solves

The proto library freezes everything that is identical across its
consumers — framing, crypto, authentication, the accept loop — and leaves
three things open: the command vocabulary, the registration persistence,
and storage. The server is exactly those three things, and nothing else:

- a **dialect** (`POST`/`GET`/`UPDATE`/`DELETE`) dispatched per connection;
- the **server half of registration** (persisting a new client's
  encryption public key);
- **storage** (one SQLite file).

It is an *authenticated dumb blob store*: it verifies who is talking
(delegated to the library), then stores and returns base64 ciphertext it
cannot open. See [Trust model](../protocol/architecture.md#trust-model)
for what that buys and costs.

## Layered modules

```
        ┌────────────────────────────────┐
        │  main            main()        │   CLI, key/db setup, start harness
        └───────────────┬────────────────┘
                        │ passes diary_handle to proto_serve()
        ┌───────────────▼────────────────┐
        │  client_handler  diary_handle()│   the diary dialect (zone 2)
        └───────────────┬────────────────┘
                        │
        ┌───────────────▼────────────────┐
        │  storage         storage_*()   │   SQLite: users + entries
        └────────────────────────────────┘
```

Dependencies point strictly downward. `storage` knows nothing about
sockets or the protocol; `client_handler` knows nothing about SQL. The
library sits beside this stack, not inside it: `main` calls `proto_init`,
`proto_load_pubkey_hex` and `proto_serve`; `client_handler` uses only the
`wire` functions plus `protocol.h` constants.

**What links in:** `main.c client_handler.c storage.c` plus the library's
server set (`wire.c crypto.c auth.c serve.c`), against `-lsodium
-lsqlite3` (see `Makefile`).

## What each file contains

### `main.c` — startup and policy

Everything that happens once, before the first connection:

- **CLI**: `-p port` (default `DIARY_PORT`, 4242), `-k pub-key-file`
  (required — the single-identity allowlist), `-db database-file`
  (required), `-v` (version). Port and size limits are app policy, so
  they live here and in `common/diary.h`, not in the library.
- **Key load**: `proto_load_pubkey_hex` turns `auth.pub` into the hex
  allowlist string `proto_serve` compares identities against.
- **Database preparation**: `ensure_db_dir` creates the containing
  directory (`mkdir -p` equivalent) and checks writability with clear
  errors; then the database is opened once (`storage_init`) and
  immediately closed — purely to create the file and schema up front and
  fail at startup, not at the first connection. Each connection re-opens
  it (next section).
- **Handoff**: `proto_serve(port, allowed_hex, diary_handle, db_path)`.
  The db *path* — not a handle — is the opaque `ctx`, because the handler
  runs in a forked child and must open its own connection.

### `client_handler.h` / `client_handler.c` — the dialect

One exported function, `diary_handle`, the `proto_handler_fn` that
`proto_serve` calls in a forked child after the handshake succeeds. It is
the entire zone-2 vocabulary of the diary (see
[Anatomy of a session](../protocol/architecture.md#anatomy-of-a-session)):

| Request | Reply | Notes |
|---|---|---|
| `POST <data_b64>` | `OK <id>` / `FAIL …` | server stamps `time(NULL)` |
| `POST <epoch> <data_b64>` | `OK <id>` / `FAIL …` | client-chosen timestamp |
| `GET` | `ENTRIES <n>` + `n` rows `<id> <ts> <data_b64>` | ordered by id, ascending |
| `UPDATE <id> <data_b64>` | `OK` / `FAIL entry not found` | |
| `DELETE <id>` | `OK` / `FAIL entry not found` | |
| `QUIT` | `BYE` | ends the loop |
| anything else | `FAIL unknown command` | |

Details worth knowing:

- **Registration reply.** The library's `proto_auth_server` authenticates
  but deliberately does not persist anything, so the post-`AUTH` reply is
  the handler's first act: `OK` if `storage_user_exists`, otherwise
  `REGISTER`, then read `REGISTER <enc_pubkey_b64>`, validate the base64
  decodes to exactly `ENC_PK_LEN` bytes, and persist it. This is the
  server half of the auto-registration described in
  [Handshake sequence](../protocol/architecture.md#handshake-sequence).
- **The two `POST` forms are distinguished by a space.** Entry data is
  base64, which never contains a space, so `POST <epoch> <data>` is
  unambiguous: a space in the payload means the first token is a
  client-chosen epoch (used by the client's `--entry-at`). A non-positive
  epoch falls back to the server clock.
- **Commands are read with `proto_recv_line_dyn`** (payload lines carry
  sealed blobs of unbounded size); replies go out with `proto_send_line`.
- **`GET` buffers before sending.** Rows are accumulated via the
  `storage_get_entries` callback so the `ENTRIES <n>` header can carry an
  exact count, capped at `MAX_ENTRIES` (1024) rows per reply.

### `storage.h` / `storage.c` — persistence

The only file that speaks SQL; a thin CRUD layer over one SQLite
database, using a module-level handle (`storage_init` / `storage_close`
per process — fine under fork-per-connection, where each child is
single-threaded). Two tables:

```sql
users   (auth_pubkey TEXT PRIMARY KEY, encrypt_pubkey TEXT)
entries (id INTEGER PRIMARY KEY AUTOINCREMENT,
         auth_pubkey TEXT, timestamp INTEGER, data TEXT)
        + index on entries(auth_pubkey)
```

Everything is text because everything arrives wire-ready: identities are
hex, keys and payloads are base64. The server stores lines and serves
lines; it never decodes `data`.

- **Ownership is enforced in the queries.** Every entry statement filters
  on `auth_pubkey = ?` — `UPDATE`/`DELETE` on someone else's id (or a
  nonexistent one) changes zero rows and returns `-1`, which the handler
  reports as `FAIL entry not found`. One database can therefore hold
  multiple identities safely, even though `proto_serve` currently allows
  only one.
- **Concurrency.** Connections are forked processes sharing nothing in
  memory, so the database is the only shared state. WAL mode plus a 5 s
  busy timeout lets concurrent children read during a write and queue
  behind a writer instead of failing with `SQLITE_BUSY`.
- All statements are prepared with bound parameters — no SQL is ever
  built from wire input.

## Process model and lifetime of a connection

```
main:   parse CLI → load allowlist → create/verify db → proto_serve()
                                                            │ accept
                                                            │ fork
child:  proto_auth_server()  ──ok──►  diary_handle()
            │                             │ storage_init(db_path)
            │                             │ OK / REGISTER exchange
            │                             │ command loop … QUIT
            │                             │ storage_close()
            └─fail: FAIL sent, child exits┘  child exits
```

Per-connection state (the SQLite handle, the command loop) lives in the
child and dies with it; anything that must survive goes through the
database. This is the
[fork model caveat](../protocol/architecture.md#extension-points) applied:
the diary shares nothing in memory by construction.

## What the library owns vs what this server owns

| Concern | Library | This server |
|---|---|---|
| TCP accept / fork / reaping | yes (`proto_serve`) | — |
| Challenge–response auth, allowlist compare | yes | loads `auth.pub` and passes it |
| Registration (persisting the enc pubkey) | — | yes (`diary_handle` + `users` table) |
| Command verbs and reply formats | — | yes (`client_handler.c`) |
| Storage | — | yes (`storage.c`, SQLite) |
| Port / limits | — | yes (`common/diary.h`, CLI) |
| Payload contents | sealed by the client | never readable here |
