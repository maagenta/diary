#ifndef NET_H
#define NET_H

#include "../protocol/auth.h"   /* proto_conn_t */

typedef struct {
    int    id;
    long   timestamp;
    char  *text;   /* malloc — caller frees */
} diary_entry_t;

/*
 * Sends a new encrypted entry.
 * timestamp > 0: sent as the entry date ("POST <epoch> <data>").
 * timestamp <= 0: not sent; the server uses its own clock.
 * Returns the id assigned by the server (>= 1), or -1 on error.
 */
int net_post_entry(proto_conn_t *conn, const char *text, long timestamp);

/*
 * Updates an existing entry.
 * Returns the same id on success, -1 on error.
 */
int net_update_entry(proto_conn_t *conn, int id, const char *text);

/*
 * Deletes an entry.
 * Returns 0 ok, -1 error.
 */
int net_delete_entry(proto_conn_t *conn, int id);

/*
 * Downloads and decrypts all entries.
 * *entries is a malloc'd array of count diary_entry_t.
 * Caller frees each entry->text and then the array.
 */
int net_get_entries(proto_conn_t *conn,
                    diary_entry_t **entries, int *count);

#endif /* NET_H */
