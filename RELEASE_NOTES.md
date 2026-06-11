# Diary v1.0

First stable release of Diary — a personal diary application with end-to-end encryption and a client-server architecture. The server never has access to private keys or plaintext entries.

**Encryption:** Ed25519 challenge-based authentication, X25519 + XSalsa20-Poly1305 (`crypto_box_seal`) entry encryption, built on libsodium.

---

## Server

TCP server written in C, one forked process per client.

- Plain-text line protocol over TCP (default port **4242**): `HELLO` → `CHALLENGE` → `AUTH`, then `POST` / `GET` / `UPDATE` / `DELETE` / `QUIT`.
- Challenge-based Ed25519 authentication — the server only ever holds the user's public keys.
- First-connection user registration (`REGISTER`) with the client's public encryption key.
- Entries stored encrypted in a SQLite database (WAL mode); the server cannot read their contents.
- Configurable via CLI flags: `-p PORT`, `-k auth.pub`, `-db diary.db`.
- **Docker support**: ready-to-use `Dockerfile`; mount a volume with `auth.pub` and the database is created automatically at `/data/diary.db`.
- Companion `keygen` tool generates the Ed25519/X25519 key pairs (`auth.key`, `auth.pub`, `enc.key`).

## CLI client

Terminal client written in C with an ncurses TUI.

- Full entry management: create, read, edit and delete entries from the terminal.
- Local decryption with the private key (`enc.key`); nothing leaves the machine unencrypted.
- Keyboard-driven interface: `N` new, `Enter` read, `E` edit, `D` delete, `R` reload, `Q` quit; `Ctrl+S`/`F2` to save in the editor.
- Connection options via CLI flags: `-h HOST`, `-p PORT`, `-a auth.key`, `-e enc.key`.
- All UI text and messages in English.
- Builds on macOS and Linux with `make` (depends on libsodium and ncurses).

## Android client

Native Android app (minSdk 24, compileSdk 35) speaking the same protocol as the CLI client.

- libsodium compiled from source via the NDK (CMake) for `arm64-v8a`, `armeabi-v7a` and `x86_64`; crypto exposed to Java through a JNI bridge.
- Setup screen to import `auth.key` and `enc.key` with the system file picker and configure the server host/port.
- Entry list sorted newest first, with previews; long-press an entry to edit or delete it.
- Full-screen distraction-free editor with debounced autosave (saves 2 s after you stop typing, and only when the text actually changed).
- Automatic reconnection on connection loss.
- Local TXT backup of entries before each server sync.
- Entries reload automatically when returning from the editor.
- App icon included; all networking and crypto run on background threads.
