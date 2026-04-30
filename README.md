# Diary

Aplicación de diario personal con cifrado end-to-end, accesible a través de red. Arquitectura cliente-servidor en C.

## Arquitectura

- **Servidor**: TCP socket en C. Almacena entradas en una base de datos SQLite, cifradas con la clave pública del usuario. Nunca tiene acceso a las claves privadas.
- **Cliente**: TUI en ncurses. Lee la clave privada de un archivo local para descifrar las entradas.

## Cifrado

| Propósito | Algoritmo | Archivos |
|-----------|-----------|---------|
| Autenticación | Ed25519 (firma de challenge) | `auth.key`, `auth.pub` |
| Cifrado de entradas | X25519 + XSalsa20-Poly1305 (`crypto_box_seal`) | `enc.key`, `enc.pub` |

El servidor almacena las entradas cifradas con la clave pública de cifrado del usuario. Solo el cliente con la clave privada puede descifrarlas.

## Protocolo

```
C→S: HELLO
S→C: CHALLENGE <base64>
C→S: AUTH <auth_pubkey_b64> <firma_b64>
S→C: OK  |  REGISTER

# Si es usuario nuevo (REGISTER):
C→S: REGISTER <enc_pubkey_b64>
S→C: OK

# Nueva entrada:
C→S: POST <entrada_cifrada_b64>
S→C: OK <id>  |  FAIL

# Obtener entradas:
C→S: GET
S→C: ENTRIES <n>
     <id> <timestamp> <entrada_cifrada_b64>  (× n)

# Editar entrada:
C→S: UPDATE <id> <entrada_cifrada_b64>
S→C: OK  |  FAIL

# Eliminar entrada:
C→S: DELETE <id>
S→C: OK  |  FAIL

C→S: QUIT
S→C: BYE
```

Puerto por defecto: **4242**

## Dependencias

```bash
# macOS
brew install libsodium sqlite ncurses
```

| Librería | Usada por |
|----------|-----------|
| libsodium | keygen, servidor, cliente |
| sqlite3 | servidor |
| ncurses | cliente |

## Compilar

```bash
make           # compila todo en build/
make server    # compila keygen + servidor
make client    # compila keygen + cliente
make clean     # elimina binarios
```

Los binarios quedan en `build/`:
- `build/keygen`        — generador de claves
- `build/diary-server`  — servidor
- `build/diary-client`  — cliente TUI

## Uso

### 1. Generar claves (una vez por usuario)

```bash
cd ~/mis-claves
/ruta/a/build/keygen
```

Genera:
- `auth.key` — clave privada de autenticación **(mantener secreta)**
- `auth.pub` — clave pública de autenticación
- `enc.key`  — clave privada de cifrado **(mantener secreta)**
- `enc.pub`  — clave pública de cifrado

### 2. Iniciar el servidor

```bash
build/diary-server -k auth.pub -db diary.db
build/diary-server -p 8080 -k auth.pub -db /ruta/a/diary.db
```

**Opciones del servidor:**

| Opción | Descripción | Default |
|--------|-------------|---------|
| `-p PORT` | Puerto | `4242` |
| `-k FILE` | Clave pública de autenticación autorizada | obligatorio |
| `-db FILE` | Ruta a la base de datos SQLite | obligatorio |

### 3. Conectar el cliente

```bash
build/diary-client -a auth.key -e enc.key
build/diary-client -h 192.168.1.10 -p 8080 -a auth.key -e enc.key
```

**Opciones del cliente:**

| Opción | Descripción | Default |
|--------|-------------|---------|
| `-h HOST` | Dirección del servidor | `127.0.0.1` |
| `-p PORT` | Puerto | `4242` |
| `-a AUTH_SK` | Ruta a clave privada de auth | `auth.key` |
| `-e ENC_SK` | Ruta a clave privada de cifrado | `enc.key` |

### 4. Teclas en el cliente

| Tecla | Acción |
|-------|--------|
| `N` | Nueva entrada |
| `Enter` | Leer entrada seleccionada |
| `E` | Editar entrada seleccionada |
| `D` | Eliminar entrada seleccionada |
| `↑` / `↓` | Navegar lista |
| `R` | Recargar entradas |
| `Q` | Salir |
| `Ctrl+S` / `F2` | Guardar entrada (en editor) |
| `ESC` | Cancelar / salir |

## Estructura del proyecto

```
diary/
├── common/
│   └── protocol.h          — constantes y protocolo compartido
├── server/
│   ├── main.c              — loop TCP, fork por cliente
│   ├── client_handler.c    — sesión: auth + comandos
│   ├── storage.c/h         — persistencia SQLite (WAL mode)
│   └── Makefile
├── client/
│   ├── main.c              — punto de entrada, opciones CLI
│   ├── crypto.c/h          — carga de claves, cifrado/descifrado
│   ├── net.c/h             — protocolo de red
│   ├── ui.c/h              — interfaz ncurses
│   └── Makefile
├── keygen/
│   ├── keygen.c            — generador de pares de claves
│   └── Makefile
├── Makefile
└── README.md
```
