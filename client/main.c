#include "net.h"
#include "ui.h"
#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sodium.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -h HOST        Server address (default: 127.0.0.1)\n"
        "  -p PORT        Port           (default: %d)\n"
        "  -a AUTH_SK     Authentication private key (default: auth.key)\n"
        "  -e ENC_SK      Encryption private key     (default: enc.key)\n",
        prog, DIARY_PORT);
}

int main(int argc, char *argv[]) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Error: could not initialize libsodium\n");
        return 1;
    }

    const char *host      = "127.0.0.1";
    int         port      = DIARY_PORT;
    const char *auth_sk   = "auth.key";
    const char *enc_sk    = "enc.key";

    int opt;
    while ((opt = getopt(argc, argv, "h:p:a:e:")) != -1) {
        switch (opt) {
        case 'h': host    = optarg; break;
        case 'p': port    = atoi(optarg); break;
        case 'a': auth_sk = optarg; break;
        case 'e': enc_sk  = optarg; break;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    diary_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.fd = -1;

    if (crypto_load_keys(auth_sk, enc_sk, &conn.keys) != 0) {
        fprintf(stderr,
            "Error loading keys.\n"
            "Generate a key pair with: ./keygen\n");
        return 1;
    }

    fprintf(stderr, "Connecting to %s:%d...\n", host, port);
    if (net_connect(&conn, host, port) != 0) {
        fprintf(stderr, "Error: could not connect or authenticate\n");
        return 1;
    }
    fprintf(stderr, "Connected.\n");

    ui_run(&conn);

    net_disconnect(&conn);
    return 0;
}
