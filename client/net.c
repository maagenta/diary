#include "net.h"
#include "../protocol/wire.h"
#include "../protocol/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* POST: returns assigned id (>= 1) or -1.
 * timestamp > 0 sends "POST <epoch> <data>" so the server stores that
 * instant; timestamp <= 0 sends plain "POST <data>" (server clock). */
int net_post_entry(proto_conn_t *conn, const char *text, long timestamp) {
    char *b64 = proto_seal_new((const unsigned char *)text, strlen(text),
                               conn->keys.enc_pk);
    if (!b64) return -1;
    char prefix[32] = "POST ";
    if (timestamp > 0)
        snprintf(prefix, sizeof(prefix), "POST %ld ", timestamp);
    int rc = proto_send_prefixed(conn->fd, prefix, b64, strlen(b64));
    free(b64);
    if (rc != 0) return -1;

    char line[64];
    if (proto_recv_line(conn->fd, line, sizeof(line)) < 0) return -1;
    if (strncmp(line, "OK ", 3) == 0) return atoi(line + 3);
    return -1;
}

/* UPDATE: returns id on success, -1 on error */
int net_update_entry(proto_conn_t *conn, int id, const char *text) {
    char *b64 = proto_seal_new((const unsigned char *)text, strlen(text),
                               conn->keys.enc_pk);
    if (!b64) return -1;

    char prefix[32];
    snprintf(prefix, sizeof(prefix), "UPDATE %d ", id);
    int rc = proto_send_prefixed(conn->fd, prefix, b64, strlen(b64));
    free(b64);
    if (rc != 0) return -1;

    char line[64];
    if (proto_recv_line(conn->fd, line, sizeof(line)) < 0) return -1;
    return (strcmp(line, "OK") == 0) ? id : -1;
}

/* DELETE: returns 0 on success, -1 on error */
int net_delete_entry(proto_conn_t *conn, int id) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "DELETE %d", id);
    if (proto_send_line(conn->fd, cmd) < 0) return -1;
    char line[64];
    if (proto_recv_line(conn->fd, line, sizeof(line)) < 0) return -1;
    return (strcmp(line, "OK") == 0) ? 0 : -1;
}

/* Newest first; ties broken by highest id */
static int entry_cmp_newest_first(const void *a, const void *b) {
    const diary_entry_t *ea = a, *eb = b;
    if (ea->timestamp != eb->timestamp)
        return (ea->timestamp < eb->timestamp) ? 1 : -1;
    return eb->id - ea->id;
}

/* GET: download and decrypt entries */
int net_get_entries(proto_conn_t *conn,
                    diary_entry_t **entries, int *count) {
    if (proto_send_line(conn->fd, "GET") < 0) return -1;

    char hdr[64];
    if (proto_recv_line(conn->fd, hdr, sizeof(hdr)) < 0) return -1;
    if (strncmp(hdr, "ENTRIES ", 8) != 0) return -1;

    int n = atoi(hdr + 8);
    if (n <= 0) { *entries = NULL; *count = 0; return 0; }

    diary_entry_t *arr = calloc(n, sizeof(diary_entry_t));
    if (!arr) return -1;

    for (int i = 0; i < n; i++) {
        char *line = proto_recv_line_dyn(conn->fd);   /* dynamic: no size cap */
        if (!line) goto fail;

        char *sv = NULL;
        char *sid  = strtok_r(line, " ", &sv);
        char *sts  = strtok_r(NULL, " ", &sv);
        char *sdat = strtok_r(NULL, " ", &sv);
        if (!sid || !sts || !sdat) { free(line); goto fail; }

        arr[i].id        = atoi(sid);
        arr[i].timestamp = atol(sts);

        unsigned char *plain = proto_unseal_new(sdat, conn->keys.enc_pk,
                                                conn->keys.enc_sk, NULL);
        arr[i].text = plain ? (char *)plain : strdup("[could not decrypt]");
        free(line);
    }

    qsort(arr, n, sizeof(diary_entry_t), entry_cmp_newest_first);

    *entries = arr; *count = n;
    return 0;

fail:
    for (int i = 0; i < n; i++) free(arr[i].text);
    free(arr); return -1;
}
