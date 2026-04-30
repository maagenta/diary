#include "net.h"
#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sodium.h>

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
        if (read(fd, &c, 1) <= 0) return -1;
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (int)i;
}

/* Envia payload grande: <prefix><b64>\n sin buffer intermedio */
static int send_encrypted(int fd, const char *prefix,
                            const char *b64, size_t b64_len) {
    size_t plen = strlen(prefix);
    return (write(fd, prefix, plen)  == (ssize_t)plen  &&
            write(fd, b64, b64_len)  == (ssize_t)b64_len &&
            write(fd, "\n", 1)       == 1) ? 0 : -1;
}

/* Cifra text y lo deja en *b64_out (malloc). Devuelve longitud o -1. */
static ssize_t encrypt_text(const char *text, size_t tlen,
                              const unsigned char *enc_pk, char **b64_out) {
    size_t seal_len = tlen + crypto_box_SEALBYTES;
    size_t b64_sz   = sodium_base64_ENCODED_LEN(seal_len,
                          sodium_base64_VARIANT_ORIGINAL) + 1;
    char *b64 = malloc(b64_sz);
    if (!b64) return -1;
    if (crypto_seal((const unsigned char *)text, tlen, enc_pk, b64, b64_sz) != 0) {
        free(b64); return -1;
    }
    *b64_out = b64;
    return (ssize_t)strlen(b64);
}

/* ------------------------------------------------------------------ */
/* Conexion y autenticacion                                             */
/* ------------------------------------------------------------------ */

int net_connect(diary_conn_t *conn, const char *host, int port) {
    if (sodium_init() < 0) return -1;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0) return -1;

    conn->fd = socket(res->ai_family, res->ai_socktype, 0);
    if (conn->fd < 0) { freeaddrinfo(res); return -1; }
    if (connect(conn->fd, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res); close(conn->fd); conn->fd = -1; return -1;
    }
    freeaddrinfo(res);

    sodium_bin2hex(conn->user_hex, sizeof(conn->user_hex),
                   conn->keys.auth_pk, AUTH_PK_LEN);

    char line[MAX_LINE];
    if (send_line(conn->fd, "HELLO") < 0) goto fail;
    if (recv_line(conn->fd, line, sizeof(line)) < 0) goto fail;
    if (strncmp(line, "CHALLENGE ", 10) != 0) goto fail;

    unsigned char challenge[CHALLENGE_LEN];
    size_t ch_len = 0;
    if (sodium_base642bin(challenge, sizeof(challenge),
                           line + 10, strlen(line + 10),
                           NULL, &ch_len, NULL,
                           sodium_base64_VARIANT_ORIGINAL) != 0
        || ch_len != CHALLENGE_LEN) goto fail;

    unsigned char sig[AUTH_SIG_LEN];
    if (crypto_sign_challenge(challenge, ch_len, conn->keys.auth_sk, sig) != 0) goto fail;

    char pk_b64[sodium_base64_ENCODED_LEN(AUTH_PK_LEN, sodium_base64_VARIANT_ORIGINAL) + 1];
    char sig_b64[sodium_base64_ENCODED_LEN(AUTH_SIG_LEN, sodium_base64_VARIANT_ORIGINAL) + 1];
    sodium_bin2base64(pk_b64,  sizeof(pk_b64),  conn->keys.auth_pk, AUTH_PK_LEN,  sodium_base64_VARIANT_ORIGINAL);
    sodium_bin2base64(sig_b64, sizeof(sig_b64), sig,                AUTH_SIG_LEN, sodium_base64_VARIANT_ORIGINAL);

    char auth_cmd[MAX_LINE];
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s %s", pk_b64, sig_b64);
    if (send_line(conn->fd, auth_cmd) < 0) goto fail;
    if (recv_line(conn->fd, line, sizeof(line)) < 0) goto fail;

    if (strcmp(line, "REGISTER") == 0) {
        char epk_b64[sodium_base64_ENCODED_LEN(ENC_PK_LEN, sodium_base64_VARIANT_ORIGINAL) + 1];
        sodium_bin2base64(epk_b64, sizeof(epk_b64), conn->keys.enc_pk, ENC_PK_LEN, sodium_base64_VARIANT_ORIGINAL);
        char reg[MAX_LINE];
        snprintf(reg, sizeof(reg), "REGISTER %s", epk_b64);
        if (send_line(conn->fd, reg) < 0) goto fail;
        if (recv_line(conn->fd, line, sizeof(line)) < 0) goto fail;
        if (strcmp(line, "OK") != 0) goto fail;
    } else if (strcmp(line, "OK") != 0) {
        goto fail;
    }
    return 0;

