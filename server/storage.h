#ifndef STORAGE_H
#define STORAGE_H

#include <time.h>

/* Abre/crea la base de datos SQLite en db_path. Llamar una vez por proceso hijo. */
int  storage_init(const char *db_path);
void storage_close(void);

/* Devuelve 1 si el usuario existe, 0 si no */
int storage_user_exists(const char *auth_pubkey_hex);

/* Registra un nuevo usuario. Devuelve 0 ok, -1 error */
int storage_register_user(const char *auth_pubkey_hex,
                           const char *encrypt_pubkey_b64);

/* Devuelve la clave publica de cifrado (malloc, caller libera). NULL si no existe */
char *storage_get_encrypt_pubkey(const char *auth_pubkey_hex);

/* Inserta una nueva entrada. Devuelve el id asignado (>= 1) o -1 en error */
int storage_add_entry(const char *auth_pubkey_hex,
                      const char *encrypted_b64,
                      time_t timestamp);

/* Itera las entradas del usuario */
typedef void (*entry_cb_t)(int id, time_t ts,
                            const char *data_b64, void *userdata);
int storage_get_entries(const char *auth_pubkey_hex,
                         entry_cb_t cb, void *userdata);

/* Actualiza el contenido de una entrada. Devuelve 0 ok, -1 error */
int storage_update_entry(const char *auth_pubkey_hex, int id,
                          const char *encrypted_b64);

/* Elimina una entrada. Devuelve 0 ok, -1 si no existe o error */
int storage_delete_entry(const char *auth_pubkey_hex, int id);

#endif /* STORAGE_H */
