cmake_minimum_required(VERSION 3.22.1)
project(diary_crypto C)

set(SODIUM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/libsodium)
set(SODIUM_SRC  ${SODIUM_ROOT}/src/libsodium)

add_library(diary_crypto SHARED
    diary_crypto.c

    # sodium core
    ${SODIUM_SRC}/sodium/core.c
    ${SODIUM_SRC}/sodium/utils.c
    ${SODIUM_SRC}/sodium/codecs.c
    ${SODIUM_SRC}/sodium/runtime.c
    ${SODIUM_SRC}/sodium/version.c

    # randombytes
    ${SODIUM_SRC}/randombytes/randombytes.c
    ${SODIUM_SRC}/randombytes/sysrandom/randombytes_sysrandom.c

    # crypto_verify
    ${SODIUM_SRC}/crypto_verify/verify.c

    # crypto_hash/sha512 (needed by Ed25519)
    ${SODIUM_SRC}/crypto_hash/sha512/hash_sha512.c
    ${SODIUM_SRC}/crypto_hash/sha512/cp/hash_sha512_cp.c

    # crypto_hash/sha256 (needed by core_h2c)
    ${SODIUM_SRC}/crypto_hash/sha256/hash_sha256.c
    ${SODIUM_SRC}/crypto_hash/sha256/cp/hash_sha256_cp.c

    # crypto_core/ed25519 (shared by sign and scalarmult)
    ${SODIUM_SRC}/crypto_core/ed25519/core_ed25519.c
    ${SODIUM_SRC}/crypto_core/ed25519/core_h2c.c
    ${SODIUM_SRC}/crypto_core/ed25519/ref10/ed25519_ref10.c

    # crypto_core/hsalsa20 (needed by crypto_box)
    ${SODIUM_SRC}/crypto_core/hsalsa20/core_hsalsa20.c
    ${SODIUM_SRC}/crypto_core/hsalsa20/ref2/core_hsalsa20_ref2.c

    # crypto_onetimeauth/poly1305
    ${SODIUM_SRC}/crypto_onetimeauth/crypto_onetimeauth.c
    ${SODIUM_SRC}/crypto_onetimeauth/poly1305/onetimeauth_poly1305.c
    ${SODIUM_SRC}/crypto_onetimeauth/poly1305/donna/poly1305_donna.c

    # crypto_core/salsa20 (needed by salsa20_ref)
    ${SODIUM_SRC}/crypto_core/salsa/ref/core_salsa_ref.c

    # crypto_stream/salsa20 + xsalsa20
    ${SODIUM_SRC}/crypto_stream/salsa20/stream_salsa20.c
    ${SODIUM_SRC}/crypto_stream/salsa20/ref/salsa20_ref.c
    ${SODIUM_SRC}/crypto_stream/xsalsa20/stream_xsalsa20.c

    # crypto_secretbox (XSalsa20-Poly1305)
    ${SODIUM_SRC}/crypto_secretbox/crypto_secretbox.c
    ${SODIUM_SRC}/crypto_secretbox/crypto_secretbox_easy.c
    ${SODIUM_SRC}/crypto_secretbox/xsalsa20poly1305/secretbox_xsalsa20poly1305.c

    # crypto_scalarmult/curve25519 (X25519)
    ${SODIUM_SRC}/crypto_scalarmult/crypto_scalarmult.c
    ${SODIUM_SRC}/crypto_scalarmult/curve25519/scalarmult_curve25519.c
    ${SODIUM_SRC}/crypto_scalarmult/curve25519/ref10/x25519_ref10.c

    # crypto_box_seal (X25519 + XSalsa20-Poly1305)
    ${SODIUM_SRC}/crypto_box/crypto_box.c
    ${SODIUM_SRC}/crypto_box/crypto_box_easy.c
    ${SODIUM_SRC}/crypto_box/crypto_box_seal.c
    ${SODIUM_SRC}/crypto_box/curve25519xsalsa20poly1305/box_curve25519xsalsa20poly1305.c

    # crypto_sign/ed25519
    ${SODIUM_SRC}/crypto_sign/crypto_sign.c
    ${SODIUM_SRC}/crypto_sign/ed25519/sign_ed25519.c
    ${SODIUM_SRC}/crypto_sign/ed25519/ref10/keypair.c
    ${SODIUM_SRC}/crypto_sign/ed25519/ref10/open.c
    ${SODIUM_SRC}/crypto_sign/ed25519/ref10/sign.c

    # crypto_stream/chacha20 (required by sodium_init)
    ${SODIUM_SRC}/crypto_stream/chacha20/stream_chacha20.c
    ${SODIUM_SRC}/crypto_stream/chacha20/ref/chacha20_ref.c

    # crypto_generichash (needed by crypto_box_seal)
    ${SODIUM_SRC}/crypto_generichash/crypto_generichash.c

    # crypto_generichash/blake2b (required by sodium_init)
    ${SODIUM_SRC}/crypto_generichash/blake2b/generichash_blake2.c
    ${SODIUM_SRC}/crypto_generichash/blake2b/ref/generichash_blake2b.c
    ${SODIUM_SRC}/crypto_generichash/blake2b/ref/blake2b-ref.c
    ${SODIUM_SRC}/crypto_generichash/blake2b/ref/blake2b-compress-ref.c

    # crypto_aead/aegis (required by sodium_init)
    ${SODIUM_SRC}/crypto_aead/aegis128l/aead_aegis128l.c
    ${SODIUM_SRC}/crypto_aead/aegis128l/aegis128l_soft.c
    ${SODIUM_SRC}/crypto_aead/aegis256/aead_aegis256.c
    ${SODIUM_SRC}/crypto_aead/aegis256/aegis256_soft.c

    # crypto_core/softaes (needed by aegis soft implementations)
    ${SODIUM_SRC}/crypto_core/softaes/softaes.c

    # crypto_ipcrypt (required by sodium_init)
    ${SODIUM_SRC}/crypto_ipcrypt/crypto_ipcrypt.c
    ${SODIUM_SRC}/crypto_ipcrypt/ipcrypt_soft.c

    # crypto_pwhash/argon2 (required by sodium_init)
    ${SODIUM_SRC}/crypto_pwhash/argon2/argon2.c
    ${SODIUM_SRC}/crypto_pwhash/argon2/argon2-core.c
    ${SODIUM_SRC}/crypto_pwhash/argon2/argon2-encoding.c
    ${SODIUM_SRC}/crypto_pwhash/argon2/argon2-fill-block-ref.c
    ${SODIUM_SRC}/crypto_pwhash/argon2/argon2-fill-block-neon.c
    ${SODIUM_SRC}/crypto_pwhash/argon2/blake2b-long.c
    ${SODIUM_SRC}/crypto_pwhash/argon2/pwhash_argon2i.c
    ${SODIUM_SRC}/crypto_pwhash/argon2/pwhash_argon2id.c
    ${SODIUM_SRC}/crypto_pwhash/crypto_pwhash.c
)

target_include_directories(diary_crypto PRIVATE
    ${SODIUM_SRC}/include
    ${SODIUM_SRC}/include/sodium
)

target_compile_definitions(diary_crypto PRIVATE
    SODIUM_STATIC=1
    CONFIGURED=1
    DEV_MODE=0
)

find_library(log-lib log)
target_link_libraries(diary_crypto ${log-lib})
