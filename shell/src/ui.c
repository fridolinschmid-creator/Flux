#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define COL_BG       0x0B0E14
#define COL_ACCENT   0x4FD1C5
#define COL_TEXT     0xE6E6E6
#define COL_DIM      0x6B7280
#define COL_STATUSBAR 0x161B26
#define COL_KEY      0x1F2533
#define COL_KEY_SPEC 0x29384A
#define COL_ROW      0x1A2230
#define COL_DANGER   0xE05252
#define COL_CANCEL   0xB23B3B
#define COL_SEND     0x2F9E6E

#define STATUSBAR_H  40
#define QUICKROW_H   56
#define INPUT_BAR_H  64
#define MIC_BTN_W    INPUT_BAR_H
#define TITLE_AREA_H 64
#define LIST_BACK_H  64
#define LIST_MAX_ROWS 12
#define PIN_LEN 4

/* ---- Statusleiste (oben, fast alle Bildschirme) -------------------- */

static void draw_statusbar(flux_fb_t *fb) {
    flux_fb_fill_rect(fb, 0, 0, fb->width, STATUSBAR_H, COL_STATUSBAR);

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M", &tmv);
    flux_fb_text(fb, 12, 10, buf, COL_TEXT, 3);

    const char *label = "Flux";
    int lw = flux_fb_text_width(label, 3);
    flux_fb_text(fb, fb->width - lw - 12, 10, label, COL_DIM, 3);
}

static void draw_back_bar(flux_fb_t *fb, const char *label) {
    int y = fb->height - LIST_BACK_H;
    flux_fb_fill_rect(fb, 0, y, fb->width, LIST_BACK_H, COL_STATUSBAR);
    int tw = flux_fb_text_width(label, 3);
    flux_fb_text(fb, (fb->width - tw) / 2, y + (LIST_BACK_H - 21) / 2, label, COL_ACCENT, 3);
}

/* ---- Bildschirmtastatur --------------------------------------------
 * Eine Geometrie-Funktion fuer Zeichnen UND Hit-Testing -- sonst
 * driften beide irgendwann auseinander und Taps landen daneben. */

#define KBD_LETTER_ROWS 4
#define KBD_TOTAL_ROWS  5
#define KBD_MAX_KEYS    48

static const char *kbd_letter_rows[KBD_LETTER_ROWS] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
};

typedef struct {
    int  x, y, w, h;
    char ch;            /* 0 bei Sondertasten */
    int  is_backspace;
    int  is_enter;
} kbd_key_geom_t;

static int kbd_key_h(const flux_fb_t *fb) {
    int h = fb->width / 9;
    if (h > 72) h = 72;
    if (h < 40) h = 40;
    return h;
}

static int build_kbd_geom(const flux_fb_t *fb, kbd_key_geom_t *out, int max_keys) {
    int n = 0;
    int key_h = kbd_key_h(fb);
    int top = fb->height - key_h * KBD_TOTAL_ROWS;

    for (int r = 0; r < KBD_LETTER_ROWS; r++) {
        const char *row = kbd_letter_rows[r];
        int len = (int)strlen(row);
        int key_w = fb->width / len;
        int row_w = key_w * len;
        int x0 = (fb->width - row_w) / 2;
        for (int i = 0; i < len && n < max_keys; i++) {
            out[n].x = x0 + i * key_w;
            out[n].y = top + r * key_h;
            out[n].w = key_w;
            out[n].h = key_h;
            out[n].ch = row[i];
            out[n].is_backspace = 0;
            out[n].is_enter = 0;
            n++;
        }
    }

    /* Letzte Reihe: Backspace | Leertaste (breit) | Enter. */
    int last_y = top + KBD_LETTER_ROWS * key_h;
    int side_w = fb->width * 3 / 10;
    int space_w = fb->width - 2 * side_w;

    if (n < max_keys) {
        out[n] = (kbd_key_geom_t){ .x = 0, .y = last_y, .w = side_w, .h = key_h,
                                    .ch = 0, .is_backspace = 1, .is_enter = 0 };
        n++;
    }
    if (n < max_keys) {
        out[n] = (kbd_key_geom_t){ .x = side_w, .y = last_y, .w = space_w, .h = key_h,
                                    .ch = ' ', .is_backspace = 0, .is_enter = 0 };
        n++;
    }
    if (n < max_keys) {
        out[n] = (kbd_key_geom_t){ .x = side_w + space_w, .y = last_y, .w = side_w, .h = key_h,
                                    .ch = 0, .is_backspace = 0, .is_enter = 1 };
        n++;
    }
    return n;
}

