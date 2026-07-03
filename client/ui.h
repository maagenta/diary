#ifndef UI_H
#define UI_H

#include "net.h"

/* Inicia la interfaz ncurses y entra en el loop principal.
   Devuelve cuando el usuario sale. */
void ui_run(proto_conn_t *conn);

#endif /* UI_H */
