#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define COL_BG       0x0B0E14
#define COL_ACCENT   0x4FD1C5
#define COL_TEXT     0xE6E6E6
#define COL_DIM      0x6B7280

/* Bricht s in Zeilen um, sodass jede Zeile bei "scale" Pixelgroesse
 * in max_w Pixel passt. Schreibt direkt auf den Bildschirm statt
 * eine Kopie zu allozieren -- der Daemon-Antwortpuffer ist eh schon
 * im Stack, mehr Speicherdruck brauchen wir auf einem Telefon nicht. */
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

void flux_ui_draw_lock(flux_fb_t *fb) {
    flux_fb_clear(fb, COL_BG);

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

    const char *hint = "Enter druecken zum Entsperren";
    int hw = flux_fb_text_width(hint, 2);
    flux_fb_text(fb, (fb->width - hw) / 2, fb->height - 40, hint, COL_ACCENT, 2);

    flux_fb_present(fb);
}

void flux_ui_draw_assistant(flux_fb_t *fb, const char *input, const char *answer, int thinking) {
    flux_fb_clear(fb, COL_BG);

    const char *title = "Frag Flux";
    flux_fb_text(fb, 16, 16, title, COL_ACCENT, 3);

    int answer_y = 60;
    if (thinking) {
        flux_fb_text(fb, 16, answer_y, "Denke nach...", COL_DIM, 2);
    } else if (answer && *answer) {
        draw_wrapped(fb, 16, answer_y, fb->width - 32, answer, COL_TEXT, 2, 22);
    } else {
        flux_fb_text(fb, 16, answer_y, "Frag mich nach Uhrzeit, Akkustand, oder was sonst...",
                     COL_DIM, 2);
    }

    /* Eingabezeile am unteren Bildschirmrand, wie eine Kommandozeile. */
    int input_y = fb->height - 40;
    flux_fb_fill_rect(fb, 0, input_y - 10, fb->width, 50, 0x161B26);
    char prompt[300];
    snprintf(prompt, sizeof(prompt), "> %s_", input);
    flux_fb_text(fb, 16, input_y, prompt, COL_TEXT, 2);

    flux_fb_present(fb);
}
