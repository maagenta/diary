#include "client_handler.h"
#include "storage.h"
#include "../protocol/crypto.h"
#include "../protocol/serve.h"
#include "../common/version.h"
#include "../common/diary.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <libgen.h>

/* Recursively create directories (equivalent to mkdir -p) */
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

/* Ensure the directory containing db_path exists */
static int ensure_db_dir(const char *db_path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", db_path);
    char *dir = dirname(tmp);

    if (strcmp(dir, ".") == 0) return 0;

    struct stat st;
    if (stat(dir, &st) != 0) {
        if (mkdirs(dir) != 0) {
            if (errno == EACCES || errno == EPERM)
                fprintf(stderr, "Error: permission denied creating directory %s\n", dir);
            else
                fprintf(stderr, "Error: could not create directory %s: %s\n",
                        dir, strerror(errno));
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: %s exists but is not a directory\n", dir);
        return -1;
    } else if (access(dir, W_OK) != 0) {
        fprintf(stderr, "Error: no write permission on %s\n", dir);
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
        } else if (strcmp(argv[i], "-v") == 0) {
            printf("diary-server %s\n", DIARY_VERSION);
            return 0;
        } else {
            fprintf(stderr,
                    "Usage: %s [-p port] -k pub-key-file -db database-file\n",
                    argv[0]);
            return 1;
        }
    }

    if (!pubkey_path) {
        fprintf(stderr, "Error: -k pub-key-file is required\n");
        fprintf(stderr, "Usage: %s [-p port] -k pub-key-file -db database-file\n",
                argv[0]);
        return 1;
    }
    if (!db_path) {
        fprintf(stderr, "Error: -db database-file is required\n");
        fprintf(stderr, "Usage: %s [-p port] -k pub-key-file -db database-file\n",
                argv[0]);
        return 1;
    }

    if (proto_init() != 0) {
        fprintf(stderr, "Error: could not initialize libsodium\n");
        return 1;
    }

    char allowed_hex[AUTH_PK_LEN * 2 + 1];
    if (proto_load_pubkey_hex(pubkey_path, allowed_hex, sizeof(allowed_hex)) != 0)
        return 1;

    if (ensure_db_dir(db_path) != 0)
        return 1;

    int db_is_new = (access(db_path, F_OK) != 0);
    if (storage_init(db_path) != 0) {
        fprintf(stderr, "Error: could not open database %s\n", db_path);
        return 1;
    }
    storage_close();

    if (db_is_new)
        printf("Database created at %s\n", db_path);
    else
        printf("Using database %s\n", db_path);

    printf("Diary server listening on port %d\n", port);

    /* Blocks: accept + fork + authenticate, then diary_handle per client. */
    if (proto_serve(port, allowed_hex, diary_handle, (void *)db_path) != 0) {
        fprintf(stderr, "Error: could not start server on port %d\n", port);
        return 1;
    }
    return 0;
}
