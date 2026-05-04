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
        fprintf(stderr, "Error: could not create %s\n", path);
        return -1;
    }
    fprintf(f, "%s\n", b64);
    fclose(f);
    chmod(path, 0600);
    printf("  Created: %s\n", path);
    return 0;
}

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Error: could not initialize libsodium\n");
        return 1;
    }

    printf("=== Diary key generator ===\n\n");

    /* --- Authentication key pair (Ed25519) --- */
    printf("Generating authentication key pair (Ed25519)...\n");
    unsigned char auth_pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char auth_sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(auth_pk, auth_sk);

    if (write_key_file("auth.key", auth_sk, crypto_sign_SECRETKEYBYTES) != 0)
        return 1;
    if (write_key_file("auth.pub", auth_pk, crypto_sign_PUBLICKEYBYTES) != 0)
        return 1;

    /* --- Encryption key pair (X25519) --- */
    printf("\nGenerating encryption key pair (X25519)...\n");
    unsigned char enc_sk[crypto_box_SECRETKEYBYTES];
    randombytes_buf(enc_sk, sizeof(enc_sk));

    if (write_key_file("enc.key", enc_sk, crypto_box_SECRETKEYBYTES) != 0)
        return 1;

    printf("\nKeys generated successfully:\n");
    printf("  auth.key  — authentication private key (KEEP SECRET)\n");
    printf("  auth.pub  — authentication public key\n");
    printf("  enc.key   — encryption private key     (KEEP SECRET)\n");
    printf("\nRun the client with:\n");
    printf("  diary-client -a auth.key -e enc.key\n");
    return 0;
}
