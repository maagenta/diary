#ifndef UI_H
#define UI_H

#include "net.h"

/* Inicia la interfaz ncurses y entra en el loop principal.
   entry_at > 0: fecha (epoch) enviada al servidor para las entradas nuevas
   de esta sesion; entry_at <= 0: el servidor pone su propia fecha.
   Devuelve cuando el usuario sale. */
void ui_run(proto_conn_t *conn, long entry_at);

#endif /* UI_H */