int flux_ui_kbd_top(const flux_fb_t *fb) {
    return fb->height - kbd_key_h(fb) * KBD_TOTAL_ROWS;
}

int flux_ui_kbd_hit(const flux_fb_t *fb, int x, int y,
                     char *out_ch, int *out_backspace, int *out_enter) {
    kbd_key_geom_t keys[KBD_MAX_KEYS];
    int n = build_kbd_geom(fb, keys, KBD_MAX_KEYS);
    for (int i = 0; i < n; i++) {
        if (x >= keys[i].x && x < keys[i].x + keys[i].w &&
            y >= keys[i].y && y < keys[i].y + keys[i].h) {
            *out_ch = keys[i].ch;
            *out_backspace = keys[i].is_backspace;
            *out_enter = keys[i].is_enter;
            return 1;
        }
    }
    return 0;
}

static void draw_keyboard(flux_fb_t *fb) {
    kbd_key_geom_t keys[KBD_MAX_KEYS];
    int n = build_kbd_geom(fb, keys, KBD_MAX_KEYS);
    int pad = 3;

    for (int i = 0; i < n; i++) {
        int special = keys[i].is_backspace || keys[i].is_enter;
        flux_fb_fill_rect(fb, keys[i].x + pad, keys[i].y + pad,
                           keys[i].w - 2 * pad, keys[i].h - 2 * pad,
                           special ? COL_KEY_SPEC : COL_KEY);

        char label[3] = {0};
        if (keys[i].is_backspace) { label[0] = '<'; label[1] = '-'; }
        else if (keys[i].is_enter) { label[0] = 'O'; label[1] = 'K'; }
        else if (keys[i].ch != ' ') { label[0] = keys[i].ch; }

        if (label[0]) {
            int tw = flux_fb_text_width(label, 3);
            int tx = keys[i].x + (keys[i].w - tw) / 2;
            int ty = keys[i].y + (keys[i].h - 21) / 2;
            flux_fb_text(fb, tx, ty, label, COL_TEXT, 3);
        }
    }
}

/* ---- Lockscreen ----------------------------------------------------- */

void flux_ui_draw_lock(flux_fb_t *fb) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char clock_buf[16], date_buf[64];
    strftime(clock_buf, sizeof(clock_buf), "%H:%M", &tmv);
    strftime(date_buf, sizeof(date_buf), "%A, %d. %B", &tmv);

    int scale_clock = fb->width / 120;
    if (scale_clock < 5) scale_clock = 5;
    int cw = flux_fb_text_width(clock_buf, scale_clock);
    flux_fb_text(fb, (fb->width - cw) / 2, fb->height / 3, clock_buf, COL_TEXT, scale_clock);

    int dw = flux_fb_text_width(date_buf, 3);
    flux_fb_text(fb, (fb->width - dw) / 2, fb->height / 3 + scale_clock * 11, date_buf, COL_DIM, 3);

    /* Touch-first: Wischen ist die primaere Geste, Enter bleibt als
     * Fallback fuer reine Tastatur-Hardware (siehe input.c). */
    const char *hint = "Nach oben wischen zum Entsperren";
    int hw = flux_fb_text_width(hint, 3);
    flux_fb_text(fb, (fb->width - hw) / 2, fb->height - 70, hint, COL_ACCENT, 3);

    /* Kleiner Wisch-Pfeil als visueller Hinweis -- ein gefuelltes
     * Dreieck aus drei schmalen, nach oben schrumpfenden Balken. */
    int ax = fb->width / 2;
    int ay = fb->height - 32;
    for (int i = 0; i < 3; i++) {
        int w = 32 - i * 8;
        flux_fb_fill_rect(fb, ax - w / 2, ay - i * 8, w, 5, COL_ACCENT);
    }

    flux_fb_present(fb);
}

