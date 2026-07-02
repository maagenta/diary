#include "ui.h"
#include <ncurses.h>
/* BUTTON5 (scroll down) is not in the system ncurses on macOS.
   macOS uses 6-bit groups per button, not 5 as in the standard. */
#ifndef BUTTON5_PRESSED
#  define BUTTON5_PRESSED ((mmask_t)(BUTTON4_PRESSED << 6))
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>

#define COLOR_HEADER  1
#define COLOR_STATUS  2
#define COLOR_SEL     3
#define COLOR_NORMAL  4

#define AUTOSAVE_MS   2000  /* autosave debounce (ms) */
#define SCROLL_LINES  3     /* lines per mouse scroll event */

/* ------------------------------------------------------------------ */
/* General helpers                                                      */
/* ------------------------------------------------------------------ */

static void draw_hline(int row, int cols) {
    move(row, 0);
    for (int i = 0; i < cols; i++) addch(ACS_HLINE);
}

static void draw_header(int cols, const char *title) {
    attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
    mvhline(0, 0, ' ', cols);
    int x = (cols - (int)strlen(title)) / 2;
    if (x < 0) x = 0;
    mvprintw(0, x, "%s", title);
    attroff(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
}

static void draw_status(int rows, int cols, const char *msg) {
    attron(COLOR_PAIR(COLOR_STATUS));
    mvhline(rows - 1, 0, ' ', cols);
    mvprintw(rows - 1, 1, "%s", msg);
    attroff(COLOR_PAIR(COLOR_STATUS));
}

/* UTF-8: continuation bytes (10xxxxxx) take no extra column */
static int u8_is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

/* Length in bytes of the UTF-8 character starting at buf[i] */
static size_t u8_char_len(const char *buf, size_t len, size_t i) {
    size_t n = 1;
    while (i + n < len && u8_is_cont((unsigned char)buf[i + n])) n++;
    return n;
}

static char *format_ts(time_t ts) {
    static char buf[32];
    struct tm *t = localtime(&ts);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", t);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Visual position in the text buffer                                   */
/*                                                                      */
/* Rule: wrap when vcol >= cols-1. '\n' advances to (vrow+1, 0).      */
/* ------------------------------------------------------------------ */

static void get_vpos(const char *buf, size_t buf_len, size_t cursor,
                      int cols, int *vr, int *vc) {
    int r = 0, c = 0;
    for (size_t i = 0; i < cursor && i < buf_len; i++) {
        if (u8_is_cont((unsigned char)buf[i])) continue;
        if (buf[i] == '\n' || c >= cols - 1) { r++; c = 0; if (buf[i] == '\n') continue; }
        c++;
    }
    *vr = r; *vc = c;
}

/* Returns the buffer index for visual position (tvr, tvc).
   If the row is shorter than tvc, returns the last index of that row. */
static size_t vpos_to_idx(const char *buf, size_t buf_len,
                            int cols, int tvr, int tvc) {
    int r = 0, c = 0;
    for (size_t i = 0; i <= buf_len; i++) {
        if (i < buf_len && u8_is_cont((unsigned char)buf[i])) continue;
        if (r == tvr && c == tvc) return i;
        if (i == buf_len) return i;
        char ch = buf[i];
        if (ch == '\n' || c >= cols - 1) {
            if (r == tvr) return i;
            r++; c = 0; if (ch == '\n') continue;
        }
        c++;
    }
    return buf_len;
}

/* ------------------------------------------------------------------ */
/* Temporary on-disk backup                                             */
/* ------------------------------------------------------------------ */

static void backup_path(int entry_id, char *out, size_t sz) {
    if (entry_id > 0)
        snprintf(out, sz, "diary_%d.tmp", entry_id);
    else
        snprintf(out, sz, "diary_new.tmp");
}

static void backup_write(int entry_id, const char *buf, size_t len) {
    char path[64];
    backup_path(entry_id, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f) { fwrite(buf, 1, len, f); fclose(f); }
}

static void backup_remove(int entry_id) {
    char path[64];
    backup_path(entry_id, path, sizeof(path));
    remove(path);
}

/* ------------------------------------------------------------------ */
/* Screen: entry viewer                                                 */
/* ------------------------------------------------------------------ */

static void screen_view(diary_entry_t *e) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    WINDOW *win = newwin(rows, cols, 0, 0);
    keypad(win, TRUE);

    char title[80];
    snprintf(title, sizeof(title), " Entry #%d - %s ", e->id, format_ts(e->timestamp));

    const char *text = e->text ? e->text : "";
    size_t tlen = strlen(text);

    int total_vrows = 1, vcol = 0;
    for (size_t i = 0; i < tlen; i++) {
        if (u8_is_cont((unsigned char)text[i])) continue;
        if (text[i] == '\n' || vcol >= cols - 1) { total_vrows++; vcol = 0; if (text[i] == '\n') continue; }
        vcol++;
    }

    int view_top = 0;

    while (1) {
        getmaxyx(stdscr, rows, cols);
        int text_rows = rows - 2;

        wattron(win, COLOR_PAIR(COLOR_HEADER) | A_BOLD);
        mvwhline(win, 0, 0, ' ', cols);
        mvwprintw(win, 0, 1, "%s", title);
        wattroff(win, COLOR_PAIR(COLOR_HEADER) | A_BOLD);

        for (int r = 1; r < rows - 1; r++) { wmove(win, r, 0); wclrtoeol(win); }

        int vrow = 0; vcol = 0;
        for (size_t i = 0; i < tlen; ) {
            char c = text[i];
            size_t clen = u8_char_len(text, tlen, i);
            if (c == '\n' || vcol >= cols - 1) { vrow++; vcol = 0; if (c == '\n') { i++; continue; } }
            int wr = vrow - view_top + 1;
            if (wr >= 1 && wr < rows - 1) mvwaddnstr(win, wr, vcol, text + i, (int)clen);
            vcol++;
            i += clen;
        }

        wattron(win, COLOR_PAIR(COLOR_STATUS));
        mvwhline(win, rows - 1, 0, ' ', cols);
        mvwprintw(win, rows - 1, 1,
                  "[j/k or scroll] navigate  [Q/ESC] back  (%d-%d / %d)",
                  view_top + 1,
                  (view_top + text_rows < total_vrows) ? view_top + text_rows : total_vrows,
                  total_vrows);
        wattroff(win, COLOR_PAIR(COLOR_STATUS));
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == 'q' || ch == 'Q' || ch == 27) break;
        else if ((ch == KEY_DOWN || ch == 'j') && view_top + text_rows < total_vrows) view_top++;
        else if ((ch == KEY_UP   || ch == 'k') && view_top > 0) view_top--;
        else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (ev.bstate & BUTTON4_PRESSED) {
                    view_top -= SCROLL_LINES; if (view_top < 0) view_top = 0;
                } else if (ev.bstate & (mmask_t)BUTTON5_PRESSED) {
                    view_top += SCROLL_LINES;
                    if (view_top + text_rows > total_vrows) view_top = total_vrows - text_rows;
                    if (view_top < 0) view_top = 0;
                }
            }
        }
    }
    delwin(win);
}

