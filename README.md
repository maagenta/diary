<p align="right"><img width="128px" src="https://github.com/maagenta/diary/blob/main/icon/256.png?raw=true"></p>

# Diary 

Personal diary application with end-to-end encryption, accessible over the network. Client-server architecture written in C.

## Architecture

- **Server**: TCP socket in C. Stores entries in a SQLite database, encrypted with the user's public key. Never has access to private keys.
- **Client**: TUI in ncurses. Reads the private key from a local file to decrypt entries.

## Encryption

| Purpose | Algorithm | Files |
|---------|-----------|-------|
| Authentication | Ed25519 (challenge signing) | `auth.key`, `auth.pub` |
| Entry encryption | X25519 + XSalsa20-Poly1305 (`crypto_box_seal`) | `enc.key` |

The server stores entries encrypted with the user's public encryption key. Only the client with the private key can decrypt them.

## Protocol

```
C→S: HELLO
S→C: CHALLENGE <base64>
C→S: AUTH <auth_pubkey_b64> <signature_b64>
S→C: OK  |  REGISTER

# New user (REGISTER):
C→S: REGISTER <enc_pubkey_b64>
S→C: OK

# New entry:
C→S: POST <encrypted_entry_b64>
S→C: OK <id>  |  FAIL

# Get entries:
C→S: GET
S→C: ENTRIES <n>
     <id> <timestamp> <encrypted_entry_b64>  (× n)

# Edit entry:
C→S: UPDATE <id> <encrypted_entry_b64>
S→C: OK  |  FAIL

# Delete entry:
C→S: DELETE <id>
S→C: OK  |  FAIL

C→S: QUIT
S→C: BYE
```

Default port: **4242**

## Dependencies

```bash
# macOS
brew install libsodium sqlite ncurses
```

| Library | Used by |
|---------|---------|
| libsodium | keygen, server, client |
| sqlite3 | server |
| ncurses | client |

## Build

```bash
make           # build everything into build/
make server    # build keygen + server
make client    # build keygen + client
make clean     # remove binaries
```

Binaries are placed in `build/`:
- `build/keygen`        — key pair generator
- `build/diary-server`  — server
- `build/diary-client`  — client TUI

## Usage

### 1. Generate keys (once per user)

The `keygen` tool ships with the [protocol library](https://github.com/maagenta/authenticated-sealed-protocol)
(`protocol/keygen/`); `make` builds it into `build/` alongside the diary binaries.

```bash
cd ~/my-keys
/path/to/build/keygen
```

Generates:
- `auth.key` — authentication private key **(keep secret)**
- `auth.pub` — authentication public key
- `enc.key`  — encryption private key **(keep secret)**

### 2. Start the server

```bash
build/diary-server -k auth.pub -db diary.db
build/diary-server -p 8080 -k auth.pub -db /path/to/diary.db
```

**Server options:**

| Option | Description | Default |
|--------|-------------|---------|
| `-p PORT` | Port | `4242` |
| `-k FILE` | Authorized authentication public key | required |
| `-db FILE` | Path to SQLite database | required |

### 3. Run with Docker (optional)

Build the image:

```bash
docker build -t diary-server .
```

Run the container, mounting a directory that contains `auth.pub` and optionally an existing `diary.db`:

```bash
docker run -d \
  -v /path/to/your/keys:/data \
  -p 4242:4242 \
  --name diary \
  diary-server
```

The container expects:
- `/data/auth.pub` — authentication public key (required)
- `/data/diary.db` — SQLite database (created automatically if absent)

Logs:

```bash
docker logs -f diary
```

Stop and remove:

```bash
docker stop diary && docker rm diary
```

### 4. Connect the client

```bash
build/diary-client -a auth.key -e enc.key
build/diary-client -h 192.168.1.10 -p 8080 -a auth.key -e enc.key
```

**Client options:**

| Option | Description | Default |
|--------|-------------|---------|
| `-h HOST` | Server address | `127.0.0.1` |
| `-p PORT` | Port | `4242` |
| `-a AUTH_SK` | Path to auth private key | `auth.key` |
| `-e ENC_SK` | Path to encryption private key | `enc.key` |

### 4. Client key bindings

| Key | Action |
|-----|--------|
| `N` | New entry |
| `Enter` | Read selected entry |
| `E` | Edit selected entry |
| `D` | Delete selected entry |
| `↑` / `↓` | Navigate list |
| `R` | Reload entries |
| `Q` | Quit |
| `Ctrl+S` / `F2` | Save entry (in editor) |
| `ESC` | Cancel / exit |

## Project structure

```
diary/
├── protocol/               — authenticated-sealed-protocol library (git submodule)
│   ├── wire.c/h            — line framing
│   ├── crypto.c/h          — key loading, sealed boxes, challenge sign/verify
│   ├── auth.c/h            — handshake, client session
│   ├── serve.c/h           — server accept/fork harness
│   └── keygen/             — key pair generator tool
├── common/
│   ├── diary.h             — app constants (port, entry size limit)
│   └── version.h           — release version
├── server/
│   ├── main.c              — entry point, CLI options
│   ├── client_handler.c    — diary command dispatch (post-auth)
│   ├── storage.c/h         — SQLite persistence (WAL mode)
│   └── Makefile
├── client/
│   ├── main.c              — entry point, CLI options
│   ├── net.c/h             — diary commands over the protocol
│   ├── ui.c/h              — ncurses interface
│   └── Makefile
├── Makefile
└── README.md
```

## Docker

To compose the docker container use:

```
docker compose up -d
```

By default, the container listen on port **4242**, stars automatically **unless-stopped** and reads everything from a mounted `/data` volume:

- `/data/auth.pub` authorized authentication public key (required)
- `/data/diary.db` SQLite database (created automatically if absent)

To change the *data* location, edit the line `10` of `docker-compose.yml`.
