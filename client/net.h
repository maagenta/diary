#ifndef NET_H
#define NET_H

#include "crypto.h"

typedef struct {
    int          fd;
    diary_keys_t keys;
    char         user_hex[65];
} diary_conn_t;

int  net_connect(diary_conn_t *conn, const char *host, int port);
void net_disconnect(diary_conn_t *conn);

typedef struct {
    int    id;
    long   timestamp;
    char  *text;   /* malloc — caller libera */
} diary_entry_t;

/*
 * Envia una nueva entrada cifrada.
 * Devuelve el id asignado por el servidor (>= 1), o -1 en error.
 */
int net_post_entry(diary_conn_t *conn, const char *text);

/*
 * Actualiza una entrada existente.
 * Devuelve el mismo id en exito, -1 en error.
 */
int net_update_entry(diary_conn_t *conn, int id, const char *text);

/*
 * Elimina una entrada.
 * Devuelve 0 ok, -1 error.
 */
int net_delete_entry(diary_conn_t *conn, int id);

/*
 * Descarga y descifra todas las entradas.
 * *entries es un array de count diary_entry_t (malloc).
 * Caller libera cada entry->text y luego el array.
 */
int net_get_entries(diary_conn_t *conn,
                     diary_entry_t **entries, int *count);

#endif /* NET_H */