/* ------------------------------------------------------------------ */
/* Screen: editor                                                       */
/*                                                                      */
/* entry_id = 0  -> new entry (POST)                                   */
/* entry_id > 0  -> edit existing (UPDATE)                             */
/* initial_text  -> NULL for new, current text for edit                */
/*                                                                      */
/* Ctrl+S / F2   -> save without exiting (autosave every AUTOSAVE_MS) */
/* ESC           -> exit                                                */
/* ------------------------------------------------------------------ */

static void screen_editor(diary_conn_t *conn, int entry_id,
                            const char *initial_text, time_t timestamp) {
    /* Disable IXON so Ctrl+S reaches the app */
    struct termios tios_orig, tios;
    tcgetattr(STDIN_FILENO, &tios_orig);
    tios = tios_orig;
    tios.c_iflag &= ~(tcflag_t)IXON;
    tcsetattr(STDIN_FILENO, TCSANOW, &tios);

    size_t buf_sz = 4096;
    char  *buf    = calloc(1, buf_sz);
    size_t buf_len = 0;
    size_t cursor  = 0;
    if (initial_text) {
        buf_len = strlen(initial_text);
        while (buf_len + 1 >= buf_sz) buf_sz *= 2;
        buf = realloc(buf, buf_sz);
        memcpy(buf, initial_text, buf_len);
        cursor = buf_len;
    }

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    WINDOW *win = newwin(rows, cols, 0, 0);
    keypad(win, TRUE);

    /* Autosave timeout: wgetch returns ERR if no input within AUTOSAVE_MS */
    wtimeout(win, AUTOSAVE_MS);

    int   view_top  = 0;
    int   dirty     = 0;
    char  status_msg[128] = "";

    while (1) {
        getmaxyx(stdscr, rows, cols);
        int text_rows = rows - 2;

        int cur_vrow, cur_vcol;
        get_vpos(buf, buf_len, cursor, cols, &cur_vrow, &cur_vcol);

        if (cur_vrow < view_top) view_top = cur_vrow;
        if (cur_vrow >= view_top + text_rows) view_top = cur_vrow - text_rows + 1;
        if (view_top < 0) view_top = 0;

        wattron(win, COLOR_PAIR(COLOR_HEADER) | A_BOLD);
        mvwhline(win, 0, 0, ' ', cols);
        {
            char hdr[128];
            if (entry_id > 0)
                snprintf(hdr, sizeof(hdr), " Entry #%d - %s%s ",
                         entry_id, format_ts(timestamp), dirty ? " *" : "");
            else
                snprintf(hdr, sizeof(hdr), " New entry - %s%s ",
                         format_ts(timestamp), dirty ? " *" : "");
            mvwprintw(win, 0, 1, "%s", hdr);
        }
        wattroff(win, COLOR_PAIR(COLOR_HEADER) | A_BOLD);

        for (int r = 1; r < rows - 1; r++) { wmove(win, r, 0); wclrtoeol(win); }

        int vrow = 0, vcol = 0;
        for (size_t i = 0; i < buf_len; ) {
            char c = buf[i];
            size_t clen = u8_char_len(buf, buf_len, i);
            if (c == '\n' || vcol >= cols - 1) { vrow++; vcol = 0; if (c == '\n') { i++; continue; } }
            int wr = vrow - view_top + 1;
            if (wr >= 1 && wr < rows - 1) mvwaddnstr(win, wr, vcol, buf + i, (int)clen);
            vcol++;
            i += clen;
        }

        wattron(win, COLOR_PAIR(COLOR_STATUS));
        mvwhline(win, rows - 1, 0, ' ', cols);
        {
            char bar[256];
            if (status_msg[0])
                snprintf(bar, sizeof(bar), "%s  |  [Ctrl+S/F2] save  [ESC] exit", status_msg);
            else
                snprintf(bar, sizeof(bar), "[Ctrl+S/F2] save  [ESC] exit");
            mvwprintw(win, rows - 1, 1, "%s", bar);
        }
        wattroff(win, COLOR_PAIR(COLOR_STATUS));

        wmove(win, cur_vrow - view_top + 1, cur_vcol);
        wrefresh(win);
        status_msg[0] = '\0';

        int ch = wgetch(win);

        /* ---- Autosave timeout ---- */
        if (ch == ERR) {
            if (!dirty) continue;
            ch = 19; /* simulate Ctrl+S */
        }

        /* ---- Save without exiting ---- */
        if (ch == 19 || ch == KEY_F(2) || ch == 23) { /* Ctrl+S, F2, Ctrl+W */
            if (!dirty && entry_id > 0) {
                snprintf(status_msg, sizeof(status_msg), "No changes.");
                continue;
            }
            buf[buf_len] = '\0';

            backup_write(entry_id > 0 ? entry_id : 0, buf, buf_len);

            int new_id;
            if (entry_id > 0)
                new_id = net_update_entry(conn, entry_id, buf);
            else
                new_id = net_post_entry(conn, buf);

            if (new_id > 0) {
                backup_remove(entry_id > 0 ? entry_id : 0);
                if (entry_id <= 0) entry_id = new_id;
                dirty = 0;
                snprintf(status_msg, sizeof(status_msg), "Saved.");
            } else {
                snprintf(status_msg, sizeof(status_msg),
                         "Save failed. Backup: %s",
                         entry_id > 0 ? "diary_X.tmp" : "diary_new.tmp");
            }

        /* ---- Exit ---- */
        } else if (ch == 27) {
            if (dirty) {
                snprintf(status_msg, sizeof(status_msg),
                         "Unsaved changes. Press ESC again to discard, Ctrl+S to save.");
                dirty = -1;
            } else if (dirty == -1) {
                break;
            } else {
                break;
            }

        /* ---- Navigation ---- */
        } else if (ch == KEY_UP) {
            if (cur_vrow > 0) cursor = vpos_to_idx(buf, buf_len, cols, cur_vrow - 1, cur_vcol);
        } else if (ch == KEY_DOWN) {
            cursor = vpos_to_idx(buf, buf_len, cols, cur_vrow + 1, cur_vcol);
        } else if (ch == KEY_LEFT  && cursor > 0) {
            cursor--;
            while (cursor > 0 && u8_is_cont((unsigned char)buf[cursor])) cursor--;
        }
        else if   (ch == KEY_RIGHT && cursor < buf_len) {
            cursor += u8_char_len(buf, buf_len, cursor);
        }
        else if   (ch == KEY_HOME) { cursor = vpos_to_idx(buf, buf_len, cols, cur_vrow, 0); }
        else if   (ch == KEY_END)  { cursor = vpos_to_idx(buf, buf_len, cols, cur_vrow, cols); }

        /* ---- Mouse scroll (moves view, not cursor) ---- */
        else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (ev.bstate & BUTTON4_PRESSED) {
                    view_top -= SCROLL_LINES; if (view_top < 0) view_top = 0;
                } else if (ev.bstate & (mmask_t)BUTTON5_PRESSED) {
                    view_top += SCROLL_LINES;
                }
            }

        /* ---- Editing ---- */
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (cursor > 0) {
                size_t start = cursor - 1;
                while (start > 0 && u8_is_cont((unsigned char)buf[start])) start--;
                memmove(buf + start, buf + cursor, buf_len - cursor);
                buf_len -= cursor - start; cursor = start; dirty = 1;
            }
        } else if (ch == KEY_DC) {
            if (cursor < buf_len) {
                size_t clen = u8_char_len(buf, buf_len, cursor);
                memmove(buf + cursor, buf + cursor + clen, buf_len - cursor - clen);
                buf_len -= clen; dirty = 1;
            }
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (buf_len + 1 >= buf_sz) {
                buf_sz *= 2; char *nb = realloc(buf, buf_sz);
                if (!nb) break;
                buf = nb;
            }
            memmove(buf + cursor + 1, buf + cursor, buf_len - cursor);
            buf[cursor] = '\n'; buf_len++; cursor++; dirty = 1;
        } else if (isprint(ch) || (ch >= 0x80 && ch <= 0xFF)) { /* ASCII or UTF-8 byte */
            if (buf_len + 1 >= buf_sz) {
                buf_sz *= 2; char *nb = realloc(buf, buf_sz);
                if (!nb) break;
                buf = nb;
            }
            memmove(buf + cursor + 1, buf + cursor, buf_len - cursor);
            buf[cursor] = (char)ch; buf_len++; cursor++; dirty = 1;
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &tios_orig);
    free(buf);
    delwin(win);
}

