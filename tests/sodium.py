"""ctypes bindings to the same libsodium the diary binaries link.

Only the five operations the protocol needs. No pip dependencies: the
library is loaded from the system, so the crypto used by the tests is
bit-for-bit the crypto used by the product.
"""
import ctypes
import ctypes.util

_path = ctypes.util.find_library("sodium")
if not _path:
    raise OSError("libsodium not found (it is required to build the project)")
_lib = ctypes.CDLL(_path)
if _lib.sodium_init() < 0:
    raise OSError("sodium_init failed")

for _fn in ("crypto_box_sealbytes", "crypto_sign_bytes",
            "crypto_sign_publickeybytes", "crypto_sign_secretkeybytes",
            "crypto_box_publickeybytes", "crypto_box_secretkeybytes"):
    getattr(_lib, _fn).restype = ctypes.c_size_t

SEAL_BYTES = _lib.crypto_box_sealbytes()
SIG_BYTES = _lib.crypto_sign_bytes()
AUTH_PK_LEN = _lib.crypto_sign_publickeybytes()
AUTH_SK_LEN = _lib.crypto_sign_secretkeybytes()
ENC_PK_LEN = _lib.crypto_box_publickeybytes()
ENC_SK_LEN = _lib.crypto_box_secretkeybytes()


def sign_detached(msg: bytes, auth_sk: bytes) -> bytes:
    sig = ctypes.create_string_buffer(SIG_BYTES)
    rc = _lib.crypto_sign_detached(sig, None, msg,
                                   ctypes.c_ulonglong(len(msg)), auth_sk)
    if rc != 0:
        raise RuntimeError("crypto_sign_detached failed")
    return sig.raw


def sign_keypair() -> tuple[bytes, bytes]:
    """Fresh Ed25519 (pk, sk) — for impostor tests."""
    pk = ctypes.create_string_buffer(AUTH_PK_LEN)
    sk = ctypes.create_string_buffer(AUTH_SK_LEN)
    _lib.crypto_sign_keypair(pk, sk)
    return pk.raw, sk.raw


def box_keypair() -> tuple[bytes, bytes]:
    """Fresh X25519 (pk, sk) — for impostor tests."""
    pk = ctypes.create_string_buffer(ENC_PK_LEN)
    sk = ctypes.create_string_buffer(ENC_SK_LEN)
    _lib.crypto_box_keypair(pk, sk)
    return pk.raw, sk.raw


def scalarmult_base(enc_sk: bytes) -> bytes:
    """X25519 public key from a private key (as proto_load_keys does)."""
    pk = ctypes.create_string_buffer(ENC_PK_LEN)
    _lib.crypto_scalarmult_base(pk, enc_sk)
    return pk.raw


def seal(msg: bytes, enc_pk: bytes) -> bytes:
    cipher = ctypes.create_string_buffer(len(msg) + SEAL_BYTES)
    rc = _lib.crypto_box_seal(cipher, msg,
                              ctypes.c_ulonglong(len(msg)), enc_pk)
    if rc != 0:
        raise RuntimeError("crypto_box_seal failed")
    return cipher.raw


def seal_open(cipher: bytes, enc_pk: bytes, enc_sk: bytes) -> bytes | None:
    if len(cipher) < SEAL_BYTES:
        return None
    plain = ctypes.create_string_buffer(max(len(cipher) - SEAL_BYTES, 1))
    rc = _lib.crypto_box_seal_open(plain, cipher,
                                   ctypes.c_ulonglong(len(cipher)),
                                   enc_pk, enc_sk)
    if rc != 0:
        return None
    return plain.raw[:len(cipher) - SEAL_BYTES]
