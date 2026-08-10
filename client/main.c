#include "net.h"
#include "ui.h"
#include "../protocol/crypto.h"
#include "../common/version.h"
#include "../common/diary.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <getopt.h>   /* getopt_long */
#include <time.h>     /* strptime, mktime: parse --entry-at (no clock read) */

#define OPT_ENTRY_AT 1000   /* long-only options, no short letter */
#define OPT_POST     1001

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -h HOST        Server address (default: 127.0.0.1)\n"
        "  -p PORT        Port           (default: %d)\n"
        "  -a AUTH_SK     Authentication private key (default: auth.key)\n"
        "  -e ENC_SK      Encryption private key     (default: enc.key)\n"
        "  --entry-at \"YYYY-MM-DD HH:MM\"\n"
        "                 Date stored for entries created this session\n"
        "                 (default: the server stamps its own date)\n"
        "  --post         Read one entry from stdin, save it, print its id\n"
        "                 and exit; no TUI (combine with --entry-at)\n"
        "  -v             Print version and exit\n",
        prog, DIARY_PORT);
}

/* Read all of stdin into a NUL-terminated heap buffer (caller frees). */
static char *read_stdin(void) {
    size_t cap = 4096, len = 0, n;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    while ((n = fread(buf + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if (len == cap) {
            char *tmp = realloc(buf, cap *= 2);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
    }
    buf[len] = '\0';
    return buf;
}

/* "YYYY-MM-DD HH:MM" (local time) -> epoch, or -1 if invalid */
static long parse_entry_at(const char *s) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    char *end = strptime(s, "%Y-%m-%d %H:%M", &tm);
    if (!end || *end != '\0') return -1;
    tm.tm_isdst = -1;   /* let mktime resolve DST for that date */
    time_t t = mktime(&tm);
    return (t == (time_t)-1) ? -1 : (long)t;
}

int main(int argc, char *argv[]) {
    /* Required for ncurses to handle UTF-8 input/output */
    setlocale(LC_ALL, "");

    if (proto_init() != 0) {
        fprintf(stderr, "Error: could not initialize libsodium\n");
        return 1;
    }

    const char *host      = "127.0.0.1";
    int         port      = DIARY_PORT;
    const char *auth_sk   = "auth.key";
    const char *enc_sk    = "enc.key";
    long        entry_at  = 0;      /* 0 = the server stamps the date */
    int         post_mode = 0;

    static const struct option long_opts[] = {
        { "entry-at", required_argument, NULL, OPT_ENTRY_AT },
        { "post",     no_argument,       NULL, OPT_POST },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h:p:a:e:v", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'h': host    = optarg; break;
        case 'p': port    = atoi(optarg); break;
        case 'a': auth_sk = optarg; break;
        case 'e': enc_sk  = optarg; break;
        case OPT_ENTRY_AT:
            entry_at = parse_entry_at(optarg);
            if (entry_at <= 0) {
                fprintf(stderr,
                    "Error: invalid --entry-at '%s' "
                    "(expected \"YYYY-MM-DD HH:MM\")\n", optarg);
                return 1;
            }
            break;
        case OPT_POST:
            post_mode = 1;
            break;
        case 'v':
            printf("diary-client %s\n", DIARY_VERSION);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    proto_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.fd = -1;

    if (proto_load_keys(auth_sk, enc_sk, &conn.keys) != 0) {
        fprintf(stderr,
            "Error loading keys.\n"
            "Generate a key pair with: ./keygen\n");
        return 1;
    }

    fprintf(stderr, "Connecting to %s:%d...\n", host, port);
    if (proto_connect(&conn, host, port) != 0) {
        fprintf(stderr, "Error: could not connect or authenticate\n");
        return 1;
    }
    fprintf(stderr, "Connected.\n");

    if (post_mode) {
        char *text = read_stdin();
        if (!text) {
            fprintf(stderr, "Error: could not read entry from stdin\n");
            proto_close(&conn);
            return 1;
        }
        int id = net_post_entry(&conn, text, entry_at);
        free(text);
        proto_close(&conn);
        if (id < 0) {
            fprintf(stderr, "Error: could not save entry\n");
            return 1;
        }
        printf("%d\n", id);
        return 0;
    }

    ui_run(&conn, entry_at);

    proto_close(&conn);
    return 0;
}