/* ------------------------------------------------------------------ */
/* Main screen                                                          */
/* ------------------------------------------------------------------ */

static void reload_entries(diary_conn_t *conn,
                             diary_entry_t **entries, int *count,
                             char *smsg, size_t ssz) {
    for (int i = 0; i < *count; i++) free((*entries)[i].text);
    free(*entries); *entries = NULL; *count = 0;
    if (net_get_entries(conn, entries, count) != 0) {
        strncpy(smsg, "Error fetching entries.", ssz - 1);
        *count = 0;
    }
}

static void screen_list(diary_conn_t *conn) {
    int rows, cols;
    diary_entry_t *entries = NULL;
    int count = 0, selected = 0, scroll = 0;
    char status_msg[128] = "";
    int  confirm_delete  = 0;

    reload_entries(conn, &entries, &count, status_msg, sizeof(status_msg));

    while (1) {
        getmaxyx(stdscr, rows, cols);
        int list_rows = rows - 3;
        confirm_delete = 0;

        clear();
        draw_header(cols, " DIARY ");
        draw_hline(1, cols);

        if (count == 0) {
            attron(A_DIM);
            mvprintw(rows / 2, (cols - 30) / 2, "No entries yet. Press [N] to create one.");
            attroff(A_DIM);
        }

        if (selected < scroll) scroll = selected;
        if (selected >= scroll + list_rows) scroll = selected - list_rows + 1;
        if (scroll < 0) scroll = 0;

        for (int i = 0; i < list_rows && (i + scroll) < count; i++) {
            int idx = i + scroll;
            diary_entry_t *e = &entries[idx];
            char preview[64] = "";
            if (e->text) {
                char *nl = strchr(e->text, '\n');
                size_t plen = nl ? (size_t)(nl - e->text) : strlen(e->text);
                if (plen > 60) plen = 60;
                memcpy(preview, e->text, plen); preview[plen] = '\0';
            }
            if (idx == selected) {
                attron(COLOR_PAIR(COLOR_SEL) | A_BOLD);
                mvhline(2 + i, 0, ' ', cols);
                mvprintw(2 + i, 1, "  %-16s  %s", format_ts(e->timestamp), preview);
                attroff(COLOR_PAIR(COLOR_SEL) | A_BOLD);
            } else {
                mvprintw(2 + i, 1, "  %-16s  %s", format_ts(e->timestamp), preview);
            }
        }

        draw_hline(rows - 2, cols);
        draw_status(rows, cols,
            status_msg[0] ? status_msg
                          : "[N]ew  [E]dit  [Enter]Read  [D]elete  [R]eload  [Q]uit");
        status_msg[0] = '\0';
        refresh();

        int ch = getch();

        if (ch == 'q' || ch == 'Q') {
            break;
        } else if ((ch == KEY_UP || ch == 'k') && selected > 0) {
            selected--;
        } else if ((ch == KEY_DOWN || ch == 'j') && selected < count - 1) {
            selected++;
        } else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if      (ev.bstate & BUTTON4_PRESSED          && selected > 0)       selected--;
                else if (ev.bstate & (mmask_t)BUTTON5_PRESSED && selected < count-1) selected++;
            }
        } else if (ch == '\n' || ch == KEY_ENTER) {
            if (count > 0) screen_view(&entries[selected]);
        } else if (ch == 'n' || ch == 'N') {
            screen_editor(conn, 0, NULL, time(NULL));
            reload_entries(conn, &entries, &count, status_msg, sizeof(status_msg));
            selected = count > 0 ? count - 1 : 0;
        } else if (ch == 'e' || ch == 'E') {
            if (count > 0) {
                screen_editor(conn, entries[selected].id, entries[selected].text,
                              (time_t)entries[selected].timestamp);
                reload_entries(conn, &entries, &count, status_msg, sizeof(status_msg));
            }
        } else if (ch == 'd' || ch == 'D') {
            if (count > 0) {
                int del_id = entries[selected].id;
                draw_status(rows, cols,
                    "Confirm delete: [Y]es  [N/ESC] cancel");
                refresh();
                int conf = getch();
                if (conf == 'y' || conf == 'Y') {
                    if (net_delete_entry(conn, del_id) == 0) {
                        strncpy(status_msg, "Entry deleted.", sizeof(status_msg) - 1);
                    } else {
                        strncpy(status_msg, "Error deleting entry.", sizeof(status_msg) - 1);
                    }
                    reload_entries(conn, &entries, &count, status_msg[0] ? status_msg : status_msg, sizeof(status_msg));
                    if (selected >= count) selected = count > 0 ? count - 1 : 0;
                }
            }
        } else if (ch == 'r' || ch == 'R') {
            reload_entries(conn, &entries, &count, status_msg, sizeof(status_msg));
            if (selected >= count) selected = count > 0 ? count - 1 : 0;
            if (!status_msg[0]) strncpy(status_msg, "Reloaded.", sizeof(status_msg) - 1);
        }
        (void)confirm_delete;
    }

    for (int i = 0; i < count; i++) free(entries[i].text);
    free(entries);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

void ui_run(diary_conn_t *conn) {
    initscr();
    set_escdelay(25);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);

    /* Include BUTTON5 explicitly for scroll-down support */
    mmask_t mouse_mask = ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION
                       | (mmask_t)BUTTON5_PRESSED;
    mousemask(mouse_mask, NULL);
    mouseinterval(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(COLOR_HEADER, COLOR_BLACK, COLOR_CYAN);
        init_pair(COLOR_STATUS, COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_SEL,    COLOR_BLACK, COLOR_YELLOW);
        init_pair(COLOR_NORMAL, -1,          -1);
    }

    screen_list(conn);
    endwin();
}
