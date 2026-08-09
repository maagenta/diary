#ifndef STORAGE_H
#define STORAGE_H

#include <time.h>

/* Opens/creates the SQLite database at db_path. Call once per child process. */
int  storage_init(const char *db_path);
void storage_close(void);

/* Returns 1 if the user exists, 0 if not */
int storage_user_exists(const char *auth_pubkey_hex);

/* Registers a new user. Returns 0 ok, -1 error */
int storage_register_user(const char *auth_pubkey_hex,
                           const char *encrypt_pubkey_b64);

/* Returns the encryption public key (malloc, caller frees). NULL if it does not exist */
char *storage_get_encrypt_pubkey(const char *auth_pubkey_hex);

/* Inserts a new entry. Returns the assigned id (>= 1) or -1 on error */
int storage_add_entry(const char *auth_pubkey_hex,
                      const char *encrypted_b64,
                      time_t timestamp);

/* Iterates over the user's entries */
typedef void (*entry_cb_t)(int id, time_t ts,
                            const char *data_b64, void *userdata);
int storage_get_entries(const char *auth_pubkey_hex,
                         entry_cb_t cb, void *userdata);

/* Updates the content of an entry. Returns 0 ok, -1 error */
int storage_update_entry(const char *auth_pubkey_hex, int id,
                          const char *encrypted_b64);

/* Deletes an entry. Returns 0 ok, -1 if it does not exist or on error */
int storage_delete_entry(const char *auth_pubkey_hex, int id);

#endif /* STORAGE_H */