/* ---- PIN-Sperre -------------------------------------------------------
 * Optional: nur aktiv, wenn in den Einstellungen ein PIN gesetzt wurde
 * (siehe main.c). Ohne PIN entsperrt der Wisch direkt in den
 * Assistenten, genau wie vorher -- ehrlicher "noch nicht eingerichtet"
 * Default statt einer erzwungenen Huerde beim Erststart. */

typedef struct { int x, y, w, h; char digit; int is_backspace; } pin_key_geom_t;

static int build_pin_geom(const flux_fb_t *fb, pin_key_geom_t *out) {
    static const char rows[4][3] = {
        {'1','2','3'}, {'4','5','6'}, {'7','8','9'}, {0,'0','B'}
    };
    int top = fb->height * 2 / 5;
    int avail_h = fb->height - top;
    int key_h = avail_h / 4;
    int key_w = fb->width / 3;
    int n = 0;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
            char ch = rows[r][c];
            out[n].x = c * key_w;
            out[n].y = top + r * key_h;
            out[n].w = key_w;
            out[n].h = key_h;
            out[n].digit = (ch >= '0' && ch <= '9') ? ch : 0;
            out[n].is_backspace = (ch == 'B');
            n++;
        }
    }
    return n;
}

void flux_ui_draw_pin(flux_fb_t *fb, int entered, int error) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    const char *title = "Code eingeben";
    int tw = flux_fb_text_width(title, 3);
    flux_fb_text(fb, (fb->width - tw) / 2, STATUSBAR_H + 24, title, COL_ACCENT, 3);

    int dot_size = 16, gap = 36;
    int total_w = PIN_LEN * gap;
    int dx = (fb->width - total_w) / 2;
    int dy = STATUSBAR_H + 84;
    for (int i = 0; i < PIN_LEN; i++) {
        uint32_t col = (i < entered) ? COL_ACCENT : COL_DIM;
        flux_fb_fill_rect(fb, dx + i * gap, dy, dot_size, dot_size, col);
    }

    if (error) {
        const char *msg = "Falscher Code";
        int mw = flux_fb_text_width(msg, 3);
        flux_fb_text(fb, (fb->width - mw) / 2, dy + 32, msg, COL_DANGER, 3);
    }

    pin_key_geom_t keys[12];
    int n = build_pin_geom(fb, keys);
    int pad = 6;
    for (int i = 0; i < n; i++) {
        if (!keys[i].digit && !keys[i].is_backspace) continue;
        flux_fb_fill_rect(fb, keys[i].x + pad, keys[i].y + pad,
                           keys[i].w - 2 * pad, keys[i].h - 2 * pad,
                           keys[i].is_backspace ? COL_KEY_SPEC : COL_KEY);
        char label[3] = {0};
        if (keys[i].is_backspace) { label[0] = '<'; label[1] = '-'; }
        else label[0] = keys[i].digit;
        int lw = flux_fb_text_width(label, 4);
        flux_fb_text(fb, keys[i].x + (keys[i].w - lw) / 2,
                     keys[i].y + (keys[i].h - 28) / 2, label, COL_TEXT, 4);
    }
    flux_fb_present(fb);
}

int flux_ui_pin_hit(const flux_fb_t *fb, int x, int y, char *out_digit, int *out_backspace) {
    pin_key_geom_t keys[12];
    int n = build_pin_geom(fb, keys);
    for (int i = 0; i < n; i++) {
        if (!keys[i].digit && !keys[i].is_backspace) continue;
        if (x >= keys[i].x && x < keys[i].x + keys[i].w &&
            y >= keys[i].y && y < keys[i].y + keys[i].h) {
            *out_digit = keys[i].digit;
            *out_backspace = keys[i].is_backspace;
            return 1;
        }
    }
    return 0;
}

/* ---- Schnellzugriff-Leiste (nur Assistenten-Bildschirm) -------------- */

static void draw_quickrow(flux_fb_t *fb) {
    int y = STATUSBAR_H;
    int half = fb->width / 2;
    flux_fb_fill_rect(fb, 2, y + 2, half - 4, QUICKROW_H - 4, COL_KEY);
    flux_fb_fill_rect(fb, half + 2, y + 2, half - 4, QUICKROW_H - 4, COL_KEY);

    const char *l1 = "Einstellungen";
    const char *l2 = "Dateien";
    int w1 = flux_fb_text_width(l1, 2);
    int w2 = flux_fb_text_width(l2, 2);
    flux_fb_text(fb, (half - w1) / 2, y + (QUICKROW_H - 16) / 2, l1, COL_TEXT, 2);
    flux_fb_text(fb, half + (half - w2) / 2, y + (QUICKROW_H - 16) / 2, l2, COL_TEXT, 2);
}

