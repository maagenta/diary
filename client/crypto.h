#ifndef CRYPTO_H
#define CRYPTO_H

#include <sodium.h>
#include "../common/protocol.h"

typedef struct {
    unsigned char auth_pk[AUTH_PK_LEN];
    unsigned char auth_sk[AUTH_SK_LEN];
    unsigned char enc_pk[ENC_PK_LEN];
    unsigned char enc_sk[ENC_SK_LEN];
} diary_keys_t;

/*
 * Carga claves desde archivos.
 *   auth_sk_path  — archivo con clave privada Ed25519 (base64)
 *   enc_sk_path   — archivo con clave privada X25519  (base64)
 * Devuelve 0 ok, -1 error.
 */
int crypto_load_keys(const char *auth_sk_path,
                      const char *enc_sk_path,
                      diary_keys_t *out);

/*
 * Cifra un mensaje con crypto_box_seal (solo necesita la clave publica).
 * out_b64 debe tener al menos sodium_base64_ENCODED_LEN(
 *   msg_len + crypto_box_SEALBYTES, sodium_base64_VARIANT_ORIGINAL) bytes.
 * Devuelve 0 ok, -1 error.
 */
int crypto_seal(const unsigned char *msg, size_t msg_len,
                 const unsigned char *enc_pk,
                 char *out_b64, size_t out_b64_sz);

/*
 * Descifra un mensaje cifrado con crypto_box_seal.
 * out debe tener al menos (cipher_len - crypto_box_SEALBYTES) bytes.
 * Devuelve longitud del mensaje o -1 en error.
 */
int crypto_unseal(const char *data_b64,
                   const unsigned char *enc_pk,
                   const unsigned char *enc_sk,
                   unsigned char *out, size_t out_sz);

/*
 * Firma un challenge con la clave privada de autenticacion.
 * sig debe tener al menos AUTH_SIG_LEN bytes.
 */
int crypto_sign_challenge(const unsigned char *challenge, size_t clen,
                            const unsigned char *auth_sk,
                            unsigned char *sig);

#endif /* CRYPTO_H */
