#include "client_handler.h"
#include "storage.h"
#include "../common/protocol.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* I/O                                                                  */
/* ------------------------------------------------------------------ */

static int send_line(int fd, const char *line) {
    size_t len = strlen(line);
    if (write(fd, line, len) != (ssize_t)len) return -1;
    if (write(fd, "\n", 1) != 1) return -1;
    return 0;
}

static int recv_line(int fd, char *buf, size_t bufsz) {
    size_t i = 0;
    while (i < bufsz - 1) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) return -1;
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (int)i;
}

/* Read a line of arbitrary size (heap-allocated). Caller must free. */
static char *recv_line_dyn(int fd) {
    size_t sz = 4096, i = 0;
    char *buf = malloc(sz);
    if (!buf) return NULL;
    while (1) {
        char c;
        if (read(fd, &c, 1) <= 0) { free(buf); return NULL; }
        if (c == '\n') break;
        if (c == '\r') continue;
        if (i + 1 >= sz) {
            sz *= 2;
            char *nb = realloc(buf, sz);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    return buf;
}

/* ------------------------------------------------------------------ */
/* Authentication                                                       */
/* ------------------------------------------------------------------ */

static char *do_auth(int fd, const char *allowed_hex) {
    char line[MAX_LINE];

    if (recv_line(fd, line, sizeof(line)) < 0) return NULL;
    if (strcmp(line, "HELLO") != 0) {
        send_line(fd, "FAIL expected HELLO"); return NULL;
    }

    unsigned char challenge[CHALLENGE_LEN];
    randombytes_buf(challenge, sizeof(challenge));

    char ch_b64[sodium_base64_ENCODED_LEN(CHALLENGE_LEN,
                    sodium_base64_VARIANT_ORIGINAL) + 1];
    sodium_bin2base64(ch_b64, sizeof(ch_b64), challenge, CHALLENGE_LEN,
                      sodium_base64_VARIANT_ORIGINAL);

    char resp[MAX_LINE];
    snprintf(resp, sizeof(resp), "CHALLENGE %s", ch_b64);
    if (send_line(fd, resp) < 0) return NULL;

    if (recv_line(fd, line, sizeof(line)) < 0) return NULL;
    if (strncmp(line, "AUTH ", 5) != 0) {
        send_line(fd, "FAIL expected AUTH"); return NULL;
    }

    char *sv = NULL, *tmp = strdup(line + 5);
    char *pk_b64  = strtok_r(tmp, " ", &sv);
    char *sig_b64 = strtok_r(NULL, " ", &sv);
    if (!pk_b64 || !sig_b64) {
        free(tmp); send_line(fd, "FAIL invalid AUTH format"); return NULL;
    }

    unsigned char pk[AUTH_PK_LEN];
    size_t pk_len = 0;
    if (sodium_base642bin(pk, sizeof(pk), pk_b64, strlen(pk_b64),
                           NULL, &pk_len, NULL,
                           sodium_base64_VARIANT_ORIGINAL) != 0
        || pk_len != AUTH_PK_LEN) {
        free(tmp); send_line(fd, "FAIL invalid public key"); return NULL;
    }

    unsigned char sig[AUTH_SIG_LEN];
    size_t sig_len = 0;
    if (sodium_base642bin(sig, sizeof(sig), sig_b64, strlen(sig_b64),
                           NULL, &sig_len, NULL,
                           sodium_base64_VARIANT_ORIGINAL) != 0
        || sig_len != AUTH_SIG_LEN) {
        free(tmp); send_line(fd, "FAIL invalid signature"); return NULL;
    }
    free(tmp);

    if (crypto_sign_verify_detached(sig, challenge, CHALLENGE_LEN, pk) != 0) {
        send_line(fd, "FAIL signature verification failed"); return NULL;
    }

    char *hex = malloc(AUTH_PK_LEN * 2 + 1);
    if (!hex) { send_line(fd, "FAIL internal error"); return NULL; }
    sodium_bin2hex(hex, AUTH_PK_LEN * 2 + 1, pk, AUTH_PK_LEN);

    if (strcmp(hex, allowed_hex) != 0) {
        send_line(fd, "FAIL access denied");
        free(hex); return NULL;
    }
    return hex;
}

/* ------------------------------------------------------------------ */
/* GET: entry accumulator                                               */
/* ------------------------------------------------------------------ */

#define MAX_ENTRIES 1024

typedef struct { int count; char **lines; } entry_collect_t;

static void collect_entry(int id, time_t ts, const char *data, void *ud) {
    entry_collect_t *c = (entry_collect_t *)ud;
    if (c->count >= MAX_ENTRIES) return;
    size_t len = 10 + 1 + 20 + 1 + strlen(data) + 1;
    char *line = malloc(len);
    if (!line) return;
    snprintf(line, len, "%d %ld %s", id, (long)ts, data);
    c->lines[c->count++] = line;
}

/* ------------------------------------------------------------------ */
/* Main loop                                                            */
/* ------------------------------------------------------------------ */

void handle_client(int fd, const char *db_path, const char *allowed_hex) {
    if (sodium_init() < 0) { send_line(fd, "FAIL crypto"); return; }
    if (storage_init(db_path) != 0) { send_line(fd, "FAIL db");     return; }

    char *user_hex = do_auth(fd, allowed_hex);
    if (!user_hex) { storage_close(); return; }

    if (!storage_user_exists(user_hex)) {
        send_line(fd, "REGISTER");

        char line[MAX_LINE];
        if (recv_line(fd, line, sizeof(line)) < 0) goto done;
        if (strncmp(line, "REGISTER ", 9) != 0) {
            send_line(fd, "FAIL expected REGISTER <enc_pubkey_b64>"); goto done;
        }

        const char *epk_b64 = line + 9;
        unsigned char epk[ENC_PK_LEN];
        size_t epk_len = 0;
        if (sodium_base642bin(epk, sizeof(epk), epk_b64, strlen(epk_b64),
                               NULL, &epk_len, NULL,
                               sodium_base64_VARIANT_ORIGINAL) != 0
            || epk_len != ENC_PK_LEN) {
            send_line(fd, "FAIL invalid encryption key"); goto done;
        }
        if (storage_register_user(user_hex, epk_b64) != 0) {
            send_line(fd, "FAIL could not register user"); goto done;
        }
        send_line(fd, "OK");
    } else {
        send_line(fd, "OK");
    }

    char *line;
    while ((line = recv_line_dyn(fd)) != NULL) {
        if (strcmp(line, "QUIT") == 0) {
            send_line(fd, "BYE"); free(line); break;

        } else if (strncmp(line, "POST ", 5) == 0) {
            int id = storage_add_entry(user_hex, line + 5, time(NULL));
            if (id < 0) {
                send_line(fd, "FAIL could not save entry");
            } else {
                char ok[32];
                snprintf(ok, sizeof(ok), "OK %d", id);
                send_line(fd, ok);
            }

        } else if (strcmp(line, "GET") == 0) {
            entry_collect_t col = { 0, calloc(MAX_ENTRIES, sizeof(char *)) };
            if (!col.lines) { send_line(fd, "FAIL out of memory"); free(line); continue; }
            storage_get_entries(user_hex, collect_entry, &col);
            char hdr[32];
            snprintf(hdr, sizeof(hdr), "ENTRIES %d", col.count);
            send_line(fd, hdr);
            for (int i = 0; i < col.count; i++) {
                send_line(fd, col.lines[i]); free(col.lines[i]);
            }
            free(col.lines);

        } else if (strncmp(line, "UPDATE ", 7) == 0) {
            char *sp = strchr(line + 7, ' ');
            if (!sp) {
                send_line(fd, "FAIL invalid UPDATE format");
            } else {
                *sp = '\0';
                int uid = atoi(line + 7);
                if (storage_update_entry(user_hex, uid, sp + 1) == 0)
                    send_line(fd, "OK");
                else
                    send_line(fd, "FAIL entry not found");
            }

        } else if (strncmp(line, "DELETE ", 7) == 0) {
            int del_id = atoi(line + 7);
            if (storage_delete_entry(user_hex, del_id) == 0)
                send_line(fd, "OK");
            else
                send_line(fd, "FAIL entry not found");

        } else {
            send_line(fd, "FAIL unknown command");
        }
        free(line);
    }

done:
    free(user_hex);
    storage_close();
}
