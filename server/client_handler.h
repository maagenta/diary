#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

/* Maneja una conexion de cliente. Llamado tras fork().
 * db_path      — ruta al archivo SQLite
 * allowed_hex  — pubkey hex del unico usuario autorizado */
void handle_client(int sock_fd, const char *db_path, const char *allowed_hex);

#endif /* CLIENT_HANDLER_H */
