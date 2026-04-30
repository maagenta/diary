#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int write_key_file(const char *path,
                            const unsigned char *key, size_t key_len) {
    char b64[512];
    sodium_bin2base64(b64, sizeof(b64), key, key_len,
                      sodium_base64_VARIANT_ORIGINAL);

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Error: no se pudo crear %s\n", path);
        return -1;
    }
    fprintf(f, "%s\n", b64);
    fclose(f);
    chmod(path, 0600);
    printf("  Creado: %s\n", path);
    return 0;
}

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Error: no se pudo inicializar libsodium\n");
        return 1;
    }

    printf("=== Generador de claves para diary ===\n\n");

    /* --- Par de claves de autenticacion (Ed25519) --- */
    printf("Generando par de claves de autenticacion (Ed25519)...\n");
    unsigned char auth_pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char auth_sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(auth_pk, auth_sk);

    if (write_key_file("auth.key", auth_sk, crypto_sign_SECRETKEYBYTES) != 0)
        return 1;
    if (write_key_file("auth.pub", auth_pk, crypto_sign_PUBLICKEYBYTES) != 0)
        return 1;

    /* --- Par de claves de cifrado (X25519) --- */
    printf("\nGenerando par de claves de cifrado (X25519)...\n");
    unsigned char enc_pk[crypto_box_PUBLICKEYBYTES];
    unsigned char enc_sk[crypto_box_SECRETKEYBYTES];
    crypto_box_keypair(enc_pk, enc_sk);

    if (write_key_file("enc.key", enc_sk, crypto_box_SECRETKEYBYTES) != 0)
        return 1;
    if (write_key_file("enc.pub", enc_pk, crypto_box_PUBLICKEYBYTES) != 0)
        return 1;

    printf("\nClaves generadas exitosamente:\n");
    printf("  auth.key  — clave privada de autenticacion (MANTENER SECRETA)\n");
    printf("  auth.pub  — clave publica de autenticacion\n");
    printf("  enc.key   — clave privada de cifrado       (MANTENER SECRETA)\n");
    printf("  enc.pub   — clave publica de cifrado\n");
    printf("\nUsa el cliente con:\n");
    printf("  diary-client -a auth.key -e enc.key\n");
    return 0;
}
