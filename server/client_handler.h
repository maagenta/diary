#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

/* proto_serve handler for one authenticated diary client.
 *   fd        connected socket
 *   user_hex  client's auth pubkey (hex), already authenticated
 *   ctx       const char * db_path (SQLite file) */
void diary_handle(int fd, const char *user_hex, void *ctx);

#endif /* CLIENT_HANDLER_H */