int flux_ui_quickrow_hit(const flux_fb_t *fb, int x, int y) {
    if (y < STATUSBAR_H || y >= STATUSBAR_H + QUICKROW_H) return 0;
    return (x < fb->width / 2) ? 1 : 2;
}

/* ---- Assistenten-Bildschirm ------------------------------------------ */

static int draw_wrapped(flux_fb_t *fb, int x, int y, int max_w, const char *s,
                         uint32_t color, int scale, int line_h) {
    char line[256];
    int line_len = 0;
    int cy = y;
    line[0] = '\0';

    const char *word_start = s;
    while (1) {
        const char *word_end = word_start;
        while (*word_end && *word_end != ' ' && *word_end != '\n') word_end++;
        int wlen = (int)(word_end - word_start);

        char candidate[256];
        snprintf(candidate, sizeof(candidate), "%s%s%.*s",
                 line, line_len ? " " : "", wlen, word_start);

        if (flux_fb_text_width(candidate, scale) > max_w && line_len > 0) {
            flux_fb_text(fb, x, cy, line, color, scale);
            cy += line_h;
            line[0] = '\0';
            line_len = 0;
            snprintf(candidate, sizeof(candidate), "%.*s", wlen, word_start);
        }
        strncpy(line, candidate, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        line_len = (int)strlen(line);

        if (*word_end == '\n') {
            flux_fb_text(fb, x, cy, line, color, scale);
            cy += line_h;
            line[0] = '\0';
            line_len = 0;
            word_end++;
        }
        if (!*word_end) break;
        word_start = word_end + (*word_end == ' ' ? 1 : 0);
        if (!*word_start) break;
    }
    if (line_len > 0) {
        flux_fb_text(fb, x, cy, line, color, scale);
        cy += line_h;
    }
    return cy;
}

void flux_ui_draw_assistant(flux_fb_t *fb, const char *input, const char *answer, int thinking) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    draw_quickrow(fb);

    const char *title = "Frag Flux";
    flux_fb_text(fb, 16, STATUSBAR_H + QUICKROW_H + 16, title, COL_ACCENT, 4);

    int answer_y = STATUSBAR_H + QUICKROW_H + 64;
    if (thinking) {
        flux_fb_text(fb, 16, answer_y, "Denke nach...", COL_DIM, 3);
    } else if (answer && *answer) {
        draw_wrapped(fb, 16, answer_y, fb->width - 32, answer, COL_TEXT, 3, 34);
    } else {
        draw_wrapped(fb, 16, answer_y, fb->width - 32,
                     "Frag mich nach Uhrzeit, Akkustand, oder sag/schreib "
                     "\"Einstellungen\" bzw. \"Dateien\".", COL_DIM, 2, 26);
    }

    /* Eingabezeile direkt ueber der Bildschirmtastatur, mit
     * Mikrofon-Knopf am rechten Rand. */
    int kbd_top = flux_ui_kbd_top(fb);
    int input_y = kbd_top - INPUT_BAR_H;
    flux_fb_fill_rect(fb, 0, input_y, fb->width, INPUT_BAR_H, COL_STATUSBAR);

    int mic_x = fb->width - MIC_BTN_W;
    flux_fb_fill_rect(fb, mic_x + 4, input_y + 4, MIC_BTN_W - 8, INPUT_BAR_H - 8, COL_KEY_SPEC);
    const char *mic_label = "MIC";
    int mlw = flux_fb_text_width(mic_label, 2);
    flux_fb_text(fb, mic_x + (MIC_BTN_W - mlw) / 2, input_y + (INPUT_BAR_H - 16) / 2,
                 mic_label, COL_ACCENT, 2);

    char prompt[300];
    snprintf(prompt, sizeof(prompt), "> %s_", input);
    flux_fb_text(fb, 16, input_y + (INPUT_BAR_H - 21) / 2, prompt, COL_TEXT, 3);

    draw_keyboard(fb);

    flux_fb_present(fb);
}

