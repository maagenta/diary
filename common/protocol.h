#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sodium.h>

#define DIARY_PORT        4242
#define CHALLENGE_LEN     32
#define MAX_LINE          16384
#define MAX_ENTRY_LEN     65536

/* libsodium key sizes */
#define AUTH_PK_LEN       crypto_sign_PUBLICKEYBYTES   /* 32 bytes Ed25519 */
#define AUTH_SK_LEN       crypto_sign_SECRETKEYBYTES   /* 64 bytes Ed25519 */
#define AUTH_SIG_LEN      crypto_sign_BYTES             /* 64 bytes */
#define ENC_PK_LEN        crypto_box_PUBLICKEYBYTES    /* 32 bytes X25519 */
#define ENC_SK_LEN        crypto_box_SECRETKEYBYTES    /* 32 bytes X25519 */

/*
 * Protocol (text lines, binary data in base64):
 *
 *   C->S: HELLO\n
 *   S->C: CHALLENGE <base64_32bytes>\n
 *   C->S: AUTH <auth_pubkey_b64> <signature_b64>\n
 *   S->C: OK\n  |  FAIL <reason>\n
 *
 *   (new user):
 *   C->S: REGISTER <encrypt_pubkey_b64>\n
 *   S->C: OK\n  |  FAIL <reason>\n
 *
 *   C->S: POST <encrypted_entry_b64>\n
 *   S->C: OK <id>\n  |  FAIL <reason>\n
 *
 *   C->S: GET\n
 *   S->C: ENTRIES <count>\n
 *          <id> <timestamp> <encrypted_entry_b64>\n  (x count)
 *
 *   C->S: UPDATE <id> <encrypted_entry_b64>\n
 *   S->C: OK\n  |  FAIL <reason>\n
 *
 *   C->S: DELETE <id>\n
 *   S->C: OK\n  |  FAIL <reason>\n
 *
 *   C->S: QUIT\n
 *   S->C: BYE\n
 */

#endif /* PROTOCOL_H */
