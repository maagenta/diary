#include "client_handler.h"
#include "storage.h"
#include "../protocol/wire.h"
#include "../protocol/protocol.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
/* Per-connection handler (called by proto_serve after auth)            */
/* ------------------------------------------------------------------ */

void diary_handle(int fd, const char *user_hex, void *ctx) {
    const char *db_path = (const char *)ctx;

    if (storage_init(db_path) != 0) { proto_send_line(fd, "FAIL db"); return; }

    if (!storage_user_exists(user_hex)) {
        proto_send_line(fd, "REGISTER");

        char line[MAX_LINE];
        if (proto_recv_line(fd, line, sizeof(line)) < 0) { storage_close(); return; }
        if (strncmp(line, "REGISTER ", 9) != 0) {
            proto_send_line(fd, "FAIL expected REGISTER <enc_pubkey_b64>");
            storage_close(); return;
        }

        const char *epk_b64 = line + 9;
        unsigned char epk[ENC_PK_LEN];
        size_t epk_len = 0;
        if (sodium_base642bin(epk, sizeof(epk), epk_b64, strlen(epk_b64),
                              NULL, &epk_len, NULL,
                              sodium_base64_VARIANT_ORIGINAL) != 0
            || epk_len != ENC_PK_LEN) {
            proto_send_line(fd, "FAIL invalid encryption key");
            storage_close(); return;
        }
        if (storage_register_user(user_hex, epk_b64) != 0) {
            proto_send_line(fd, "FAIL could not register user");
            storage_close(); return;
        }
        proto_send_line(fd, "OK");
    } else {
        proto_send_line(fd, "OK");
    }

    char *line;
    while ((line = proto_recv_line_dyn(fd)) != NULL) {
        if (strcmp(line, "QUIT") == 0) {
            proto_send_line(fd, "BYE"); free(line); break;

        } else if (strncmp(line, "POST ", 5) == 0) {
            int id = storage_add_entry(user_hex, line + 5, time(NULL));
            if (id < 0) {
                proto_send_line(fd, "FAIL could not save entry");
            } else {
                char ok[32];
                snprintf(ok, sizeof(ok), "OK %d", id);
                proto_send_line(fd, ok);
            }

        } else if (strcmp(line, "GET") == 0) {
            entry_collect_t col = { 0, calloc(MAX_ENTRIES, sizeof(char *)) };
            if (!col.lines) { proto_send_line(fd, "FAIL out of memory"); free(line); continue; }
            storage_get_entries(user_hex, collect_entry, &col);
            char hdr[32];
            snprintf(hdr, sizeof(hdr), "ENTRIES %d", col.count);
            proto_send_line(fd, hdr);
            for (int i = 0; i < col.count; i++) {
                proto_send_line(fd, col.lines[i]); free(col.lines[i]);
            }
            free(col.lines);

        } else if (strncmp(line, "UPDATE ", 7) == 0) {
            char *sp = strchr(line + 7, ' ');
            if (!sp) {
                proto_send_line(fd, "FAIL invalid UPDATE format");
            } else {
                *sp = '\0';
                int uid = atoi(line + 7);
                if (storage_update_entry(user_hex, uid, sp + 1) == 0)
                    proto_send_line(fd, "OK");
                else
                    proto_send_line(fd, "FAIL entry not found");
            }

        } else if (strncmp(line, "DELETE ", 7) == 0) {
            int del_id = atoi(line + 7);
            if (storage_delete_entry(user_hex, del_id) == 0)
                proto_send_line(fd, "OK");
            else
                proto_send_line(fd, "FAIL entry not found");

        } else {
            proto_send_line(fd, "FAIL unknown command");
        }
        free(line);
    }

    storage_close();
}
