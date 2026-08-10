# Architecture

How the diary client is structured, why it looks this way, and where it
plugs into the [proto library](../protocol/architecture.md).

## The problem it solves

The proto library handles connecting, authenticating, registering, and
framing; the server stores ciphertext it cannot read. What remains — and
what this client is — falls into three layers:

- **`main`** — CLI, key loading, connect, and the choice between two
  frontends (interactive TUI or one-shot `--post`);
- **`net`** — the client half of the diary dialect: seal plaintext, send
  a verb, parse the reply, unseal what comes back;
- **`ui`** — an ncurses TUI (list, viewer, editor) that only ever sees
  plaintext and never touches a socket directly.

All encryption and decryption happen here, on the client: entries are
sealed to the user's own X25519 public key before they leave the process,
and opened with `enc.key` after they return. The server and the wire only
ever carry ciphertext (see
[Trust model](../protocol/architecture.md#trust-model)).

## Layered modules

```
        ┌────────────────────────────────┐
        │  main          main()          │   CLI, keys, connect, mode select
        └───────┬───────────────┬────────┘
                │ --post        │ default
                │               ▼
                │       ┌────────────────┐
                │       │  ui   ui_run() │   ncurses screens (plaintext only)
                │       └───────┬────────┘
                ▼               ▼
        ┌────────────────────────────────┐
        │  net           net_*()         │   dialect + seal/unseal (zone 2)
        └────────────────────────────────┘
```

Dependencies point strictly downward: `ui` calls `net`, never the library
wire functions; `net` is the only file that reads or writes the socket
after the handshake. The split mirrors the server's
`client_handler`/`storage` split — `net.c` and
`server/client_handler.c` are the two halves of one zone-2 agreement (see
[Anatomy of a session](../protocol/architecture.md#anatomy-of-a-session)),
and neither ncurses nor SQLite knows the dialect exists.

**What links in:** `main.c net.c ui.c` plus the library's client set
(`wire.c crypto.c auth.c` — no `serve.c`), against `-lsodium` and
ncurses (`-lncursesw` on Linux for wide-char UTF-8; on macOS the system
`-lncurses` already includes it — see `Makefile`).

## What each file contains

### `main.c` — startup and mode selection

- **CLI**: `-h host` (default `127.0.0.1`), `-p port` (default
  `DIARY_PORT`, 4242), `-a`/`-e` key file paths (defaults `auth.key`,
  `enc.key`), `--entry-at "YYYY-MM-DD HH:MM"`, `--post`, `-v`.
- **Session setup**: `setlocale` (required for UTF-8 ncurses),
  `proto_init`, `proto_load_keys` into a `proto_conn_t`, then
  `proto_connect` — which runs the whole handshake, including the
  `REGISTER` exchange on first contact. By the time either frontend
  starts, the connection is authenticated and registered.
- **`--entry-at`**: parsed with `strptime` as local time (`mktime`
  resolves DST) into an epoch that is carried through the session and
  attached to every *new* entry (`POST <epoch> <data>`); without it the
  server stamps its own clock. It backdates new entries only — `UPDATE`
  never touches an entry's timestamp.
- **`--post`**: scripting mode. Read all of stdin, save it as one entry,
  print the assigned id on stdout, exit nonzero on failure — no TUI, so
  the diary can be fed from pipes and cron jobs. Combines with
  `--entry-at`.

### `net.h` / `net.c` — the dialect, client side

Four functions, one per verb, each a complete request/reply round trip on
`conn->fd`. This is the only place the diary's wire vocabulary appears in
the client:

| Function | Sends | Expects | Returns |
|---|---|---|---|
| `net_post_entry` | `POST [<epoch>] <data_b64>` | `OK <id>` | id, or -1 |
| `net_update_entry` | `UPDATE <id> <data_b64>` | `OK` | id, or -1 |
| `net_delete_entry` | `DELETE <id>` | `OK` | 0, or -1 |
| `net_get_entries` | `GET` | `ENTRIES <n>` + n rows | array of `diary_entry_t` |

Details worth knowing:

- **Sealing.** `net_post_entry` and `net_update_entry` call
  `proto_seal_new(text, strlen(text), conn->keys.enc_pk)` — sealed to the
  user's *own* encryption key, so only this user's `enc.key` can open the
  result. The base64 line is sent with `proto_send_prefixed`, which
  streams `"POST … " + payload` without assembling one large buffer.
- **Unsealing.** `net_get_entries` reads each row with
  `proto_recv_line_dyn` (rows carry blobs of unbounded size), splits
  `<id> <ts> <data_b64>` with `strtok_r`, and opens each blob with
  `proto_unseal_new`. A blob that fails to open becomes the literal text
  `[could not decrypt]` rather than aborting the whole list — one corrupt
  or foreign-key entry must not hide the rest.
- **Ordering is a client concern.** The server returns rows in id order;
  the client sorts newest-first by timestamp (ties broken by higher id,
  so same-minute entries keep creation order). The `diary_entry_t` the UI
  consumes is already decrypted and sorted.
- Short control replies (`OK <id>`, `ENTRIES <n>`) are read with
  fixed-buffer `proto_recv_line`, per the library's
  [framing rules](../protocol/architecture.md#framing-rules).

### `ui.h` / `ui.c` — the ncurses TUI

One entry point, `ui_run(conn, entry_at)`, which owns the terminal for
the whole session. Three screens, each a self-contained loop:

- **List** (`screen_list`) — the hub. Fetches entries via
  `net_get_entries`, shows `date  first-line-preview` rows, and
  dispatches: `N`ew, `E`dit, `Enter` read, `D`elete (with a `y/n`
  confirmation), `R`eload, `Q`uit. After any operation that changes the
  server state it re-fetches the list rather than patching local state —
  the server is the single source of truth.
- **Viewer** (`screen_view`) — read-only, scrollable (`j`/`k`, mouse
  wheel), with a line-position indicator.
- **Editor** (`screen_editor`) — one buffer for both new entries
  (`entry_id == 0`, saved with `net_post_entry`) and edits
  (`entry_id > 0`, saved with `net_update_entry`). After the first
  successful save of a new entry it adopts the assigned id, so later
  saves in the same session become `UPDATE`s instead of duplicate
  `POST`s.

Editor mechanics worth knowing:

- **The buffer is UTF-8 bytes, not characters.** Cursor movement,
  backspace and delete step over whole UTF-8 sequences by skipping
  continuation bytes (`10xxxxxx`), which take no screen column. Text soft-
  wraps at `cols - 1`; `get_vpos`/`vpos_to_idx` map between byte offsets
  and visual row/column, so arrow keys move visually.
- **Autosave with a debounce.** `wtimeout` makes `wgetch` return after
  `AUTOSAVE_MS` (2 s) of idle; if the buffer is dirty, that timeout is
  treated as a Ctrl+S. Explicit save is Ctrl+S, F2 or Ctrl+W — reaching
  Ctrl+S at all requires temporarily clearing the terminal's `IXON` flag
  (otherwise it is flow control), restored on exit.
- **Crash safety.** Before every network save the buffer is written to a
  local backup file (`diary_<id>.tmp` / `diary_new.tmp`), removed only
  after the server confirms. If a save fails or the client dies mid-
  session, the plaintext survives on disk.
- **ESC discards deliberately.** Leaving with unsaved changes takes a
  second ESC after a warning.
- **macOS mouse quirk.** The system ncurses predates `BUTTON5_PRESSED`
  (scroll down) and packs button state in 6-bit groups; `ui.c` defines it
  as `BUTTON4_PRESSED << 6` and requests it explicitly in the mouse mask.

## What the library owns vs what this client owns

| Concern | Library | This client |
|---|---|---|
| Connect, handshake, registration | yes (`proto_connect`) | calls it |
| Line framing | yes | — |
| Seal / unseal primitives | yes | `net.c` calls them per entry |
| Command verbs and reply parsing | — | yes (`net.c`) |
| Entry ordering, previews, timestamps display | — | yes (client-side) |
| Editing, autosave, local backups | — | yes (`ui.c`) |
| Key files on disk | reads them (`proto_load_keys`) | user generates with `keygen` |
| Host / port / key paths | — | yes (CLI) |