fail:
    close(conn->fd); conn->fd = -1; return -1;
}

void net_disconnect(diary_conn_t *conn) {
    if (conn->fd >= 0) {
        send_line(conn->fd, "QUIT");
        char line[64]; recv_line(conn->fd, line, sizeof(line));
        close(conn->fd); conn->fd = -1;
    }
}

/* ------------------------------------------------------------------ */
/* Operaciones                                                          */
/* ------------------------------------------------------------------ */

/* POST: devuelve id asignado (>= 1) o -1 */
int net_post_entry(diary_conn_t *conn, const char *text) {
    char *b64 = NULL;
    ssize_t b64_len = encrypt_text(text, strlen(text), conn->keys.enc_pk, &b64);
    if (b64_len < 0) return -1;
    int rc = send_encrypted(conn->fd, "POST ", b64, (size_t)b64_len);
    free(b64);
    if (rc != 0) return -1;

    char line[64];
    if (recv_line(conn->fd, line, sizeof(line)) < 0) return -1;
    /* Respuesta: "OK <id>" */
    if (strncmp(line, "OK ", 3) == 0) return atoi(line + 3);
    return -1;
}

/* UPDATE: devuelve id en exito, -1 en error */
int net_update_entry(diary_conn_t *conn, int id, const char *text) {
    char *b64 = NULL;
    ssize_t b64_len = encrypt_text(text, strlen(text), conn->keys.enc_pk, &b64);
    if (b64_len < 0) return -1;

    char prefix[32];
    snprintf(prefix, sizeof(prefix), "UPDATE %d ", id);
    int rc = send_encrypted(conn->fd, prefix, b64, (size_t)b64_len);
    free(b64);
    if (rc != 0) return -1;

    char line[64];
    if (recv_line(conn->fd, line, sizeof(line)) < 0) return -1;
    return (strcmp(line, "OK") == 0) ? id : -1;
}

/* DELETE: devuelve 0 ok, -1 error */
int net_delete_entry(diary_conn_t *conn, int id) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "DELETE %d", id);
    if (send_line(conn->fd, cmd) < 0) return -1;
    char line[64];
    if (recv_line(conn->fd, line, sizeof(line)) < 0) return -1;
    return (strcmp(line, "OK") == 0) ? 0 : -1;
}

/* GET: descarga y descifra */
int net_get_entries(diary_conn_t *conn,
                     diary_entry_t **entries, int *count) {
    if (send_line(conn->fd, "GET") < 0) return -1;

    char line[MAX_LINE];
    if (recv_line(conn->fd, line, sizeof(line)) < 0) return -1;
    if (strncmp(line, "ENTRIES ", 8) != 0) return -1;

    int n = atoi(line + 8);
    if (n <= 0) { *entries = NULL; *count = 0; return 0; }

    diary_entry_t *arr = calloc(n, sizeof(diary_entry_t));
    if (!arr) return -1;

    for (int i = 0; i < n; i++) {
        if (recv_line(conn->fd, line, sizeof(line)) < 0) goto fail;
        char *sv = NULL, *tmp = strdup(line);
        char *sid  = strtok_r(tmp, " ", &sv);
        char *sts  = strtok_r(NULL, " ", &sv);
        char *sdat = strtok_r(NULL, " ", &sv);
        if (!sid || !sts || !sdat) { free(tmp); goto fail; }

        arr[i].id        = atoi(sid);
        arr[i].timestamp = atol(sts);

        unsigned char plain[MAX_ENTRY_LEN];
        int plen = crypto_unseal(sdat, conn->keys.enc_pk, conn->keys.enc_sk,
                                  plain, sizeof(plain) - 1);
        free(tmp);
        plain[plen > 0 ? plen : 0] = '\0';
        arr[i].text = strdup(plen > 0 ? (char *)plain : "[no se pudo descifrar]");
    }

    *entries = arr; *count = n;
    return 0;

fail:
    for (int i = 0; i < n; i++) free(arr[i].text);
    free(arr); return -1;
}
