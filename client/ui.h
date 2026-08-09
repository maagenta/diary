#ifndef UI_H
#define UI_H

#include "net.h"

/* Starts the ncurses interface and enters the main loop.
   entry_at > 0: date (epoch) sent to the server for new entries in this
   session; entry_at <= 0: the server sets its own date.
   Returns when the user exits. */
void ui_run(proto_conn_t *conn, long entry_at);

#endif /* UI_H */
