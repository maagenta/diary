#include "client_handler.h"
#include "storage.h"
#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>
#include <sodium.h>

static void reap_children(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* Crea directorios recursivamente (equivalente a mkdir -p) */
static int mkdirs(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) return 0;
    if (tmp[len - 1] == '/') tmp[--len] = '\0';
    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            char save = tmp[i];
            tmp[i] = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            tmp[i] = save;
        }
    }
    return 0;
}

/* Lee clave publica Ed25519 (base64) y devuelve su representacion hex */
static int load_pubkey_hex(const char *path,
                             char *hex_out, size_t hex_sz) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Error: no se pudo abrir %s: %s\n", path, strerror(errno));
        return -1;
    }
    char b64[512];
    if (!fgets(b64, sizeof(b64), f)) {
        fclose(f);
        fprintf(stderr, "Error: archivo de clave vacio: %s\n", path);
        return -1;
    }
    fclose(f);

    size_t l = strlen(b64);
    while (l > 0 && (b64[l - 1] == '\n' || b64[l - 1] == '\r'))
        b64[--l] = '\0';

    unsigned char pk[AUTH_PK_LEN];
    size_t pk_len = 0;
    if (sodium_base642bin(pk, sizeof(pk), b64, l,
                           NULL, &pk_len, NULL,
                           sodium_base64_VARIANT_ORIGINAL) != 0
        || pk_len != AUTH_PK_LEN) {
        fprintf(stderr, "Error: clave publica invalida en %s\n", path);
        return -1;
    }
    sodium_bin2hex(hex_out, hex_sz, pk, AUTH_PK_LEN);
    return 0;
}

/* Comprueba o crea el directorio que contiene db_path */
static int ensure_db_dir(const char *db_path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", db_path);
    char *dir = dirname(tmp);

    if (strcmp(dir, ".") == 0) return 0;  /* ruta relativa sin directorio */

    struct stat st;
    if (stat(dir, &st) != 0) {
        /* No existe: intentar crear */
        if (mkdirs(dir) != 0) {
            if (errno == EACCES || errno == EPERM)
                fprintf(stderr, "Error: sin permisos para crear el directorio %s\n", dir);
            else
                fprintf(stderr, "Error: no se pudo crear el directorio %s: %s\n",
                        dir, strerror(errno));
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: %s existe pero no es un directorio\n", dir);
        return -1;
    } else if (access(dir, W_OK) != 0) {
        fprintf(stderr, "Error: sin permisos de escritura en %s\n", dir);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int         port        = DIARY_PORT;
    const char *db_path     = NULL;
    const char *pubkey_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            pubkey_path = argv[++i];
        } else if (strcmp(argv[i], "-db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else {
            fprintf(stderr,
                    "Uso: %s [-p puerto] -k pub-key-file -db database-file\n",
                    argv[0]);
            return 1;
        }
    }

    if (!pubkey_path) {
        fprintf(stderr, "Error: -k pub-key-file es obligatorio\n");
        fprintf(stderr, "Uso: %s [-p puerto] -k pub-key-file -db database-file\n",
                argv[0]);
        return 1;
    }
    if (!db_path) {
        fprintf(stderr, "Error: -db database-file es obligatorio\n");
        fprintf(stderr, "Uso: %s [-p puerto] -k pub-key-file -db database-file\n",
                argv[0]);
        return 1;
    }

    if (sodium_init() < 0) {
        fprintf(stderr, "Error: no se pudo inicializar libsodium\n");
        return 1;
    }

    /* Cargar clave publica autorizada */
    char allowed_hex[AUTH_PK_LEN * 2 + 1];
    if (load_pubkey_hex(pubkey_path, allowed_hex, sizeof(allowed_hex)) != 0)
        return 1;

    /* Crear directorio de la db si no existe */
    if (ensure_db_dir(db_path) != 0)
        return 1;

    /* Abrir la db una vez para validar acceso y detectar si es nueva */
    int db_is_new = (access(db_path, F_OK) != 0);
    if (storage_init(db_path) != 0) {
        fprintf(stderr, "Error: no se pudo abrir la base de datos %s\n", db_path);
        return 1;
    }
    storage_close();

    if (db_is_new)
        printf("DB exitosamente creada en %s\n", db_path);
    else
        printf("%s usada como DB\n", db_path);

    /* Ignorar SIGPIPE y limpiar hijos zombies */
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    sa.sa_handler = reap_children;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(srv, 16) < 0) {
        perror("listen"); return 1;
    }

    printf("Diary server escuchando en puerto %d\n", port);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cli = accept(srv, (struct sockaddr *)&cli_addr, &cli_len);
        if (cli < 0) continue;

        printf("Conexion de %s:%d\n",
               inet_ntoa(cli_addr.sin_addr),
               ntohs(cli_addr.sin_port));

        pid_t pid = fork();
        if (pid == 0) {
            close(srv);
            handle_client(cli, db_path, allowed_hex);
            close(cli);
            exit(0);
        }
        close(cli);
    }

    close(srv);
    return 0;
}