int flux_ui_mic_hit(const flux_fb_t *fb, int x, int y) {
    int kbd_top = flux_ui_kbd_top(fb);
    int input_y = kbd_top - INPUT_BAR_H;
    int mic_x = fb->width - MIC_BTN_W;
    return (x >= mic_x && y >= input_y && y < input_y + INPUT_BAR_H);
}

/* ---- Bestaetigungs-Dialog (KI will Mail/SMS/Anruf ausloesen) --------
 * Das Apple-artige "bist du sicher?" aus der Aufgabenstellung: die KI
 * fuehrt nie direkt etwas aus, sie schlaegt nur vor (siehe
 * shell/src/action.c). Drei grosse Knoepfe statt eines Mini-Dialogs --
 * "alles muss groesser sein" gilt hier besonders, weil eine
 * Fehlbedienung hier tatsaechlich etwas verschickt. */

typedef struct { int x, y, w, h; } btn_geom_t;

static void build_confirm_buttons(const flux_fb_t *fb, btn_geom_t out[3]) {
    int h = 80;
    int y = fb->height - h;
    int w = fb->width / 3;
    out[0] = (btn_geom_t){ 0,     y, w, h };
    out[1] = (btn_geom_t){ w,     y, w, h };
    out[2] = (btn_geom_t){ 2 * w, y, fb->width - 2 * w, h };
}

void flux_ui_draw_confirm(flux_fb_t *fb, const char *type_label,
                           const char *to, const char *subject, const char *body) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    char title[160];
    snprintf(title, sizeof(title), "Flux moechte eine %s senden", type_label);
    flux_fb_text(fb, 16, STATUSBAR_H + 16, title, COL_ACCENT, 3);

    int y = STATUSBAR_H + 64;
    char line[300];
    snprintf(line, sizeof(line), "An: %s", to);
    flux_fb_text(fb, 16, y, line, COL_TEXT, 2);
    y += 30;
    if (subject && subject[0]) {
        snprintf(line, sizeof(line), "Betreff: %s", subject);
        flux_fb_text(fb, 16, y, line, COL_TEXT, 2);
        y += 30;
    }
    y += 10;
    draw_wrapped(fb, 16, y, fb->width - 32, body, COL_TEXT, 2, 28);

    btn_geom_t btn[3];
    build_confirm_buttons(fb, btn);
    static const char *labels[3] = { "Bearbeiten", "Abbrechen", "Senden" };
    const uint32_t cols[3] = { COL_KEY_SPEC, COL_CANCEL, COL_SEND };
    for (int i = 0; i < 3; i++) {
        flux_fb_fill_rect(fb, btn[i].x + 3, btn[i].y + 3, btn[i].w - 6, btn[i].h - 6, cols[i]);
        int tw = flux_fb_text_width(labels[i], 2);
        flux_fb_text(fb, btn[i].x + (btn[i].w - tw) / 2,
                     btn[i].y + (btn[i].h - 16) / 2, labels[i], COL_TEXT, 2);
    }
    flux_fb_present(fb);
}

flux_confirm_hit_t flux_ui_confirm_hit(const flux_fb_t *fb, int x, int y) {
    btn_geom_t btn[3];
    build_confirm_buttons(fb, btn);
    for (int i = 0; i < 3; i++) {
        if (x >= btn[i].x && x < btn[i].x + btn[i].w &&
            y >= btn[i].y && y < btn[i].y + btn[i].h) {
            switch (i) {
                case 0: return FLUX_CONFIRM_EDIT;
                case 1: return FLUX_CONFIRM_CANCEL;
                default: return FLUX_CONFIRM_SEND;
            }
        }
    }
    return FLUX_CONFIRM_NONE;
}

/* ---- Text bearbeiten (vor dem Senden einer Aktion) -------------------- */

void flux_ui_draw_edit_body(flux_fb_t *fb, const char *body) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    flux_fb_text(fb, 16, STATUSBAR_H + 16, "Text bearbeiten", COL_ACCENT, 3);

    draw_wrapped(fb, 16, STATUSBAR_H + 64, fb->width - 32, body, COL_TEXT, 2, 28);

    int kbd_top = flux_ui_kbd_top(fb);
    int input_y = kbd_top - INPUT_BAR_H;
    flux_fb_fill_rect(fb, 0, input_y, fb->width, INPUT_BAR_H, COL_STATUSBAR);
    const char *hint = "OK auf der Tastatur speichert die Aenderung";
    flux_fb_text(fb, 16, input_y + (INPUT_BAR_H - 21) / 2, hint, COL_DIM, 2);

    draw_keyboard(fb);
    flux_fb_present(fb);
}

