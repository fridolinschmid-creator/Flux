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

/* ---- Statusleiste (oben, beide Bildschirme) ----------------------- */

static void draw_statusbar(flux_fb_t *fb) {
    flux_fb_fill_rect(fb, 0, 0, fb->width, 28, COL_STATUSBAR);

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M", &tmv);
    flux_fb_text(fb, 12, 8, buf, COL_TEXT, 2);

    const char *label = "Flux";
    int lw = flux_fb_text_width(label, 2);
    flux_fb_text(fb, fb->width - lw - 12, 8, label, COL_DIM, 2);
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
    int h = fb->width / 11;
    if (h > 56) h = 56;
    if (h < 28) h = 28;
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
            int tw = flux_fb_text_width(label, 2);
            int tx = keys[i].x + (keys[i].w - tw) / 2;
            int ty = keys[i].y + (keys[i].h - 14) / 2;
            flux_fb_text(fb, tx, ty, label, COL_TEXT, 2);
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

    int scale_clock = fb->width / 160;
    if (scale_clock < 4) scale_clock = 4;
    int cw = flux_fb_text_width(clock_buf, scale_clock);
    flux_fb_text(fb, (fb->width - cw) / 2, fb->height / 3, clock_buf, COL_TEXT, scale_clock);

    int dw = flux_fb_text_width(date_buf, 2);
    flux_fb_text(fb, (fb->width - dw) / 2, fb->height / 3 + scale_clock * 10, date_buf, COL_DIM, 2);

    /* Touch-first: Wischen ist die primaere Geste, Enter bleibt als
     * Fallback fuer reine Tastatur-Hardware (siehe input.c). */
    const char *hint = "Nach oben wischen zum Entsperren";
    int hw = flux_fb_text_width(hint, 2);
    flux_fb_text(fb, (fb->width - hw) / 2, fb->height - 60, hint, COL_ACCENT, 2);

    /* Kleiner Wisch-Pfeil als visueller Hinweis -- ein gefuelltes
     * Dreieck aus drei schmalen, nach oben schrumpfenden Balken. */
    int ax = fb->width / 2;
    int ay = fb->height - 30;
    for (int i = 0; i < 3; i++) {
        int w = 24 - i * 6;
        flux_fb_fill_rect(fb, ax - w / 2, ay - i * 6, w, 4, COL_ACCENT);
    }

    flux_fb_present(fb);
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

    const char *title = "Frag Flux";
    flux_fb_text(fb, 16, 40, title, COL_ACCENT, 3);

    int answer_y = 84;
    if (thinking) {
        flux_fb_text(fb, 16, answer_y, "Denke nach...", COL_DIM, 2);
    } else if (answer && *answer) {
        draw_wrapped(fb, 16, answer_y, fb->width - 32, answer, COL_TEXT, 2, 22);
    } else {
        flux_fb_text(fb, 16, answer_y, "Frag mich nach Uhrzeit, Akkustand, oder was sonst...",
                     COL_DIM, 2);
    }

    /* Eingabezeile direkt ueber der Bildschirmtastatur. */
    int kbd_top = flux_ui_kbd_top(fb);
    int input_bar_h = 44;
    int input_y = kbd_top - input_bar_h;
    flux_fb_fill_rect(fb, 0, input_y, fb->width, input_bar_h, COL_STATUSBAR);
    char prompt[300];
    snprintf(prompt, sizeof(prompt), "> %s_", input);
    flux_fb_text(fb, 16, input_y + 14, prompt, COL_TEXT, 2);

    draw_keyboard(fb);

    flux_fb_present(fb);
}
