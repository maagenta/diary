#include "storage.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sqlite3 *db = NULL;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

int storage_init(const char *db_path) {
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "storage_init: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    /* WAL allows concurrent reads and writes without full locking */
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;",  NULL, NULL, NULL);

    const char *schema =
        "CREATE TABLE IF NOT EXISTS users ("
        "  auth_pubkey    TEXT PRIMARY KEY NOT NULL,"
        "  encrypt_pubkey TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS entries ("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  auth_pubkey  TEXT    NOT NULL,"
        "  timestamp    INTEGER NOT NULL,"
        "  data         TEXT    NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_entries_auth"
        "  ON entries(auth_pubkey);";

    char *err = NULL;
    if (sqlite3_exec(db, schema, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "storage_init schema: %s\n", err);
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

void storage_close(void) {
    if (db) { sqlite3_close(db); db = NULL; }
}

/* ------------------------------------------------------------------ */
/* Users                                                                */
/* ------------------------------------------------------------------ */

int storage_user_exists(const char *auth_pubkey_hex) {
    sqlite3_stmt *s;
    sqlite3_prepare_v2(db,
        "SELECT 1 FROM users WHERE auth_pubkey=? LIMIT 1", -1, &s, NULL);
    sqlite3_bind_text(s, 1, auth_pubkey_hex, -1, SQLITE_STATIC);
    int exists = (sqlite3_step(s) == SQLITE_ROW);
    sqlite3_finalize(s);
    return exists;
}

int storage_register_user(const char *auth_pubkey_hex,
                            const char *encrypt_pubkey_b64) {
    sqlite3_stmt *s;
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO users(auth_pubkey, encrypt_pubkey) VALUES(?,?)",
        -1, &s, NULL);
    sqlite3_bind_text(s, 1, auth_pubkey_hex,   -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 2, encrypt_pubkey_b64, -1, SQLITE_STATIC);
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

char *storage_get_encrypt_pubkey(const char *auth_pubkey_hex) {
    sqlite3_stmt *s;
    sqlite3_prepare_v2(db,
        "SELECT encrypt_pubkey FROM users WHERE auth_pubkey=? LIMIT 1",
        -1, &s, NULL);
    sqlite3_bind_text(s, 1, auth_pubkey_hex, -1, SQLITE_STATIC);
    char *result = NULL;
    if (sqlite3_step(s) == SQLITE_ROW)
        result = strdup((const char *)sqlite3_column_text(s, 0));
    sqlite3_finalize(s);
    return result;
}

/* ------------------------------------------------------------------ */
/* Entries                                                              */
/* ------------------------------------------------------------------ */

int storage_add_entry(const char *auth_pubkey_hex,
                       const char *encrypted_b64,
                       time_t timestamp) {
    sqlite3_stmt *s;
    sqlite3_prepare_v2(db,
        "INSERT INTO entries(auth_pubkey, timestamp, data) VALUES(?,?,?)",
        -1, &s, NULL);
    sqlite3_bind_text(s, 1, auth_pubkey_hex, -1, SQLITE_STATIC);
    sqlite3_bind_int64(s, 2, (sqlite3_int64)timestamp);
    sqlite3_bind_text(s, 3, encrypted_b64,   -1, SQLITE_STATIC);
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) return -1;
    return (int)sqlite3_last_insert_rowid(db);
}

int storage_get_entries(const char *auth_pubkey_hex,
                         entry_cb_t cb, void *userdata) {
    sqlite3_stmt *s;
    sqlite3_prepare_v2(db,
        "SELECT id, timestamp, data FROM entries"
        " WHERE auth_pubkey=? ORDER BY id ASC",
        -1, &s, NULL);
    sqlite3_bind_text(s, 1, auth_pubkey_hex, -1, SQLITE_STATIC);
    while (sqlite3_step(s) == SQLITE_ROW) {
        int    id   = sqlite3_column_int(s, 0);
        time_t ts   = (time_t)sqlite3_column_int64(s, 1);
        const char *data = (const char *)sqlite3_column_text(s, 2);
        cb(id, ts, data, userdata);
    }
    sqlite3_finalize(s);
    return 0;
}

int storage_update_entry(const char *auth_pubkey_hex, int id,
                          const char *encrypted_b64) {
    sqlite3_stmt *s;
    sqlite3_prepare_v2(db,
        "UPDATE entries SET data=? WHERE id=? AND auth_pubkey=?",
        -1, &s, NULL);
    sqlite3_bind_text(s, 1, encrypted_b64,   -1, SQLITE_STATIC);
    sqlite3_bind_int(s,  2, id);
    sqlite3_bind_text(s, 3, auth_pubkey_hex, -1, SQLITE_STATIC);
    int rc = sqlite3_step(s);
    int changed = sqlite3_changes(db);
    sqlite3_finalize(s);
    return (rc == SQLITE_DONE && changed > 0) ? 0 : -1;
}

int storage_delete_entry(const char *auth_pubkey_hex, int id) {
    sqlite3_stmt *s;
    sqlite3_prepare_v2(db,
        "DELETE FROM entries WHERE id=? AND auth_pubkey=?",
        -1, &s, NULL);
    sqlite3_bind_int(s,  1, id);
    sqlite3_bind_text(s, 2, auth_pubkey_hex, -1, SQLITE_STATIC);
    int rc = sqlite3_step(s);
    int changed = sqlite3_changes(db);
    sqlite3_finalize(s);
    return (rc == SQLITE_DONE && changed > 0) ? 0 : -1;
}