/* ---- Gemeinsame Liste fuer Einstellungen/Dateien ---------------------- */

typedef struct { int x, y, w, h; } list_row_geom_t;

static int build_list_rows(const flux_fb_t *fb, int n, list_row_geom_t *out) {
    if (n > LIST_MAX_ROWS) n = LIST_MAX_ROWS;
    if (n < 1) return 0;
    int top = STATUSBAR_H + TITLE_AREA_H;
    int bottom = fb->height - LIST_BACK_H;
    int avail = bottom - top;
    int row_h = avail / n;
    if (row_h > 96) row_h = 96;
    if (row_h < 56) row_h = 56;
    for (int i = 0; i < n; i++) {
        out[i].x = 8;
        out[i].y = top + i * row_h;
        out[i].w = fb->width - 16;
        out[i].h = row_h - 8;
    }
    return n;
}

int flux_ui_list_hit(const flux_fb_t *fb, int x, int y, int n, int *out_index, int *out_back) {
    *out_back = 0;
    if (y >= fb->height - LIST_BACK_H) { *out_back = 1; return 1; }

    list_row_geom_t rows[LIST_MAX_ROWS];
    int rn = build_list_rows(fb, n, rows);
    for (int i = 0; i < rn; i++) {
        if (x >= rows[i].x && x < rows[i].x + rows[i].w &&
            y >= rows[i].y && y < rows[i].y + rows[i].h) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

/* ---- Einstellungen ----------------------------------------------------- */

void flux_ui_draw_settings(flux_fb_t *fb, const char **labels, const char **values, int n) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    flux_fb_text(fb, 16, STATUSBAR_H + 12, "Einstellungen", COL_ACCENT, 3);

    list_row_geom_t rows[LIST_MAX_ROWS];
    int rn = build_list_rows(fb, n, rows);
    for (int i = 0; i < rn; i++) {
        flux_fb_fill_rect(fb, rows[i].x, rows[i].y, rows[i].w, rows[i].h, COL_ROW);
        flux_fb_text(fb, rows[i].x + 12, rows[i].y + 8, labels[i], COL_TEXT, 2);
        flux_fb_text(fb, rows[i].x + 12, rows[i].y + rows[i].h - 24, values[i], COL_DIM, 2);
    }
    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}

/* ---- Dateien ------------------------------------------------------------
 * Bewusst nur ein Browser (Ordner ansehen, hinein/hinaus navigieren),
 * kein Loeschen/Umbenennen -- ein erster, sicherer Schritt; siehe
 * README-Roadmap fuer den Ausbau. */

void flux_ui_draw_files(flux_fb_t *fb, const char *path, const char **names,
                         const char **metas, int n, int truncated) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    flux_fb_text(fb, 16, STATUSBAR_H + 8, "Dateien", COL_ACCENT, 3);
    flux_fb_text(fb, 16, STATUSBAR_H + 36, path, COL_DIM, 2);
    if (truncated) {
        const char *hint = "(zeige nur die ersten Eintraege)";
        flux_fb_text(fb, 16, STATUSBAR_H + TITLE_AREA_H - 4, hint, COL_DIM, 1);
    }

    if (n == 0) {
        flux_fb_text(fb, 16, STATUSBAR_H + TITLE_AREA_H + 12, "(leer)", COL_DIM, 2);
    } else {
        list_row_geom_t rows[LIST_MAX_ROWS];
        int rn = build_list_rows(fb, n, rows);
        for (int i = 0; i < rn; i++) {
            flux_fb_fill_rect(fb, rows[i].x, rows[i].y, rows[i].w, rows[i].h, COL_ROW);
            flux_fb_text(fb, rows[i].x + 12, rows[i].y + 8, names[i], COL_TEXT, 2);
            flux_fb_text(fb, rows[i].x + 12, rows[i].y + rows[i].h - 24, metas[i], COL_DIM, 2);
        }
    }
    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}
