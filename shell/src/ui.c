#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

/* Dynamische Akzentfarbe -- aenderbar via flux_ui_set_accent(). */
static uint32_t g_accent = 0x4FD1C5;
void flux_ui_set_accent(uint32_t rgb) { g_accent = rgb; }

#define COL_BG       0x0B0E14
#define COL_ACCENT   g_accent   /* immer den dynamischen Wert lesen */
#define COL_TEXT     0xE6E6E6
#define COL_DIM      0x6B7280
#define COL_STATUSBAR 0x161B26
#define COL_KEY      0x1F2533
#define COL_KEY_SPEC 0x29384A
#define COL_ROW          0x1A2230
#define COL_DANGER       0xE05252
#define COL_CANCEL       0xB23B3B
#define COL_SEND         0x2F9E6E
#define COL_BUBBLE_USER  0x1D7A6B  /* Nutzer-Blase: dunkles Teal */
#define COL_BUBBLE_AI    0x1A2535  /* KI-Blase: dunkelblau */
#define COL_CARD         0x1C2840  /* Karten-Hintergrund im Bestaetigungs-Dialog */
#define COL_DIVIDER      0x2A3850  /* Trennlinie */
#define CLIP_BTN_W       56        /* Breite der Kopieren-/Einfuegen-Knoepfe */

/* Layout-Hoehen -- grosszuegig fuer Touch-Bedienung (Ziel: >= 56px je
 * antippbares Element, vgl. iOS 44pt / Material 48dp auf HiDPI). */
#define STATUSBAR_H  48
#define QUICKROW_H   64
#define INPUT_BAR_H  72
#define MIC_BTN_W    INPUT_BAR_H
#define TITLE_AREA_H 72
#define LIST_BACK_H  72
#define LIST_MAX_ROWS 12
#define PIN_LEN 4

/* ---- Statusleiste (oben, fast alle Bildschirme) -------------------- */

/* Liest Batteriefuellstand aus sysfs (0-100, oder -1 wenn nicht verfuegbar). */
static int read_battery_pct(void) {
    const char *paths[] = {
        "/sys/class/power_supply/BAT0/capacity",
        "/sys/class/power_supply/BAT1/capacity",
        "/sys/class/power_supply/battery/capacity",
        "/sys/class/power_supply/BAT/capacity",
    };
    for (int i = 0; i < 4; i++) {
        FILE *f = fopen(paths[i], "r");
        if (!f) continue;
        int pct = -1;
        fscanf(f, "%d", &pct);
        fclose(f);
        if (pct >= 0) return pct;
    }
    return -1;
}

/* Liest WLAN-Linkqualitaet (0-70 typisch) aus /proc/net/wireless,
 * oder -1 wenn kein WLAN aktiv. */
static int read_wifi_quality(void) {
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) return -1;
    char line[128];
    fgets(line, sizeof(line), f); /* Header 1 */
    fgets(line, sizeof(line), f); /* Header 2 */
    int quality = -1;
    if (fgets(line, sizeof(line), f)) {
        char iface[64];
        int status, link;
        if (sscanf(line, " %63[^:]: %d %d.", iface, &status, &link) >= 3)
            quality = link;
    }
    fclose(f);
    return quality;
}

/* Zeichnet ein kleines Batterie-Symbol (20x12) bei (x,y). */
static void draw_battery_icon(flux_fb_t *fb, int x, int y, int pct) {
    /* Rahmen */
    flux_fb_fill_rect(fb, x,    y,    18, 12, 0x445566);
    flux_fb_fill_rect(fb, x+18, y+3,   2,  6, 0x445566); /* Plus-Pol */
    /* Fuellung (gruen bis 50%, gelb bis 20%, rot darunter) */
    int fill_w = 14 * pct / 100;
    if (fill_w < 1 && pct > 0) fill_w = 1;
    uint32_t col = (pct > 50) ? 0x22CC55 : (pct > 20) ? 0xFFCC00 : 0xFF4444;
    if (fill_w > 0)
        flux_fb_fill_rect(fb, x+2, y+2, fill_w, 8, col);
}

/* Zeichnet WLAN-Balken (3 Balken bei x,y). quality: 0-70 */
static void draw_wifi_icon(flux_fb_t *fb, int x, int y, int quality) {
    int bars = (quality >= 55) ? 3 : (quality >= 30) ? 2 : 1;
    uint32_t hi = COL_ACCENT, lo = 0x334455;
    flux_fb_fill_rect(fb, x,    y+8,  4, 4, bars >= 1 ? hi : lo);
    flux_fb_fill_rect(fb, x+5,  y+4,  4, 8, bars >= 2 ? hi : lo);
    flux_fb_fill_rect(fb, x+10, y,    4, 12, bars >= 3 ? hi : lo);
}

static void draw_statusbar(flux_fb_t *fb) {
    flux_fb_fill_rect(fb, 0, 0, fb->width, STATUSBAR_H, COL_STATUSBAR);

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M", &tmv);
    flux_fb_text(fb, 12, 10, buf, COL_TEXT, 3);

    /* Rechts: [Wifi] [Bat] Flux */
    int rx = fb->width - 12;

    const char *label = "Flux";
    int lw = flux_fb_text_width(label, 2);
    rx -= lw;
    flux_fb_text(fb, rx, 12, label, COL_DIM, 2);
    rx -= 8;

    int bat = read_battery_pct();
    if (bat >= 0) {
        rx -= 20;
        draw_battery_icon(fb, rx, (STATUSBAR_H - 12) / 2, bat);
        rx -= 6;
        /* Prozentzahl */
        char pbuf[8];
        snprintf(pbuf, sizeof(pbuf), "%d%%", bat);
        int pw = flux_fb_text_width(pbuf, 2);
        rx -= pw;
        flux_fb_text(fb, rx, 12, pbuf, COL_DIM, 2);
        rx -= 8;
    }

    int wifi = read_wifi_quality();
    if (wifi >= 0) {
        rx -= 14;
        draw_wifi_icon(fb, rx, (STATUSBAR_H - 12) / 2, wifi);
        rx -= 6;
    }
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
    int h = fb->width / 8;   /* etwas hoehere Tasten fuer den Daumen */
    if (h > 84) h = 84;
    if (h < 54) h = 54;
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
    int hw = flux_fb_text_width(hint, 2);
    flux_fb_text(fb, (fb->width - hw) / 2, fb->height - 66, hint, COL_ACCENT, 2);

    /* Kleiner Wisch-Pfeil als visueller Hinweis -- ein gefuelltes
     * Dreieck aus drei schmalen, nach oben schrumpfenden Balken. */
    int ax = fb->width / 2;
    int ay = fb->height - 32;
    for (int i = 0; i < 3; i++) {
        int w = 32 - i * 8;
        flux_fb_fill_rect(fb, ax - w / 2, ay - i * 8, w, 5, COL_ACCENT);
    }

    /* Wetter-Info aus Cache (falls vorhanden) */
    {
        char wline[160] = {0};
        FILE *wf = fopen("/tmp/flux_weather.txt", "r");
        if (wf) { if (!fgets(wline, sizeof(wline), wf)) wline[0] = '\0'; fclose(wf); }
        size_t wl = strlen(wline);
        while (wl > 0 && (wline[wl-1] == '\n' || wline[wl-1] == '\r')) wline[--wl] = '\0';
        if (wline[0]) {
            int ww = flux_fb_text_width(wline, 2);
            if (ww > fb->width - 24) ww = fb->width - 24;
            flux_fb_text(fb, (fb->width - flux_fb_text_width(wline, 2)) / 2,
                         fb->height / 3 + scale_clock * 11 + 30, wline, COL_DIM, 2);
        }
    }
    /* Begruessung (von KI generiert, falls /tmp/flux_greeting.txt vorhanden) */
    {
        char gline[120] = {0};
        FILE *gf = fopen("/tmp/flux_greeting.txt", "r");
        if (gf) { if (!fgets(gline, sizeof(gline), gf)) gline[0] = '\0'; fclose(gf); }
        size_t gl = strlen(gline);
        while (gl > 0 && (gline[gl-1] == '\n' || gline[gl-1] == '\r')) gline[--gl] = '\0';
        if (gline[0]) {
            int gw = flux_fb_text_width(gline, 2);
            flux_fb_text(fb, (fb->width - gw) / 2,
                         fb->height / 3 + scale_clock * 11 + 56, gline, COL_ACCENT, 2);
        }
    }
    /* Proaktive KI-Benachrichtigung (von fluxaid generiert) */
    {
        char pline[512] = {0};
        FILE *pf = fopen("/tmp/flux_proactive.txt", "r");
        if (pf) { size_t pn = fread(pline, 1, sizeof(pline)-1, pf); pline[pn] = '\0'; fclose(pf); }
        size_t pl = strlen(pline);
        while (pl > 0 && (pline[pl-1] == '\n' || pline[pl-1] == '\r')) pline[--pl] = '\0';
        if (pline[0]) {
            /* Notification card: rounded rect with accent-colored top stripe */
            int card_m = 20;
            int card_x = card_m;
            int card_w = fb->width - 2 * card_m;
            int card_y = fb->height * 2 / 3 - 10;
            int card_h = 100;
            flux_fb_fill_rect(fb, card_x, card_y, card_w, card_h, 0x1A2535);
            flux_fb_fill_rect(fb, card_x, card_y, card_w, 3, g_accent);
            /* KI icon label */
            flux_fb_text(fb, card_x + 10, card_y + 8, "KI-Hinweis", g_accent, 2);
            /* Message text: wrap at ~38 chars */
            const char *p = pline;
            int ty = card_y + 26;
            while (*p && ty < card_y + card_h - 10) {
                char lbuf[48]; int ll = 0;
                while (p[ll] && p[ll] != '\n' && ll < 42) ll++;
                /* Word-wrap: back up to last space if line too long */
                if (ll == 42 && p[ll] && p[ll] != ' ') {
                    int sw = ll;
                    while (sw > 20 && p[sw] != ' ') sw--;
                    if (p[sw] == ' ') ll = sw;
                }
                memcpy(lbuf, p, (size_t)ll); lbuf[ll] = '\0';
                flux_fb_text(fb, card_x + 10, ty, lbuf, COL_TEXT, 2);
                ty += 18;
                p += ll;
                if (*p == ' ' || *p == '\n') p++;
            }
        }
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
    int bw = fb->width / 4;
    static const char *labels[4] = { "Einst.", "Dateien", "Kalender", "Kontakte" };
    for (int i = 0; i < 4; i++) {
        int bx = i * bw;
        flux_fb_fill_rect(fb, bx + 2, y + 2, bw - 4, QUICKROW_H - 4, COL_KEY);
        int tw = flux_fb_text_width(labels[i], 2);
        flux_fb_text(fb, bx + (bw - tw) / 2, y + (QUICKROW_H - 16) / 2, labels[i], COL_TEXT, 2);
    }
}

int flux_ui_quickrow_hit(const flux_fb_t *fb, int x, int y) {
    if (y < STATUSBAR_H || y >= STATUSBAR_H + QUICKROW_H) return 0;
    int bw = fb->width / 4;
    return (x / bw) + 1; /* 1=Einst, 2=Dateien, 3=Kalender, 4=Kontakte */
}

/* ---- Wetter-Widget ---------------------------------------------------- */

#define WEATHER_BAR_H  52
#define WEATHER_CACHE  "/tmp/flux_weather.txt"

/* Pixel-Art-Ikone (28x28) fuer verschiedene Wetterbedingungen */
typedef enum {
    WCOND_SUNNY = 0,
    WCOND_PARTLY_CLOUDY,
    WCOND_CLOUDY,
    WCOND_RAINY,
    WCOND_SNOWY,
    WCOND_STORMY,
    WCOND_FOGGY,
    WCOND_UNKNOWN,
} weather_cond_t;

static weather_cond_t classify_weather(const char *desc) {
    /* Entscheidet anhand von Schlagworten im ASCII-Beschreibungstext */
    char low[256];
    int i;
    for (i = 0; desc[i] && i < 255; i++)
        low[i] = (desc[i] >= 'A' && desc[i] <= 'Z') ? desc[i] + 32 : desc[i];
    low[i] = '\0';
    if (strstr(low, "thunder") || strstr(low, "storm") || strstr(low, "gewitter"))
        return WCOND_STORMY;
    if (strstr(low, "snow")   || strstr(low, "sleet") || strstr(low, "schnee"))
        return WCOND_SNOWY;
    if (strstr(low, "rain")   || strstr(low, "drizzle") || strstr(low, "regen"))
        return WCOND_RAINY;
    if (strstr(low, "fog")    || strstr(low, "mist") || strstr(low, "nebel"))
        return WCOND_FOGGY;
    if (strstr(low, "overcast") || strstr(low, "bedeckt"))
        return WCOND_CLOUDY;
    if (strstr(low, "cloud") || strstr(low, "wolke") || strstr(low, "partly"))
        return WCOND_PARTLY_CLOUDY;
    if (strstr(low, "sun") || strstr(low, "clear") || strstr(low, "sonne") || strstr(low, "klar"))
        return WCOND_SUNNY;
    return WCOND_UNKNOWN;
}

/* Zeichnet eine 28x28 Wetter-Pixel-Ikone bei (ox,oy). */
static void draw_weather_icon(flux_fb_t *fb, int ox, int oy, weather_cond_t cond) {
    /* Farben */
    uint32_t sun    = 0xFFDD44;
    uint32_t cloud  = 0xA8B8C8;
    uint32_t rain   = 0x5599DD;
    uint32_t snow   = 0xDDEEFF;
    uint32_t storm  = 0xDD9922;
    uint32_t fog    = 0x889AA8;
    (void)fog;

    switch (cond) {
    case WCOND_SUNNY:
        /* Sonne: Kreis (approximiert) + Strahlen */
        flux_fb_fill_rect(fb, ox+10, oy+4,  8, 2, sun);  /* oben */
        flux_fb_fill_rect(fb, ox+10, oy+22, 8, 2, sun);  /* unten */
        flux_fb_fill_rect(fb, ox+4,  oy+10, 2, 8, sun);  /* links */
        flux_fb_fill_rect(fb, ox+22, oy+10, 2, 8, sun);  /* rechts */
        flux_fb_fill_rect(fb, ox+6,  oy+6,  2, 2, sun);  /* diag TL */
        flux_fb_fill_rect(fb, ox+20, oy+6,  2, 2, sun);  /* diag TR */
        flux_fb_fill_rect(fb, ox+6,  oy+20, 2, 2, sun);  /* diag BL */
        flux_fb_fill_rect(fb, ox+20, oy+20, 2, 2, sun);  /* diag BR */
        /* Kern */
        flux_fb_fill_rect(fb, ox+8,  oy+10, 12, 8, sun);
        flux_fb_fill_rect(fb, ox+10, oy+8,  8,  12, sun);
        break;
    case WCOND_PARTLY_CLOUDY:
        /* Sonne halb verdeckt */
        flux_fb_fill_rect(fb, ox+14, oy+2,  6, 2, sun);
        flux_fb_fill_rect(fb, ox+20, oy+8,  2, 6, sun);
        flux_fb_fill_rect(fb, ox+16, oy+2,  4, 8, sun);
        flux_fb_fill_rect(fb, ox+14, oy+4,  8, 6, sun);
        /* Wolke vorne */
        flux_fb_fill_rect(fb, ox+2,  oy+12, 18, 8, cloud);
        flux_fb_fill_rect(fb, ox+4,  oy+10, 10, 4, cloud);
        flux_fb_fill_rect(fb, ox+12, oy+9,  6,  4, cloud);
        break;
    case WCOND_CLOUDY:
        flux_fb_fill_rect(fb, ox+4,  oy+12, 20, 10, cloud);
        flux_fb_fill_rect(fb, ox+6,  oy+9,  12, 5,  cloud);
        flux_fb_fill_rect(fb, ox+14, oy+8,  8,  5,  cloud);
        break;
    case WCOND_RAINY:
        /* Wolke + Regentropfen */
        flux_fb_fill_rect(fb, ox+4,  oy+8,  20, 9,  cloud);
        flux_fb_fill_rect(fb, ox+6,  oy+6,  12, 4,  cloud);
        flux_fb_fill_rect(fb, ox+14, oy+5,  8,  4,  cloud);
        flux_fb_fill_rect(fb, ox+6,  oy+19, 2,  5,  rain);
        flux_fb_fill_rect(fb, ox+11, oy+20, 2,  5,  rain);
        flux_fb_fill_rect(fb, ox+16, oy+19, 2,  5,  rain);
        flux_fb_fill_rect(fb, ox+21, oy+20, 2,  5,  rain);
        break;
    case WCOND_SNOWY:
        /* Wolke + Schnee-Punkte */
        flux_fb_fill_rect(fb, ox+4,  oy+8,  20, 9,  cloud);
        flux_fb_fill_rect(fb, ox+6,  oy+6,  12, 4,  cloud);
        flux_fb_fill_rect(fb, ox+14, oy+5,  8,  4,  cloud);
        flux_fb_fill_rect(fb, ox+6,  oy+19, 3,  3,  snow);
        flux_fb_fill_rect(fb, ox+12, oy+20, 3,  3,  snow);
        flux_fb_fill_rect(fb, ox+18, oy+19, 3,  3,  snow);
        break;
    case WCOND_STORMY:
        /* Dunkle Wolke + Blitz */
        flux_fb_fill_rect(fb, ox+4,  oy+6,  20, 10, 0x445566);
        flux_fb_fill_rect(fb, ox+6,  oy+4,  12, 4,  0x445566);
        /* Blitz */
        flux_fb_fill_rect(fb, ox+12, oy+17, 5,  2,  storm);
        flux_fb_fill_rect(fb, ox+10, oy+19, 8,  2,  storm);
        flux_fb_fill_rect(fb, ox+8,  oy+21, 5,  4,  storm);
        break;
    case WCOND_FOGGY:
        /* Horizontale Nebel-Linien */
        flux_fb_fill_rect(fb, ox+2,  oy+8,  24, 3, 0x778899);
        flux_fb_fill_rect(fb, ox+4,  oy+13, 20, 3, 0x889AAA);
        flux_fb_fill_rect(fb, ox+2,  oy+18, 24, 3, 0x778899);
        break;
    case WCOND_UNKNOWN:
    default:
        flux_fb_fill_rect(fb, ox+8,  oy+12, 12, 4, COL_DIM);
        break;
    }
}

/* Liest den Wetter-Cache und zeichnet die Leiste.
 * Gibt die y-Koordinate unterhalb der Leiste zurueck. */
static int draw_weather_bar(flux_fb_t *fb, int y) {
    char line[256];
    line[0] = '\0';

    FILE *f = fopen(WEATHER_CACHE, "r");
    if (f) {
        if (!fgets(line, sizeof(line), f)) line[0] = '\0';
        /* trailing newline entfernen */
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) { line[--l] = '\0'; }
        fclose(f);
    }

    if (!line[0]) return y; /* kein Cache -- Leiste weglassen */

    flux_fb_fill_rect(fb, 0, y, fb->width, WEATHER_BAR_H, 0x0D1420);
    /* Trennlinie oben */
    flux_fb_fill_rect(fb, 0, y, fb->width, 1, COL_DIVIDER);

    weather_cond_t cond = classify_weather(line);
    draw_weather_icon(fb, 6, y + (WEATHER_BAR_H - 28) / 2, cond);

    /* Text rechts neben Ikone */
    flux_fb_text(fb, 42, y + (WEATHER_BAR_H - 16) / 2, line, COL_DIM, 2);

    return y + WEATHER_BAR_H;
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

/* ---- Nachrichtenblasen (Nutzer rechts, KI links) -------------------- */

static int measure_wrapped_height(int max_w, const char *s, int scale, int line_h) {
    char line[256];
    int line_len = 0, lines = 0;
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
            lines++;
            line[0] = '\0'; line_len = 0;
            snprintf(candidate, sizeof(candidate), "%.*s", wlen, word_start);
        }
        strncpy(line, candidate, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        line_len = (int)strlen(line);
        if (*word_end == '\n') { lines++; line[0] = '\0'; line_len = 0; word_end++; }
        if (!*word_end) break;
        word_start = word_end + (*word_end == ' ' ? 1 : 0);
        if (!*word_start) break;
    }
    if (line_len > 0) lines++;
    return lines * line_h;
}

#define BUBBLE_PAD_X 14
#define BUBBLE_PAD_Y  9

/* Zeichnet eine Nachrichtenblase, gibt den y-Wert direkt unter der Blase
 * plus 8px Abstand zurueck (fuer die naechste Blase). */
static int draw_bubble(flux_fb_t *fb, const char *text, int y,
                        int max_w, uint32_t col, int scale, int line_h,
                        int right_align) {
    int text_w = max_w - 2 * BUBBLE_PAD_X;
    int text_h = measure_wrapped_height(text_w, text, scale, line_h);
    if (text_h <= 0) text_h = line_h;
    int bubble_h = text_h + 2 * BUBBLE_PAD_Y;
    int bx = right_align ? (fb->width - max_w - 10) : 10;
    flux_fb_fill_rect(fb, bx, y, max_w, bubble_h, col);
    draw_wrapped(fb, bx + BUBBLE_PAD_X, y + BUBBLE_PAD_Y, text_w,
                 text, COL_TEXT, scale, line_h);
    return y + bubble_h + 8;
}

/* ---- Hilfsfunktion: Eingabeleiste zeichnen (Assistent + Bearbeiten) -- */

static void draw_input_bar(flux_fb_t *fb, int input_y, const char *prompt_text,
                             int show_mic) {
    flux_fb_fill_rect(fb, 0, input_y, fb->width, INPUT_BAR_H, COL_STATUSBAR);

    /* Rechts: MIC-Knopf (Assistent) oder OK-Knopf (Bearbeiten) */
    int mic_x = fb->width - MIC_BTN_W;
    flux_fb_fill_rect(fb, mic_x + 4, input_y + 4, MIC_BTN_W - 8, INPUT_BAR_H - 8,
                       COL_KEY_SPEC);
    const char *mic_label = show_mic ? "MIC" : "OK";
    int mlw = flux_fb_text_width(mic_label, 2);
    flux_fb_text(fb, mic_x + (MIC_BTN_W - mlw) / 2,
                 input_y + (INPUT_BAR_H - 16) / 2, mic_label, COL_ACCENT, 2);

    /* Links vom MIC: [V] Einfuegen */
    int paste_x = mic_x - CLIP_BTN_W;
    flux_fb_fill_rect(fb, paste_x + 3, input_y + 4, CLIP_BTN_W - 6, INPUT_BAR_H - 8, COL_KEY);
    int pw = flux_fb_text_width("V", 2);
    flux_fb_text(fb, paste_x + (CLIP_BTN_W - pw) / 2,
                 input_y + (INPUT_BAR_H - 16) / 2, "V", COL_DIM, 2);

    /* Links vom Einfuegen: [C] Kopieren */
    int copy_x = paste_x - CLIP_BTN_W;
    flux_fb_fill_rect(fb, copy_x + 3, input_y + 4, CLIP_BTN_W - 6, INPUT_BAR_H - 8, COL_KEY);
    int cw = flux_fb_text_width("C", 2);
    flux_fb_text(fb, copy_x + (CLIP_BTN_W - cw) / 2,
                 input_y + (INPUT_BAR_H - 16) / 2, "C", COL_DIM, 2);

    /* Eingabetext links -- auf den Platz vor den Knoepfen beschneiden und
     * das ENDE zeigen (mitlaufender Cursor), damit nichts ueberlappt. */
    if (prompt_text && prompt_text[0]) {
        int avail = copy_x - 12 - 6;
        const char *s = prompt_text;
        while (*s && flux_fb_text_width(s, 3) > avail) s++;
        flux_fb_text(fb, 12, input_y + (INPUT_BAR_H - 21) / 2, s, COL_TEXT, 3);
    }
}

void flux_ui_draw_assistant(flux_fb_t *fb, const char *last_q,
                              const char *input, const char *answer, int thinking) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    draw_quickrow(fb);

    int kbd_top   = flux_ui_kbd_top(fb);
    int input_y   = kbd_top - INPUT_BAR_H;
    /* Wetter-Widget (nur wenn Cache-Datei vorhanden) */
    int weather_end = draw_weather_bar(fb, STATUSBAR_H + QUICKROW_H);
    int chat_top  = weather_end + 8;

    int bubble_max_w = fb->width * 3 / 4;
    int cy = chat_top;

    int has_q = (last_q && *last_q);
    int has_a = (answer && *answer);

    if (!has_q && !thinking && !has_a) {
        /* Leerer Zustand: kleine Kopfzeile + Tipp-Hinweis */
        flux_fb_text(fb, 16, cy, "Frag Flux", COL_ACCENT, 3);
        cy += 36;
        draw_wrapped(fb, 16, cy, fb->width - 32,
                     "Tippe deine Frage. Sage \"Einstellungen\" oder "
                     "\"Dateien\" fuer direkten Zugriff.",
                     COL_DIM, 2, 26);
    } else {
        /* Nutzer-Blase rechts (gruen-teal) */
        if (has_q)
            cy = draw_bubble(fb, last_q, cy, bubble_max_w,
                              COL_BUBBLE_USER, 2, 24, 1);

        /* KI-Blase links (dunkelblau) oder Lade-Animation */
        if (thinking) {
            /* Animierter Denke-Indikator: Blase mit 3 Springpunkten */
            flux_fb_fill_rect(fb, 10, cy, bubble_max_w, 52, COL_BUBBLE_AI);
            flux_fb_text(fb, 10 + BUBBLE_PAD_X, cy + 8, "Flux denkt nach", COL_DIM, 2);
            int ddx = 10 + BUBBLE_PAD_X;
            int dot_y_off[] = {0, -6, -3}; /* verschiedene Hoehen = Bewegung */
            for (int d = 0; d < 3; d++) {
                int dy_dot = cy + 30 + dot_y_off[d];
                flux_fb_fill_rect(fb, ddx + d * 14, dy_dot, 8, 8, COL_ACCENT);
            }
        } else if (has_a) {
            draw_bubble(fb, answer, cy, bubble_max_w, COL_BUBBLE_AI, 2, 24, 0);
        }
    }

    /* Eingabeleiste mit [C] [V] [MIC] */
    char prompt[300];
    snprintf(prompt, sizeof(prompt), "> %s_", input);
    draw_input_bar(fb, input_y, prompt, 1);

    draw_keyboard(fb);
    flux_fb_present(fb);
}

int flux_ui_mic_hit(const flux_fb_t *fb, int x, int y) {
    int kbd_top = flux_ui_kbd_top(fb);
    int input_y = kbd_top - INPUT_BAR_H;
    int mic_x = fb->width - MIC_BTN_W;
    return (x >= mic_x && y >= input_y && y < input_y + INPUT_BAR_H);
}

int flux_ui_copy_hit(const flux_fb_t *fb, int x, int y) {
    int kbd_top  = flux_ui_kbd_top(fb);
    int input_y  = kbd_top - INPUT_BAR_H;
    int mic_x    = fb->width - MIC_BTN_W;
    int paste_x  = mic_x  - CLIP_BTN_W;
    int copy_x   = paste_x - CLIP_BTN_W;
    return (x >= copy_x && x < copy_x + CLIP_BTN_W &&
            y >= input_y && y < input_y + INPUT_BAR_H);
}

int flux_ui_paste_hit(const flux_fb_t *fb, int x, int y) {
    int kbd_top  = flux_ui_kbd_top(fb);
    int input_y  = kbd_top - INPUT_BAR_H;
    int mic_x    = fb->width - MIC_BTN_W;
    int paste_x  = mic_x - CLIP_BTN_W;
    return (x >= paste_x && x < paste_x + CLIP_BTN_W &&
            y >= input_y && y < input_y + INPUT_BAR_H);
}

/* ---- Bestaetigungs-Dialog (KI will Mail/SMS/Anruf ausloesen) --------
 * Das Apple-artige "bist du sicher?" aus der Aufgabenstellung: die KI
 * fuehrt nie direkt etwas aus, sie schlaegt nur vor (siehe
 * shell/src/action.c). Kein "Bearbeiten"-Knopf -- man tippt direkt auf
 * die Zeile (Empfaenger/Betreff/Nachricht), die man aendern will. Unten
 * nur zwei grosse Knoepfe mit Symbol: Abbrechen (X) und Senden (Pfeil). */

typedef struct { int x, y, w, h; } btn_geom_t;

/* Gefuelltes Rechteck mit abgerundeten Ecken (radius r). */
static void fill_round_rect(flux_fb_t *fb, int x, int y, int w, int h,
                            int r, uint32_t col) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int dx = (i < r) ? (r - i) : (i >= w - r) ? (i - (w - r - 1)) : 0;
            int dy = (j < r) ? (r - j) : (j >= h - r) ? (j - (h - r - 1)) : 0;
            if (dx && dy && dx * dx + dy * dy > r * r) continue;
            flux_fb_set_px(fb, x + i, y + j, col);
        }
    }
}

/* Dicke Linie (Bresenham mit Strichbreite t). */
static void draw_thick_line(flux_fb_t *fb, int x0, int y0, int x1, int y1,
                            int t, uint32_t col) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int h = t / 2;
    for (;;) {
        flux_fb_fill_rect(fb, x0 - h, y0 - h, t, t, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Nach rechts zeigendes gefuelltes Dreieck (Sende-Pfeil), Spitze rechts. */
static void draw_send_arrow(flux_fb_t *fb, int cx, int cy, int s, uint32_t col) {
    for (int i = 0; i < s; i++) {
        int hh = (s - i) * 7 / 10;       /* Hoehe nimmt zur Spitze ab */
        flux_fb_fill_rect(fb, cx - s / 2 + i, cy - hh, 2, 2 * hh, col);
    }
    /* kleiner Schaft links fuer Papierflieger-Anmutung */
    flux_fb_fill_rect(fb, cx - s / 2 - s / 3, cy - 1, s / 3, 3, col);
}

/* Symbol-Knopf unten: abgerundete Pille + Icon + Beschriftung. */
static void draw_action_button(flux_fb_t *fb, btn_geom_t b, uint32_t col,
                               const char *label, int is_send) {
    int m = 8; /* Aussenabstand zwischen den Knoepfen */
    int bx = b.x + m, by = b.y + m / 2;
    int bw = b.w - 2 * m, bh = b.h - m;
    fill_round_rect(fb, bx, by, bw, bh, 18, col);

    int icx = bx + bw / 2;
    int icy = by + bh / 2 - 8;
    if (is_send) {
        draw_send_arrow(fb, icx + 2, icy, 22, COL_TEXT);
    } else {
        draw_thick_line(fb, icx - 9, icy - 9, icx + 9, icy + 9, 4, COL_TEXT);
        draw_thick_line(fb, icx + 9, icy - 9, icx - 9, icy + 9, 4, COL_TEXT);
    }
    int tw = flux_fb_text_width(label, 2);
    flux_fb_text(fb, bx + (bw - tw) / 2, by + bh - 24, label, COL_TEXT, 2);
}

static void build_confirm_buttons(const flux_fb_t *fb, btn_geom_t out[2]) {
    int h = 92;
    int y = fb->height - h;
    int w = fb->width / 2;
    out[0] = (btn_geom_t){ 0, y, w, h };               /* Abbrechen */
    out[1] = (btn_geom_t){ w, y, fb->width - w, h };   /* Senden */
}

/* Gemeinsame Geometrie der antippbaren Zeilen -- von Draw UND Hit genutzt,
 * damit beide nie auseinanderlaufen. */
static void confirm_layout(const flux_fb_t *fb, int has_subject,
                           int *card_x, int *card_y, int *card_w, int *card_h,
                           int *to_y, int *subj_y, int *div_y, int *body_y) {
    btn_geom_t btn[2];
    build_confirm_buttons(fb, btn);
    *card_x = 10;
    *card_y = STATUSBAR_H + 78;
    *card_w = fb->width - 2 * (*card_x);
    *card_h = btn[0].y - *card_y - 8;
    *to_y   = *card_y + 14;
    *subj_y = *to_y + 36;
    *div_y  = (has_subject ? *subj_y : *to_y) + 36;
    *body_y = *div_y + 12;
}

void flux_ui_draw_confirm(flux_fb_t *fb, const char *type_label,
                           const char *to, const char *subject, const char *body) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    int has_subject = (subject && subject[0]) ? 1 : 0;

    /* Kopfzeile + dezenter Hinweis */
    char header[80];
    snprintf(header, sizeof(header), "%s senden?", type_label);
    int hw = flux_fb_text_width(header, 4);
    flux_fb_text(fb, (fb->width - hw) / 2, STATUSBAR_H + 12, header, COL_ACCENT, 4);
    const char *hint = "Tippe auf eine Zeile zum Aendern";
    int hiw = flux_fb_text_width(hint, 2);
    flux_fb_text(fb, (fb->width - hiw) / 2, STATUSBAR_H + 52, hint, COL_DIM, 2);

    int card_x, card_y, card_w, card_h, to_y, subj_y, div_y, body_y;
    confirm_layout(fb, has_subject, &card_x, &card_y, &card_w, &card_h,
                   &to_y, &subj_y, &div_y, &body_y);
    fill_round_rect(fb, card_x, card_y, card_w, card_h, 14, COL_CARD);

    int cx = card_x + 14;

    /* An: -- antippbar */
    flux_fb_text(fb, cx, to_y, "An:", COL_DIM, 2);
    flux_fb_text(fb, cx + flux_fb_text_width("An: ", 2), to_y,
                 to[0] ? to : "(tippen)", COL_ACCENT, 2);

    /* Betreff: (nur Mail) -- antippbar */
    if (has_subject) {
        flux_fb_text(fb, cx, subj_y, "Betreff:", COL_DIM, 2);
        flux_fb_text(fb, cx + flux_fb_text_width("Betreff: ", 2), subj_y,
                     subject, COL_TEXT, 2);
    }

    /* Trennlinie + Nachrichtentext -- antippbar */
    flux_fb_fill_rect(fb, cx, div_y, card_w - 28, 2, COL_DIVIDER);
    draw_wrapped(fb, cx, body_y, card_w - 28, body[0] ? body : "(tippen)",
                 COL_TEXT, 2, 26);

    /* Zwei Symbol-Knoepfe unten */
    btn_geom_t btn[2];
    build_confirm_buttons(fb, btn);
    draw_action_button(fb, btn[0], COL_CANCEL, "Abbrechen", 0);
    draw_action_button(fb, btn[1], COL_SEND,   "Senden",    1);

    flux_fb_present(fb);
}

flux_confirm_hit_t flux_ui_confirm_hit(const flux_fb_t *fb, int x, int y, int has_subject) {
    btn_geom_t btn[2];
    build_confirm_buttons(fb, btn);
    if (y >= btn[0].y) {
        return (x < fb->width / 2) ? FLUX_CONFIRM_CANCEL : FLUX_CONFIRM_SEND;
    }

    int card_x, card_y, card_w, card_h, to_y, subj_y, div_y, body_y;
    confirm_layout(fb, has_subject, &card_x, &card_y, &card_w, &card_h,
                   &to_y, &subj_y, &div_y, &body_y);
    if (x < card_x || x >= card_x + card_w) return FLUX_CONFIRM_NONE;

    if (y >= to_y - 8 && y < to_y + 28) return FLUX_CONFIRM_EDIT_TO;
    if (has_subject && y >= subj_y - 8 && y < subj_y + 28)
        return FLUX_CONFIRM_EDIT_SUBJECT;
    if (y >= body_y - 8 && y < card_y + card_h) return FLUX_CONFIRM_EDIT_BODY;
    return FLUX_CONFIRM_NONE;
}

/* ---- Text bearbeiten (vor dem Senden einer Aktion) -------------------- */

/* Titel der Bearbeiten-Maske -- vom Aufrufer setzbar (z.B. "App-Passwort"),
 * Standard "Text bearbeiten". */
static char s_edit_title[64] = "Text bearbeiten";

void flux_ui_set_edit_title(const char *title) {
    if (title && title[0]) snprintf(s_edit_title, sizeof(s_edit_title), "%s", title);
    else snprintf(s_edit_title, sizeof(s_edit_title), "Text bearbeiten");
}

void flux_ui_draw_edit_body(flux_fb_t *fb, const char *body) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    flux_fb_text(fb, 16, STATUSBAR_H + 16, s_edit_title, COL_ACCENT, 3);

    /* Text als bearbeitbare Blase (hellerer Hintergrund = aktiv) */
    int text_y = STATUSBAR_H + 64;
    int kbd_top = flux_ui_kbd_top(fb);
    int input_y = kbd_top - INPUT_BAR_H;
    int text_h  = input_y - text_y - 8;
    if (text_h > 0)
        flux_fb_fill_rect(fb, 8, text_y, fb->width - 16, text_h, COL_CARD);
    draw_wrapped(fb, 22, text_y + 10, fb->width - 44, body, COL_TEXT, 2, 28);

    /* Eingabeleiste: [C] [V] [OK] -- OK speichert/schliesst */
    draw_input_bar(fb, input_y, NULL, 0);

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
    if (row_h < 64) row_h = 64;
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

/* ---- WLAN -------------------------------------------------------------- */

void flux_ui_draw_wifi(flux_fb_t *fb, const char *current, const char **names,
                       const char **metas, int n, int scanning, int unavailable) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    flux_fb_text(fb, 16, STATUSBAR_H + 12, "WLAN", COL_ACCENT, 3);

    /* Statuszeile: aktuell verbunden? */
    char status[96];
    if (current && current[0]) snprintf(status, sizeof(status), "Verbunden: %s", current);
    else snprintf(status, sizeof(status), "Nicht verbunden");
    flux_fb_text(fb, 16, STATUSBAR_H + 44, status,
                 (current && current[0]) ? COL_ACCENT : COL_DIM, 2);

    if (unavailable) {
        draw_wrapped(fb, 16, STATUSBAR_H + TITLE_AREA_H + 8, fb->width - 32,
                     "Kein WLAN-Geraet erkannt (oder wpa_cli fehlt). Auf echter "
                     "Hardware mit WLAN-Chip erscheinen hier die Netze.",
                     COL_DIM, 2, 26);
        draw_back_bar(fb, "Zurueck");
        flux_fb_present(fb);
        return;
    }
    if (scanning) {
        flux_fb_text(fb, 16, STATUSBAR_H + TITLE_AREA_H + 8, "Suche Netze ...",
                     COL_TEXT, 2);
        draw_back_bar(fb, "Zurueck");
        flux_fb_present(fb);
        return;
    }
    if (n == 0) {
        flux_fb_text(fb, 16, STATUSBAR_H + TITLE_AREA_H + 8,
                     "Keine Netze gefunden. Erneut tippen zum Aktualisieren.",
                     COL_DIM, 2);
        draw_back_bar(fb, "Aktualisieren");
        flux_fb_present(fb);
        return;
    }

    list_row_geom_t rows[LIST_MAX_ROWS];
    int rn = build_list_rows(fb, n, rows);
    for (int i = 0; i < rn; i++) {
        flux_fb_fill_rect(fb, rows[i].x, rows[i].y, rows[i].w, rows[i].h, COL_ROW);
        flux_fb_text(fb, rows[i].x + 12, rows[i].y + 8, names[i], COL_TEXT, 2);
        flux_fb_text(fb, rows[i].x + 12, rows[i].y + rows[i].h - 24, metas[i], COL_DIM, 2);
        /* aktuell verbundenes Netz markieren */
        if (current && current[0] && strcmp(names[i], current) == 0)
            flux_fb_fill_rect(fb, rows[i].x, rows[i].y, 4, rows[i].h, COL_ACCENT);
    }
    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}

/* ---- Dateien ------------------------------------------------------------ */

#define FILES_DELETE_BTN_H  56
#define FILES_DELETE_BTN_W  120

void flux_ui_draw_files(flux_fb_t *fb, const char *path, const char **names,
                         const char **metas, int n, int truncated, int selected_idx) {
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
            uint32_t row_col = (i == selected_idx) ? 0x1E3A5A : COL_ROW;
            flux_fb_fill_rect(fb, rows[i].x, rows[i].y, rows[i].w, rows[i].h, row_col);
            if (i == selected_idx)
                flux_fb_fill_rect(fb, rows[i].x, rows[i].y, 4, rows[i].h, COL_ACCENT);
            flux_fb_text(fb, rows[i].x + 12, rows[i].y + 8, names[i], COL_TEXT, 2);
            flux_fb_text(fb, rows[i].x + 12, rows[i].y + rows[i].h - 24, metas[i], COL_DIM, 2);
        }
    }

    /* Loeschen-Knopf (nur sichtbar wenn ein nicht-Ordner ausgewaehlt) */
    if (selected_idx >= 0 && selected_idx < n) {
        int del_y = fb->height - LIST_BACK_H - FILES_DELETE_BTN_H - 4;
        int del_x = fb->width - FILES_DELETE_BTN_W - 8;
        flux_fb_fill_rect(fb, del_x, del_y, FILES_DELETE_BTN_W, FILES_DELETE_BTN_H, COL_DANGER);
        const char *dlabel = "Loeschen";
        int dlw = flux_fb_text_width(dlabel, 2);
        flux_fb_text(fb, del_x + (FILES_DELETE_BTN_W - dlw) / 2,
                     del_y + (FILES_DELETE_BTN_H - 16) / 2, dlabel, 0xFFFFFF, 2);
    }

    /* "Neuer Ordner"-Knopf oben rechts im Titelbereich */
    {
        int bw = 100, bh = 32;
        int bx = fb->width - bw - 8;
        int by = STATUSBAR_H + (TITLE_AREA_H - bh) / 2;
        flux_fb_fill_rect(fb, bx, by, bw, bh, COL_KEY_SPEC);
        const char *nl = "+ Ordner";
        int nlw = flux_fb_text_width(nl, 2);
        flux_fb_text(fb, bx + (bw - nlw) / 2, by + (bh - 16) / 2, nl, COL_ACCENT, 2);
    }

    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}

int flux_ui_files_new_btn_hit(const flux_fb_t *fb, int x, int y) {
    int bw = 100, bh = 32;
    int bx = fb->width - bw - 8;
    int by = STATUSBAR_H + (TITLE_AREA_H - bh) / 2;
    return (x >= bx && x < bx + bw && y >= by && y < by + bh);
}

int flux_ui_files_delete_hit(const flux_fb_t *fb, int x, int y) {
    int del_y = fb->height - LIST_BACK_H - FILES_DELETE_BTN_H - 4;
    int del_x = fb->width - FILES_DELETE_BTN_W - 8;
    return (x >= del_x && x < del_x + FILES_DELETE_BTN_W &&
            y >= del_y && y < del_y + FILES_DELETE_BTN_H);
}

/* ---- Datei-Betrachter -------------------------------------------------- */

#define VIEWER_LINE_H    28
#define VIEWER_LINES_MAX 200

void flux_ui_draw_file_viewer(flux_fb_t *fb, const char *path,
                               const char *content, int scroll_line) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    /* Titel: Dateiname */
    const char *slash = strrchr(path, '/');
    const char *fname = slash ? slash + 1 : path;
    flux_fb_text(fb, 16, STATUSBAR_H + 10, fname, COL_ACCENT, 3);

    /* Pfad kleiner darunter */
    flux_fb_text(fb, 16, STATUSBAR_H + 38, path, COL_DIM, 2);

    int text_top = STATUSBAR_H + TITLE_AREA_H;
    int text_bottom = fb->height - LIST_BACK_H - 4;
    int avail_lines = (text_bottom - text_top) / VIEWER_LINE_H;

    /* Inhalt zeilenweise ausgeben */
    const char *p = content;
    int cur_line = 0;
    int visible = 0;
    while (*p && visible < avail_lines) {
        const char *eol = p;
        while (*eol && *eol != '\n') eol++;

        if (cur_line >= scroll_line) {
            char linebuf[256];
            size_t len = (size_t)(eol - p);
            if (len >= sizeof(linebuf)) len = sizeof(linebuf) - 1;
            memcpy(linebuf, p, len);
            linebuf[len] = '\0';
            flux_fb_text(fb, 12, text_top + visible * VIEWER_LINE_H,
                         linebuf, COL_TEXT, 2);
            visible++;
        }
        cur_line++;
        if (*eol) eol++;
        p = eol;
        if (!*p) break;
    }

    if (visible == 0 && cur_line == 0) {
        flux_fb_text(fb, 16, text_top + 12, "(Datei leer)", COL_DIM, 2);
    }

    /* Scroll-Hinweis */
    if (scroll_line > 0) {
        const char *up = "^ Hoch";
        flux_fb_text(fb, fb->width - flux_fb_text_width(up, 2) - 12,
                     text_bottom - 20, up, COL_DIM, 2);
    }

    /* Geteilte Leiste unten: links "Zurueck", rechts "KI fragen".
     * Die KI-Schaltflaeche macht die Dokument-KI sichtbar (vorher nur
     * per Wisch-nach-rechts erreichbar, siehe flux_ui_viewer_hit). */
    {
        int by = fb->height - LIST_BACK_H;
        flux_fb_fill_rect(fb, 0, by, fb->width, LIST_BACK_H, COL_STATUSBAR);
        int split = fb->width * 3 / 5;
        flux_fb_fill_rect(fb, split, by + 10, 1, LIST_BACK_H - 20, COL_DIVIDER);

        const char *back_lbl = "Zurueck";
        int btw = flux_fb_text_width(back_lbl, 3);
        flux_fb_text(fb, (split - btw) / 2, by + (LIST_BACK_H - 21) / 2,
                     back_lbl, COL_DIM, 3);

        const char *ai_lbl = "KI fragen";
        int atw = flux_fb_text_width(ai_lbl, 3);
        flux_fb_text(fb, split + (fb->width - split - atw) / 2,
                     by + (LIST_BACK_H - 21) / 2, ai_lbl, COL_ACCENT, 3);
    }
    flux_fb_present(fb);
}

int flux_ui_viewer_hit(const flux_fb_t *fb, int x, int y,
                       int *scroll_delta, int *back, int *ai) {
    *scroll_delta = 0;
    *back = 0;
    if (ai) *ai = 0;
    if (y >= fb->height - LIST_BACK_H) {
        /* Untere Leiste: rechtes Drittel = KI fragen, sonst Zurueck */
        if (ai && x >= fb->width * 3 / 5) *ai = 1;
        else *back = 1;
        return 1;
    }
    int mid = fb->height / 2;
    if (y < mid - 20) { *scroll_delta = -3; return 1; }
    if (y > mid + 20) { *scroll_delta =  3; return 1; }
    return 0;
}

/* ---- Benachrichtigungs-Overlay --------------------------------------- */

void flux_ui_draw_notify(flux_fb_t *fb) {
    flux_fb_fill_rect(fb, 0, 0, fb->width, fb->height, 0x080C14);

    int y = 24;

    /* --- Grosse Uhrzeit + Datum ---------------------------------------- */
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char clock_buf[16], date_buf[64];
    strftime(clock_buf, sizeof(clock_buf), "%H:%M", &tmv);
    strftime(date_buf, sizeof(date_buf), "%A, %d. %B %Y", &tmv);

    int cw = flux_fb_text_width(clock_buf, 7);
    flux_fb_text(fb, (fb->width - cw) / 2, y, clock_buf, COL_TEXT, 7);
    y += 7 * 11 + 8;

    int dw = flux_fb_text_width(date_buf, 3);
    flux_fb_text(fb, (fb->width - dw) / 2, y, date_buf, COL_DIM, 3);
    y += 36;

    /* Trennlinie */
    flux_fb_fill_rect(fb, 20, y, fb->width - 40, 1, COL_DIVIDER);
    y += 12;

    /* --- Batterie + WLAN ------------------------------------------------ */
    int bat = read_battery_pct();
    int wifi = read_wifi_quality();

    if (bat >= 0) {
        draw_battery_icon(fb, 24, y + 2, bat);
        char pbuf[16];
        snprintf(pbuf, sizeof(pbuf), "  Batterie: %d%%", bat);
        flux_fb_text(fb, 48, y, pbuf, COL_TEXT, 2);
        y += 28;
    }
    if (wifi >= 0) {
        draw_wifi_icon(fb, 24, y + 2, wifi);
        char wbuf[32];
        snprintf(wbuf, sizeof(wbuf), "  WLAN: %d/70", wifi);
        flux_fb_text(fb, 38, y, wbuf, COL_TEXT, 2);
        y += 28;
    }

    /* Trennlinie */
    flux_fb_fill_rect(fb, 20, y, fb->width - 40, 1, COL_DIVIDER);
    y += 12;

    /* --- Wetter --------------------------------------------------------- */
    {
        char weather[256] = {0};
        FILE *f = fopen(WEATHER_CACHE, "r");
        if (f) {
            if (!fgets(weather, sizeof(weather), f)) weather[0] = '\0';
            size_t l = strlen(weather);
            while (l > 0 && (weather[l-1] == '\n' || weather[l-1] == '\r'))
                weather[--l] = '\0';
            fclose(f);
        }
        if (weather[0]) {
            weather_cond_t cond = classify_weather(weather);
            draw_weather_icon(fb, 16, y, cond);
            draw_wrapped(fb, 50, y, fb->width - 66, weather, COL_DIM, 2, 26);
            y += 52;
        }
    }

    /* --- Alarme --------------------------------------------------------- */
    {
        FILE *f = fopen("/tmp/flux_alarms.txt", "r");
        if (f) {
            flux_fb_text(fb, 16, y, "Alarme:", COL_ACCENT, 2);
            y += 24;
            char line[128];
            int shown = 0;
            while (fgets(line, sizeof(line), f) && shown < 3) {
                size_t l = strlen(line);
                while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r'))
                    line[--l] = '\0';
                if (!line[0]) continue;
                flux_fb_text(fb, 24, y, line, COL_TEXT, 2);
                y += 24; shown++;
            }
            fclose(f);
            if (!shown) { flux_fb_text(fb, 24, y, "(keine)", COL_DIM, 2); y += 24; }
        }
    }

    /* --- Erinnerungen --------------------------------------------------- */
    {
        FILE *f = fopen("/tmp/flux_reminders.txt", "r");
        if (f) {
            flux_fb_text(fb, 16, y, "Erinnerungen:", COL_ACCENT, 2);
            y += 24;
            char line[128];
            int shown = 0;
            while (fgets(line, sizeof(line), f) && shown < 2) {
                size_t l = strlen(line);
                while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r'))
                    line[--l] = '\0';
                if (!line[0]) continue;
                flux_fb_text(fb, 24, y, line, COL_TEXT, 2);
                y += 24; shown++;
            }
            fclose(f);
        }
    }

    /* Hinweis zum Schliessen */
    {
        const char *hint = "Tippen zum Schliessen";
        int hw = flux_fb_text_width(hint, 2);
        flux_fb_text(fb, (fb->width - hw) / 2, fb->height - 32, hint, COL_DIM, 2);
    }

    flux_fb_present(fb);
}

/* ---- Tap-Ripple-Animation ------------------------------------------- */

void flux_ui_draw_ripple(flux_fb_t *fb, int cx, int cy, int frame) {
    /* Expanding square outline, fading from accent to dim */
    static const int sizes[5] = {14, 28, 42, 58, 74};
    static const uint32_t colors[5] = {
        0xFFFFFF, 0xB0E0DC, 0x7FB8B0, 0x4D8F88, 0x2A6060
    };
    int s = sizes[frame < 5 ? frame : 4];
    uint32_t col = colors[frame < 5 ? frame : 4];
    int x = cx - s/2, y = cy - s/2;
    int thick = 3 - frame/2;
    if (thick < 1) thick = 1;
    /* Top */
    flux_fb_fill_rect(fb, x, y, s, thick, col);
    /* Bottom */
    flux_fb_fill_rect(fb, x, y + s - thick, s, thick, col);
    /* Left */
    flux_fb_fill_rect(fb, x, y, thick, s, col);
    /* Right */
    flux_fb_fill_rect(fb, x + s - thick, y, thick, s, col);
}

/* ---- Animierte Aktions-Symbole -------------------------------------- */

static void fill_circle(flux_fb_t *fb, int cx, int cy, int r, uint32_t col) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                flux_fb_set_px(fb, cx + dx, cy + dy, col);
}

static void draw_ring(flux_fb_t *fb, int cx, int cy, int r, int thick, uint32_t col) {
    int ri = r - thick; if (ri < 0) ri = 0;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r && d2 >= ri * ri)
                flux_fb_set_px(fb, cx + dx, cy + dy, col);
        }
}

void flux_ui_draw_action_anim(flux_fb_t *fb, flux_anim_kind_t kind,
                              int frame, const char *caption) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    int cx = fb->width / 2;
    int cy = fb->height / 2 - 20;

    switch (kind) {
        case FLUX_ANIM_MAIL: {
            /* Papierflieger fliegt nach rechts-oben, mit verblassender Spur */
            int t = frame % 24;
            int px = cx - 60 + t * 5;
            int py = cy + 22 - t * 2;
            for (int i = 1; i <= 3; i++) {
                int sx = px - i * 15, sy = py + i * 6;
                if (sx > 16) flux_fb_fill_rect(fb, sx, sy, 7, 3, 0x33485F);
            }
            draw_send_arrow(fb, px, py, 34, COL_ACCENT);
            break;
        }
        case FLUX_ANIM_SMS: {
            int bw = 130, bh = 76, bx = cx - bw / 2, by = cy - bh / 2;
            fill_round_rect(fb, bx, by, bw, bh, 16, COL_CARD);
            flux_fb_fill_rect(fb, bx + 20, by + bh - 2, 16, 12, COL_CARD); /* Zipfel */
            int lit = (frame / 4) % 4;
            for (int i = 0; i < 3; i++)
                fill_circle(fb, bx + 32 + i * 33, by + bh / 2, 8,
                            (i < lit) ? COL_ACCENT : 0x44566B);
            break;
        }
        case FLUX_ANIM_CALL: {
            /* pulsierende Ringe + zentraler "Hoerer" */
            for (int i = 0; i < 3; i++) {
                int r = 30 + ((frame * 4 + i * 16) % 48);
                uint32_t c = (r < 50) ? COL_ACCENT : (r < 66) ? 0x2F7C74 : 0x21534E;
                draw_ring(fb, cx, cy, r, 3, c);
            }
            fill_circle(fb, cx, cy, 24, COL_SEND);
            draw_thick_line(fb, cx - 9, cy - 9, cx + 9, cy + 9, 6, COL_TEXT);
            break;
        }
        case FLUX_ANIM_SCAN: {
            /* WLAN-Balken leuchten nacheinander auf */
            int lit = (frame / 3) % 4;
            int bw = 24, gap = 14, basey = cy + 34;
            int x0 = cx - (bw * 3 + gap * 2) / 2;
            for (int i = 0; i < 3; i++) {
                int h = 22 + i * 24;
                uint32_t c = ((i + 1) <= lit) ? COL_ACCENT : 0x33485F;
                flux_fb_fill_rect(fb, x0 + i * (bw + gap), basey - h, bw, h, c);
            }
            break;
        }
        case FLUX_ANIM_OK: {
            fill_circle(fb, cx, cy, 46, COL_SEND);
            draw_thick_line(fb, cx - 22, cy + 2, cx - 6, cy + 18, 6, COL_TEXT);
            if (frame > 3)
                draw_thick_line(fb, cx - 6, cy + 18, cx + 24, cy - 16, 6, COL_TEXT);
            break;
        }
        case FLUX_ANIM_FAIL: {
            fill_circle(fb, cx, cy, 46, COL_CANCEL);
            draw_thick_line(fb, cx - 20, cy - 20, cx + 20, cy + 20, 6, COL_TEXT);
            draw_thick_line(fb, cx + 20, cy - 20, cx - 20, cy + 20, 6, COL_TEXT);
            break;
        }
    }

    if (caption && caption[0]) {
        int tw = flux_fb_text_width(caption, 2);
        flux_fb_text(fb, (fb->width - tw) / 2, cy + 96, caption, COL_DIM, 2);
    }
    flux_fb_present(fb);
}

/* ---- Kalender -------------------------------------------------------- */

#define CAL_HEADER_H    52
#define CAL_DAYROW_H    32
#define CAL_CELL_W(fb)  ((fb)->width / 7)
#define CAL_CELL_H      58
#define CAL_GRID_TOP(fb) (STATUSBAR_H + CAL_HEADER_H + CAL_DAYROW_H)

static int days_in_month(int y, int m) {
    static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int days = d[m-1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) days = 29;
    return days;
}

/* Returns 0=Mon .. 6=Sun for the 1st of month m in year y */
static int first_weekday(int y, int m) {
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y--;
    int dow = (y + y/4 - y/100 + y/400 + t[m-1] + 1) % 7;
    return (dow + 6) % 7; /* 0=Mon */
}

static const char *month_name(int m) {
    static const char *names[] = {
        "Januar","Februar","Maerz","April","Mai","Juni",
        "Juli","August","September","Oktober","November","Dezember"
    };
    return (m >= 1 && m <= 12) ? names[m-1] : "?";
}

void flux_ui_draw_calendar(flux_fb_t *fb, int year, int month,
                            int today_day, int selected_day,
                            const char **event_strs, int n_events) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    int cw = CAL_CELL_W(fb);
    int grid_top = CAL_GRID_TOP(fb);

    /* --- Header: < Monat Jahr > --------------------------------------- */
    int hdr_y = STATUSBAR_H + (CAL_HEADER_H - 21) / 2;
    /* "<" Pfeil links */
    flux_fb_fill_rect(fb, 4, STATUSBAR_H + 6, 40, CAL_HEADER_H - 12, COL_KEY);
    flux_fb_text(fb, 14, hdr_y, "<", COL_ACCENT, 3);
    /* ">" Pfeil rechts */
    flux_fb_fill_rect(fb, fb->width - 44, STATUSBAR_H + 6, 40, CAL_HEADER_H - 12, COL_KEY);
    flux_fb_text(fb, fb->width - 34, hdr_y, ">", COL_ACCENT, 3);
    /* Monat + Jahr zentriert */
    char title[32];
    snprintf(title, sizeof(title), "%s %d", month_name(month), year);
    int tw = flux_fb_text_width(title, 3);
    flux_fb_text(fb, (fb->width - tw) / 2, hdr_y, title, COL_TEXT, 3);

    /* --- Wochentag-Kopfzeile ------------------------------------------ */
    static const char *dow_labels[] = {"Mo","Di","Mi","Do","Fr","Sa","So"};
    for (int i = 0; i < 7; i++) {
        int bx = i * cw;
        int lw = flux_fb_text_width(dow_labels[i], 2);
        uint32_t col = (i >= 5) ? 0xEE8855 : COL_DIM; /* Sa/So in orange */
        flux_fb_text(fb, bx + (cw - lw) / 2,
                     STATUSBAR_H + CAL_HEADER_H + (CAL_DAYROW_H - 16) / 2,
                     dow_labels[i], col, 2);
    }

    /* --- Datumsraster ------------------------------------------------- */
    int first_dow = first_weekday(year, month);
    int dim = days_in_month(year, month);

    for (int day = 1; day <= dim; day++) {
        int cell_idx = first_dow + day - 1; /* 0-based cell in grid */
        int row = cell_idx / 7;
        int col_idx = cell_idx % 7;
        int cx2 = col_idx * cw;
        int cy = grid_top + row * CAL_CELL_H;

        /* Cell background */
        uint32_t bg = COL_BG;
        if (day == selected_day) bg = 0x1E3A5A;
        else if (day == today_day) bg = 0x152A1E;
        if (bg != COL_BG)
            flux_fb_fill_rect(fb, cx2 + 1, cy + 1, cw - 2, CAL_CELL_H - 2, bg);

        /* Accent border for today/selected */
        if (day == selected_day)
            flux_fb_fill_rect(fb, cx2, cy, 3, CAL_CELL_H, COL_ACCENT);
        else if (day == today_day)
            flux_fb_fill_rect(fb, cx2, cy, 3, CAL_CELL_H, 0x22C55E);

        /* Day number */
        char daystr[4]; snprintf(daystr, sizeof(daystr), "%d", day);
        int dw = flux_fb_text_width(daystr, 2);
        uint32_t tcol = (day == selected_day) ? COL_TEXT :
                        (day == today_day)     ? 0x22C55E : COL_DIM;
        flux_fb_text(fb, cx2 + (cw - dw) / 2, cy + (CAL_CELL_H - 16) / 2, daystr, tcol, 2);
    }

    /* --- Ereignisse fuer ausgewaehlten Tag ------------------------------ */
    int ev_y = grid_top + ((first_dow + dim - 1) / 7 + 1) * CAL_CELL_H + 8;
    if (ev_y > fb->height - LIST_BACK_H - 8) ev_y = grid_top + 6 * CAL_CELL_H + 8;

    if (n_events > 0) {
        const char *ev_title = "Termine:";
        flux_fb_text(fb, 12, ev_y, ev_title, COL_ACCENT, 2);
        ev_y += 24;
        for (int i = 0; i < n_events && ev_y < fb->height - LIST_BACK_H - 20; i++) {
            draw_wrapped(fb, 20, ev_y, fb->width - 32, event_strs[i], COL_TEXT, 2, 24);
            ev_y += 28;
        }
    } else if (selected_day > 0) {
        flux_fb_text(fb, 12, ev_y, "(Keine Termine)", COL_DIM, 2);
    }

    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}

int flux_ui_calendar_hit(const flux_fb_t *fb, int x, int y,
                          int *day, int *prev_month, int *next_month) {
    *day = 0; *prev_month = 0; *next_month = 0;

    if (y >= fb->height - LIST_BACK_H) return 0; /* back bar handled by caller */

    /* Navigation arrows */
    if (y >= STATUSBAR_H && y < STATUSBAR_H + CAL_HEADER_H) {
        if (x >= 4 && x < 44) { *prev_month = 1; return 1; }
        if (x >= fb->width - 44 && x < fb->width - 4) { *next_month = 1; return 1; }
        return 0;
    }

    /* Day cells */
    if (y < CAL_GRID_TOP(fb)) return 0;
    int row = (y - CAL_GRID_TOP(fb)) / CAL_CELL_H;
    int col_idx = x / CAL_CELL_W(fb);
    int cell_idx = row * 7 + col_idx;
    /* We need year/month to compute first_dow; pass dummy -- caller knows */
    /* Actually we need the caller to pass day_offset. Let's embed it here.
     * We return the cell index and let the caller map to day. */
    *day = cell_idx; /* raw cell index -- caller adjusts by first_weekday */
    return 1;
}

/* ---- Kontakte -------------------------------------------------------- */

void flux_ui_draw_contacts(flux_fb_t *fb, const char **names,
                            const char **details, int n, int selected_idx) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    flux_fb_text(fb, 16, STATUSBAR_H + 12, "Kontakte", COL_ACCENT, 3);

    if (n == 0) {
        flux_fb_text(fb, 16, STATUSBAR_H + TITLE_AREA_H + 12,
                     "Keine Kontakte. KI: \"Speichere Max, +49 151 ..., max@mail.de\"",
                     COL_DIM, 2);
    } else {
        list_row_geom_t rows[LIST_MAX_ROWS];
        int rn = build_list_rows(fb, n, rows);
        for (int i = 0; i < rn; i++) {
            uint32_t row_col = (i == selected_idx) ? 0x1E3A5A : COL_ROW;
            flux_fb_fill_rect(fb, rows[i].x, rows[i].y, rows[i].w, rows[i].h, row_col);
            if (i == selected_idx)
                flux_fb_fill_rect(fb, rows[i].x, rows[i].y, 4, rows[i].h, COL_ACCENT);
            flux_fb_text(fb, rows[i].x + 12, rows[i].y + 8, names[i], COL_TEXT, 2);
            if (details && details[i])
                flux_fb_text(fb, rows[i].x + 12, rows[i].y + rows[i].h - 24,
                             details[i], COL_DIM, 2);
        }
    }

    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}

int flux_ui_notify_hit(const flux_fb_t *fb, int x, int y) {
    (void)fb; (void)x; (void)y;
    return 1; /* beliebiger Tap schliesst den Overlay */
}

/* ---- Fotogalerie ----------------------------------------------------- */

#define GALLERY_CAM_BTN_W 120
#define GALLERY_CAM_BTN_H  48

/* Kamera-Knopf: rechts unten in der Titelzeile. */
static void draw_gallery_cam_btn(flux_fb_t *fb) {
    int bx = fb->width - GALLERY_CAM_BTN_W - 8;
    int by = STATUSBAR_H + (TITLE_AREA_H - GALLERY_CAM_BTN_H) / 2;
    flux_fb_fill_rect(fb, bx, by, GALLERY_CAM_BTN_W, GALLERY_CAM_BTN_H, COL_ACCENT);
    int tw = flux_fb_text_width("Kamera", 2);
    flux_fb_text(fb, bx + (GALLERY_CAM_BTN_W - tw) / 2,
                 by + (GALLERY_CAM_BTN_H - 14) / 2, "Kamera", COL_BG, 2);
}

void flux_ui_draw_gallery(flux_fb_t *fb, const char **names, const char **dates,
                           int n, int selected_idx) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    flux_fb_text(fb, 16, STATUSBAR_H + 12, "Fotos", COL_ACCENT, 3);
    draw_gallery_cam_btn(fb);

    if (n == 0) {
        flux_fb_text(fb, 16, STATUSBAR_H + TITLE_AREA_H + 20,
                     "Noch keine Fotos.", COL_DIM, 2);
        flux_fb_text(fb, 16, STATUSBAR_H + TITLE_AREA_H + 42,
                     "Tippe \"Kamera\" oben rechts.", COL_DIM, 2);
    } else {
        list_row_geom_t rows[LIST_MAX_ROWS];
        int rn = build_list_rows(fb, n, rows);
        for (int i = 0; i < rn; i++) {
            uint32_t row_col = (i == selected_idx) ? 0x1E3A5A : COL_ROW;
            flux_fb_fill_rect(fb, rows[i].x, rows[i].y, rows[i].w, rows[i].h, row_col);
            if (i == selected_idx)
                flux_fb_fill_rect(fb, rows[i].x, rows[i].y, 4, rows[i].h, COL_ACCENT);
            /* Kamera-Icon (kleines Quadrat als Symbol) */
            flux_fb_fill_rect(fb, rows[i].x + 12, rows[i].y + 12, 28, 22, 0x2A3850);
            flux_fb_fill_rect(fb, rows[i].x + 17, rows[i].y + 16, 18, 14, 0x334466);
            /* Dateiname */
            flux_fb_text(fb, rows[i].x + 52, rows[i].y + 8, names[i], COL_TEXT, 2);
            if (dates && dates[i])
                flux_fb_text(fb, rows[i].x + 52, rows[i].y + rows[i].h - 24,
                             dates[i], COL_DIM, 2);
        }
    }

    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}

int flux_ui_gallery_camera_hit(const flux_fb_t *fb, int x, int y) {
    int bx = fb->width - GALLERY_CAM_BTN_W - 8;
    int by = STATUSBAR_H + (TITLE_AREA_H - GALLERY_CAM_BTN_H) / 2;
    return (x >= bx && x < bx + GALLERY_CAM_BTN_W &&
            y >= by && y < by + GALLERY_CAM_BTN_H);
}

/* ---- Bild-Betrachter ------------------------------------------------- */

#define IV_HEADER_H   48     /* Titelzeile oben */
#define IV_BTN_H      56     /* Button-Leiste unten */
#define IV_BTN_COUNT  3      /* Zurueck | KI analysieren | Loeschen */

void flux_ui_draw_image_viewer(flux_fb_t *fb, const char *filename,
                                const uint32_t *pixels, int img_w, int img_h,
                                const char *ai_caption, int analyzing) {
    flux_fb_clear(fb, 0x080A10);
    draw_statusbar(fb);

    /* Header mit Dateiname und X-Schliessen-Symbol */
    flux_fb_fill_rect(fb, 0, STATUSBAR_H, fb->width, IV_HEADER_H, COL_STATUSBAR);
    flux_fb_text(fb, 12, STATUSBAR_H + (IV_HEADER_H - 14) / 2, filename, COL_TEXT, 2);

    /* Bildbereich berechnen */
    int img_area_y = STATUSBAR_H + IV_HEADER_H;

    /* Untere Bereich: Caption + Buttons */
    int caption_h = 0;
    if (ai_caption && ai_caption[0]) {
        /* Mehrzeiliger Text: ca. 14px pro Zeile, max 4 Zeilen */
        caption_h = 72;
    } else if (analyzing) {
        caption_h = 32;
    }
    int btn_area_y = fb->height - IV_BTN_H;
    int cap_area_y = btn_area_y - caption_h;
    int img_area_h = cap_area_y - img_area_y;

    /* Bild zentriert zeichnen */
    if (pixels && img_w > 0 && img_h > 0) {
        int off_x = (fb->width  - img_w) / 2;
        int off_y = img_area_y + (img_area_h - img_h) / 2;
        if (off_x < 0) off_x = 0;
        if (off_y < img_area_y) off_y = img_area_y;
        for (int iy = 0; iy < img_h; iy++) {
            int fy = off_y + iy;
            if (fy < img_area_y || fy >= cap_area_y) continue;
            for (int ix = 0; ix < img_w; ix++) {
                int fx = off_x + ix;
                if (fx < 0 || fx >= fb->width) continue;
                fb->back[fy * fb->width + fx] = pixels[iy * img_w + ix];
            }
        }
    } else {
        /* Platzhalter -- dunkelgraues Feld mit Kamera-Symbol */
        int px = (fb->width - 80) / 2, py = img_area_y + (img_area_h - 60) / 2;
        flux_fb_fill_rect(fb, px, py, 80, 60, 0x1A2535);
        flux_fb_fill_rect(fb, px + 15, py + 10, 50, 40, 0x243450);
        flux_fb_fill_rect(fb, px + 28, py + 18, 24, 24, 0x2A3D60);
    }

    /* Caption / Analysiere-Text */
    if (analyzing) {
        int cw = flux_fb_text_width("KI analysiert Bild ...", 2);
        flux_fb_text(fb, (fb->width - cw) / 2, cap_area_y + 8,
                     "KI analysiert Bild ...", COL_ACCENT, 2);
    } else if (ai_caption && ai_caption[0]) {
        flux_fb_fill_rect(fb, 0, cap_area_y, fb->width, caption_h, 0x0E1520);
        /* Bis zu 3 Zeilen a 40 Zeichen */
        char line[48];
        const char *p = ai_caption;
        int cy = cap_area_y + 6;
        for (int li = 0; li < 4 && *p; li++) {
            int len = 0;
            while (p[len] && p[len] != '\n' && len < 42) len++;
            if (len > 42) len = 42;
            memcpy(line, p, (size_t)len);
            line[len] = '\0';
            flux_fb_text(fb, 10, cy, line, COL_TEXT, 2);
            cy += 17;
            p += len;
            if (*p == '\n') p++;
        }
    }

    /* Button-Leiste: [Zurueck] [KI analysieren] [Loeschen] */
    flux_fb_fill_rect(fb, 0, btn_area_y, fb->width, IV_BTN_H, COL_STATUSBAR);
    int bw = fb->width / IV_BTN_COUNT;
    const char *btn_labels[] = { "< Zurueck", "KI analyse", "Loeschen" };
    uint32_t btn_cols[] = { COL_KEY_SPEC, COL_ACCENT, COL_DANGER };
    for (int i = 0; i < IV_BTN_COUNT; i++) {
        int bx = i * bw + 4;
        int byw = btn_area_y + 6;
        int bww = bw - 8;
        int bhh = IV_BTN_H - 12;
        flux_fb_fill_rect(fb, bx, byw, bww, bhh, btn_cols[i]);
        int tw = flux_fb_text_width(btn_labels[i], 2);
        uint32_t tc = (i == 1) ? COL_BG : COL_TEXT;
        flux_fb_text(fb, bx + (bww - tw) / 2, byw + (bhh - 14) / 2, btn_labels[i], tc, 2);
    }

    flux_fb_present(fb);
}

int flux_ui_image_viewer_hit(const flux_fb_t *fb, int x, int y,
                              int *back, int *analyze, int *del) {
    *back = *analyze = *del = 0;
    int btn_area_y = fb->height - IV_BTN_H;
    if (y < btn_area_y) return 0;
    int bw = fb->width / IV_BTN_COUNT;
    int btn = x / bw;
    if (btn == 0) { *back    = 1; return 1; }
    if (btn == 1) { *analyze = 1; return 1; }
    if (btn == 2) { *del     = 1; return 1; }
    return 0;
}

/* ---- Globaler KI-Kontext-Overlay (Wisch nach rechts) ---------------- */

#define AI_OVL_MARGIN  14
#define AI_OVL_CARD_Y  (STATUSBAR_H + 16)
#define AI_OVL_CARD_H  196
#define AI_OVL_BTN_H   48

/* Verdunkelt den aktuellen Backbuffer (50% Schwarz-Ueberlagerung). */
static void dim_screen(flux_fb_t *fb) {
    int npx = fb->width * fb->height;
    for (int i = 0; i < npx; i++) {
        uint32_t px = fb->back[i];
        fb->back[i] = ((px >> 1) & 0x7F7F7F);
    }
}

void flux_ui_draw_ai_overlay(flux_fb_t *fb, const char *context_label,
                              const char *input_text, const char *result_text) {
    dim_screen(fb);

    int cx  = AI_OVL_MARGIN;
    int cw  = fb->width - 2 * AI_OVL_MARGIN;
    int cy  = AI_OVL_CARD_Y;
    int ch  = AI_OVL_CARD_H;

    /* Karten-Hintergrund */
    flux_fb_fill_rect(fb, cx, cy, cw, ch, COL_CARD);
    /* Akzent-Oberkante */
    flux_fb_fill_rect(fb, cx, cy, cw, 4, COL_ACCENT);

    /* Kontext-Label */
    if (context_label && context_label[0]) {
        flux_fb_text(fb, cx + 12, cy + 10, context_label, COL_ACCENT, 2);
    }

    /* Eingabefeld */
    int inf_y = cy + 38;
    int inf_h = 54;
    flux_fb_fill_rect(fb, cx + 8, inf_y, cw - 16, inf_h, COL_BG);
    flux_fb_fill_rect(fb, cx + 8, inf_y, 3, inf_h, COL_ACCENT); /* linker Akzent-Strich */
    const char *disp = (input_text && input_text[0]) ? input_text : "Frage die KI...";
    uint32_t   tcol  = (input_text && input_text[0]) ? COL_TEXT : COL_DIM;
    flux_fb_text(fb, cx + 18, inf_y + (inf_h - 14) / 2, disp, tcol, 2);

    /* Zwei Buttons: [Abbrechen] [Fragen] */
    int bty  = cy + ch - AI_OVL_BTN_H - 10;
    int btnw = (cw - 28) / 2;

    flux_fb_fill_rect(fb, cx + 8, bty, btnw, AI_OVL_BTN_H, COL_KEY_SPEC);
    int tw = flux_fb_text_width("Abbrechen", 2);
    flux_fb_text(fb, cx + 8 + (btnw - tw) / 2, bty + (AI_OVL_BTN_H - 14) / 2,
                 "Abbrechen", COL_DIM, 2);

    int b2x = cx + 8 + btnw + 12;
    flux_fb_fill_rect(fb, b2x, bty, btnw, AI_OVL_BTN_H, COL_ACCENT);
    tw = flux_fb_text_width("Fragen", 2);
    flux_fb_text(fb, b2x + (btnw - tw) / 2, bty + (AI_OVL_BTN_H - 14) / 2,
                 "Fragen", COL_BG, 2);

    /* Ergebnis-Panel (unterhalb der Karte) */
    if (result_text && result_text[0]) {
        int ry  = cy + ch + 10;
        int rh  = fb->height - ry - 14;
        if (rh > 64) {
            flux_fb_fill_rect(fb, cx, ry, cw, rh, 0x0D1320);
            flux_fb_fill_rect(fb, cx, ry, cw, 3, COL_ACCENT);

            /* Text: bis zu ~9 Zeilen a 42 Zeichen */
            const char *p = result_text;
            int text_bottom = ry + rh - 52; /* Platz fuer Speichern-Button */
            int ly = ry + 8;
            while (*p && ly < text_bottom) {
                char line[48]; int len = 0;
                while (p[len] && p[len] != '\n' && len < 42) len++;
                if (len == 0) { p++; ly += 17; continue; }
                memcpy(line, p, (size_t)len); line[len] = '\0';
                flux_fb_text(fb, cx + 10, ly, line, COL_TEXT, 2);
                ly += 17;
                p += len;
                if (*p == '\n') p++;
            }

            /* "Speichern"-Button */
            int sy = ry + rh - 46;
            flux_fb_fill_rect(fb, cx + 8, sy, cw - 16, 38, 0x143314);
            tw = flux_fb_text_width("Als Datei speichern", 2);
            flux_fb_text(fb, cx + 8 + (cw - 16 - tw) / 2, sy + 12,
                         "Als Datei speichern", 0x4AE84A, 2);
        }
    }

    flux_fb_present(fb);
}

/* ---- Meeting-Mitschrift -------------------------------------------- */

#define MTG_BTN_R   56   /* Radius des Aufnahme-Knopfs */
#define MTG_SAVE_H  52
#define MTG_BACK_H  LIST_BACK_H

void flux_ui_draw_meeting(flux_fb_t *fb, int recording, int elapsed_s,
                           const char *transcript, const char *status_msg) {
    flux_fb_fill_rect(fb, 0, 0, fb->width, fb->height, COL_BG);
    draw_statusbar(fb);

    /* Title */
    int hy = STATUSBAR_H + 10;
    int tw = flux_fb_text_width("Meeting-Mitschrift", 3);
    flux_fb_text(fb, (fb->width - tw) / 2, hy, "Meeting-Mitschrift", g_accent, 3);

    /* Recording button: circle at 1/3 height */
    int cx = fb->width / 2;
    int cy = STATUSBAR_H + 48 + MTG_BTN_R + 10;
    /* Outer ring */
    uint32_t ring_col = recording ? 0xE05252 : 0x3A4A5A;
    for (int dy = -MTG_BTN_R; dy <= MTG_BTN_R; dy++) {
        for (int dx = -MTG_BTN_R; dx <= MTG_BTN_R; dx++) {
            int d2 = dx*dx + dy*dy;
            int r2 = MTG_BTN_R * MTG_BTN_R;
            int ri2 = (MTG_BTN_R - 6) * (MTG_BTN_R - 6);
            if (d2 <= r2 && d2 > ri2) {
                int px = cx + dx, py = cy + dy;
                if (px >= 0 && px < fb->width && py >= 0 && py < fb->height)
                    fb->back[py * fb->width + px] = ring_col;
            }
        }
    }
    /* Inner filled circle */
    int ir = MTG_BTN_R - 10;
    uint32_t inner_col = recording ? 0xC03030 : 0x1C2840;
    for (int dy = -ir; dy <= ir; dy++) {
        for (int dx = -ir; dx <= ir; dx++) {
            if (dx*dx + dy*dy <= ir*ir) {
                int px = cx + dx, py = cy + dy;
                if (px >= 0 && px < fb->width && py >= 0 && py < fb->height)
                    fb->back[py * fb->width + px] = inner_col;
            }
        }
    }
    /* Icon: REC dot or stop square */
    if (recording) {
        int dot_r = 12;
        for (int dy = -dot_r; dy <= dot_r; dy++)
            for (int dx = -dot_r; dx <= dot_r; dx++)
                if (dx*dx + dy*dy <= dot_r*dot_r) {
                    int px = cx + dx, py = cy + dy;
                    if (px >= 0 && px < fb->width && py >= 0 && py < fb->height)
                        fb->back[py * fb->width + px] = 0xFFFFFF;
                }
    } else {
        /* Microphone icon (simplified: vertical bar + semicircle outline) */
        flux_fb_fill_rect(fb, cx - 6, cy - 14, 12, 20, 0xAAAAAA);
        flux_fb_fill_rect(fb, cx - 12, cy + 6, 4, 8, 0xAAAAAA);
        flux_fb_fill_rect(fb, cx + 8, cy + 6, 4, 8, 0xAAAAAA);
        flux_fb_fill_rect(fb, cx - 6, cy + 14, 12, 4, 0xAAAAAA);
    }

    /* Timer */
    int by = cy + MTG_BTN_R + 12;
    if (recording) {
        char timer[16];
        int m = elapsed_s / 60, s = elapsed_s % 60;
        snprintf(timer, sizeof(timer), "%02d:%02d", m, s);
        tw = flux_fb_text_width(timer, 4);
        flux_fb_text(fb, (fb->width - tw) / 2, by, timer, 0xE05252, 4);
        by += 40;
    } else {
        by += 8;
    }

    /* Status text */
    if (status_msg && status_msg[0]) {
        tw = flux_fb_text_width(status_msg, 2);
        flux_fb_text(fb, (fb->width - tw) / 2, by, status_msg, COL_DIM, 2);
        by += 24;
    }

    /* Transcript area */
    int tr_top = by + 8;
    int tr_bot = fb->height - MTG_SAVE_H - MTG_BACK_H - 8;
    if (tr_top < tr_bot) {
        flux_fb_fill_rect(fb, 12, tr_top, fb->width - 24, tr_bot - tr_top, 0x111820);
        if (transcript && transcript[0]) {
            draw_wrapped(fb, 20, tr_top + 8,
                         fb->width - 40, transcript, COL_TEXT, 2, tr_bot - tr_top - 16);
        } else {
            const char *ph = recording ? "Transkription laueft..." : "Kein Transkript";
            tw = flux_fb_text_width(ph, 2);
            flux_fb_text(fb, (fb->width - tw) / 2,
                         tr_top + (tr_bot - tr_top) / 2 - 8, ph, COL_DIM, 2);
        }
    }

    /* Save button */
    int save_y = fb->height - MTG_SAVE_H - MTG_BACK_H;
    flux_fb_fill_rect(fb, 16, save_y + 4, fb->width - 32, MTG_SAVE_H - 8, 0x1A3A2A);
    tw = flux_fb_text_width("Speichern & Schliessen", 2);
    flux_fb_text(fb, (fb->width - tw) / 2, save_y + (MTG_SAVE_H - 16) / 2,
                 "Speichern & Schliessen", 0x4AE84A, 2);

    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}

int flux_ui_meeting_hit(const flux_fb_t *fb, int x, int y,
                        int *rec_btn, int *save_btn, int *back_btn) {
    *rec_btn = *save_btn = *back_btn = 0;
    if (y >= fb->height - MTG_BACK_H) { *back_btn = 1; return 1; }
    int save_y = fb->height - MTG_SAVE_H - MTG_BACK_H;
    if (y >= save_y && y < save_y + MTG_SAVE_H) { *save_btn = 1; return 1; }
    int cx = fb->width / 2, cy = STATUSBAR_H + 48 + MTG_BTN_R + 10;
    int dx = x - cx, dy = y - cy;
    if (dx*dx + dy*dy <= MTG_BTN_R * MTG_BTN_R) { *rec_btn = 1; return 1; }
    return 0;
}

/* ---- KI-Gedaechtnis ------------------------------------------------- */

void flux_ui_draw_memory(flux_fb_t *fb, const char **entries, int n, int scroll) {
    flux_fb_fill_rect(fb, 0, 0, fb->width, fb->height, COL_BG);
    draw_statusbar(fb);

    /* Header */
    int hy = STATUSBAR_H;
    flux_fb_fill_rect(fb, 0, hy, fb->width, LIST_BACK_H, COL_STATUSBAR);
    flux_fb_fill_rect(fb, 0, hy + LIST_BACK_H - 1, fb->width, 1, g_accent);
    int tw = flux_fb_text_width("< Zurueck", 2);
    flux_fb_text(fb, (fb->width - tw) / 2 - 40, hy + (LIST_BACK_H - 16) / 2, "< Zurueck", COL_DIM, 2);
    tw = flux_fb_text_width("KI-Gedaechtnis", 3);
    flux_fb_text(fb, (fb->width - tw) / 2 + 20, hy + (LIST_BACK_H - 24) / 2, "KI-Gedaechtnis", g_accent, 3);

    int list_y = hy + LIST_BACK_H + 8;
    int list_h = fb->height - list_y - 8;

    if (n == 0) {
        tw = flux_fb_text_width("Noch nichts gespeichert.", 2);
        flux_fb_text(fb, (fb->width - tw) / 2,
                     list_y + list_h / 2, "Noch nichts gespeichert.", COL_DIM, 2);
        flux_fb_present(fb);
        return;
    }

    /* Draw entries */
    int entry_h = 72;
    int y0 = list_y;
    int max_visible = list_h / entry_h;
    if (scroll > n - max_visible) scroll = n - max_visible;
    if (scroll < 0) scroll = 0;

    for (int i = scroll; i < n && y0 + entry_h <= list_y + list_h; i++) {
        int ey = y0;
        /* Alternating row background */
        uint32_t row_bg = (i % 2 == 0) ? COL_ROW : 0x111111;
        flux_fb_fill_rect(fb, 0, ey, fb->width, entry_h, row_bg);
        /* Accent left stripe */
        flux_fb_fill_rect(fb, 0, ey + 4, 4, entry_h - 8, g_accent);
        /* Entry text: first line is the timestamp in brackets */
        const char *entry = entries[i];
        if (entry[0] == '[') {
            /* Split: "[timestamp] text" */
            const char *end_bracket = strchr(entry, ']');
            if (end_bracket) {
                char ts_buf[32];
                size_t ts_len = (size_t)(end_bracket - entry + 1);
                if (ts_len >= sizeof(ts_buf)) ts_len = sizeof(ts_buf) - 1;
                memcpy(ts_buf, entry, ts_len); ts_buf[ts_len] = '\0';
                flux_fb_text(fb, 14, ey + 8, ts_buf, COL_DIM, 1);
                const char *text = end_bracket + 1;
                while (*text == ' ') text++;
                /* Wrap text over 2 lines, max 44 chars each */
                char line1[48] = {0}, line2[48] = {0};
                int l1 = 0;
                while (text[l1] && text[l1] != '\n' && l1 < 44) l1++;
                memcpy(line1, text, (size_t)l1); line1[l1] = '\0';
                flux_fb_text(fb, 14, ey + 22, line1, COL_TEXT, 2);
                text += l1;
                if (*text == '\n') text++;
                if (*text) {
                    int l2 = 0;
                    while (text[l2] && text[l2] != '\n' && l2 < 44) l2++;
                    memcpy(line2, text, (size_t)l2); line2[l2] = '\0';
                    flux_fb_text(fb, 14, ey + 44, line2, COL_DIM, 2);
                }
            } else {
                flux_fb_text(fb, 14, ey + 24, entry, COL_TEXT, 2);
            }
        } else {
            flux_fb_text(fb, 14, ey + 24, entry, COL_TEXT, 2);
        }
        /* Divider */
        flux_fb_fill_rect(fb, 0, ey + entry_h - 1, fb->width, 1, 0x222222);
        y0 += entry_h;
    }

    /* Scroll indicator */
    if (n > max_visible) {
        int bar_h = list_h * max_visible / n;
        int bar_y = list_y + list_h * scroll / n;
        flux_fb_fill_rect(fb, fb->width - 4, bar_y, 4, bar_h, g_accent);
    }

    flux_fb_present(fb);
}

int flux_ui_ai_overlay_hit(const flux_fb_t *fb, int x, int y,
                            int *cancel, int *submit, int *save_result) {
    *cancel = *submit = *save_result = 0;

    int cx   = AI_OVL_MARGIN;
    int cw   = fb->width - 2 * AI_OVL_MARGIN;
    int cy   = AI_OVL_CARD_Y;
    int ch   = AI_OVL_CARD_H;
    int bty  = cy + ch - AI_OVL_BTN_H - 10;
    int btnw = (cw - 28) / 2;
    int b2x  = cx + 8 + btnw + 12;

    /* Abbrechen-Button */
    if (x >= cx + 8 && x < cx + 8 + btnw && y >= bty && y < bty + AI_OVL_BTN_H)
        { *cancel = 1; return 1; }

    /* Fragen-Button */
    if (x >= b2x && x < b2x + btnw && y >= bty && y < bty + AI_OVL_BTN_H)
        { *submit = 1; return 1; }

    /* Speichern-Button (unteres Panel) */
    int ry = cy + ch + 10;
    int rh = fb->height - ry - 14;
    if (rh > 64) {
        int sy = ry + rh - 46;
        if (x >= cx + 8 && x < cx + cw - 8 && y >= sy && y < sy + 38)
            { *save_result = 1; return 1; }
    }

    /* Tap ausserhalb der Karte -> schliessen */
    if (y >= cy && y < cy + ch && x >= cx && x < cx + cw)
        return 0; /* Tap in der Karte, aber nicht auf einem Button */
    *cancel = 1; return 1;
}

/* ---- Semantische KI-Suche ------------------------------------------ */

#define SRCH_INPUT_H   56
#define SRCH_ROW_H     72
#define SRCH_MAX_ROWS  8

/* Source-Farben: Gedaechtnis, Kalender, Kontakte, Dateien, Foto, Notiz */
static uint32_t srch_source_color(const char *line) {
    if (strncmp(line, "Gedaechtnis:", 12) == 0 || strncmp(line, "Memory:", 7) == 0)
        return 0x4FD1C5; /* Teal */
    if (strncmp(line, "Kalender:", 9) == 0)
        return 0x3B82F6; /* Blau */
    if (strncmp(line, "Kontakt:", 8) == 0)
        return 0xA855F7; /* Lila */
    if (strncmp(line, "Foto:", 5) == 0)
        return 0xF97316; /* Orange */
    if (strncmp(line, "Notiz:", 6) == 0)
        return 0x22C55E; /* Gruen */
    return 0x6B7280;     /* Grau (Datei / allgemein) */
}

void flux_ui_draw_search(flux_fb_t *fb, const char *query,
                          const char **results, int n, int searching) {
    flux_fb_fill_rect(fb, 0, 0, fb->width, fb->height, COL_BG);
    draw_statusbar(fb);

    int iy = STATUSBAR_H + 8;

    /* Suchfeld */
    flux_fb_fill_rect(fb, 8, iy, fb->width - 16, SRCH_INPUT_H, COL_KEY);
    flux_fb_fill_rect(fb, 8, iy + SRCH_INPUT_H - 2, fb->width - 16, 2, g_accent);

    /* Lupe-Icon (einfaches L) */
    flux_fb_fill_rect(fb, 18, iy + 18, 16, 16, 0x3A4A5A);
    flux_fb_fill_rect(fb, 20, iy + 20, 12, 12, COL_BG);
    flux_fb_fill_rect(fb, 27, iy + 30, 4, 8, 0x6B7280);

    if (query && query[0]) {
        /* Anzeige-Text auf Breite kuerzen */
        char disp[56]; int dlen = 0;
        while (query[dlen] && dlen < 50) dlen++;
        memcpy(disp, query, (size_t)dlen); disp[dlen] = '\0';
        flux_fb_text(fb, 44, iy + (SRCH_INPUT_H - 16) / 2, disp, COL_TEXT, 2);
        /* Cursor */
        int cw = flux_fb_text_width(disp, 2);
        flux_fb_fill_rect(fb, 44 + cw + 2, iy + 14, 2, 24, g_accent);
    } else {
        flux_fb_text(fb, 44, iy + (SRCH_INPUT_H - 16) / 2,
                     "Alles durchsuchen...", COL_DIM, 2);
    }

    int list_y = iy + SRCH_INPUT_H + 10;
    int kbd_top = flux_ui_kbd_top(fb);

    if (searching) {
        /* Lade-Animation */
        flux_fb_text(fb, 24, list_y + 20, "KI sucht...", g_accent, 2);
        flux_fb_text(fb, 24, list_y + 46,
                     "Durchsucht Gedaechtnis, Kalender,", COL_DIM, 2);
        flux_fb_text(fb, 24, list_y + 66,
                     "Kontakte, Dateien und Fotos.", COL_DIM, 2);
        draw_keyboard(fb);
        flux_fb_present(fb);
        return;
    }

    if (n == 0 && query && query[0]) {
        flux_fb_text(fb, 24, list_y + 24, "Keine Treffer gefunden.", COL_DIM, 2);
    } else if (n == 0) {
        /* Intro-Text */
        flux_fb_text(fb, 24, list_y + 16, "Einfach tippen und Enter druecken.", COL_DIM, 2);
        flux_fb_text(fb, 24, list_y + 38, "Die KI durchsucht alles auf dem Geraet:", COL_DIM, 2);
        const char *examples[] = {
            "  Gedaechtnis & persoenliche Infos",
            "  Kalender & Termine",
            "  Kontakte",
            "  Dateien & Notizen",
            "  Fotos",
        };
        int ey = list_y + 68;
        for (int i = 0; i < 5 && ey < kbd_top - 10; i++) {
            flux_fb_fill_rect(fb, 14, ey + 3, 4, 12, g_accent);
            flux_fb_text(fb, 26, ey, examples[i], COL_TEXT, 2);
            ey += 24;
        }
        draw_keyboard(fb);
        flux_fb_present(fb);
        return;
    }

    /* Ergebnisliste */
    int max_show = (kbd_top - list_y) / SRCH_ROW_H;
    if (max_show > SRCH_MAX_ROWS) max_show = SRCH_MAX_ROWS;
    if (n > max_show) n = max_show;

    for (int i = 0; i < n; i++) {
        int ry = list_y + i * SRCH_ROW_H;
        uint32_t sc = srch_source_color(results[i]);

        /* Zeilen-Hintergrund */
        flux_fb_fill_rect(fb, 0, ry, fb->width, SRCH_ROW_H - 2,
                          i % 2 == 0 ? 0x111820 : COL_BG);

        /* Farbiger Akzent-Streifen links */
        flux_fb_fill_rect(fb, 0, ry + 4, 5, SRCH_ROW_H - 12, sc);

        /* Source-Label (vor dem Doppelpunkt) */
        const char *colon = strchr(results[i], ':');
        if (colon) {
            char src[32]; size_t sl = (size_t)(colon - results[i]);
            if (sl >= sizeof(src)) sl = sizeof(src)-1;
            memcpy(src, results[i], sl); src[sl] = '\0';
            flux_fb_text(fb, 14, ry + 6, src, sc, 1);
            /* Inhalt (nach dem Doppelpunkt) */
            const char *content = colon + 1;
            while (*content == ' ') content++;
            char line1[52], line2[52]; int l1=0, l2=0;
            while (content[l1] && content[l1]!='\n' && l1<46) l1++;
            memcpy(line1, content, (size_t)l1); line1[l1]='\0';
            flux_fb_text(fb, 14, ry + 20, line1, COL_TEXT, 2);
            content += l1; if (*content=='\n') content++;
            if (*content) {
                while (content[l2] && content[l2]!='\n' && l2<46) l2++;
                memcpy(line2, content, (size_t)l2); line2[l2]='\0';
                flux_fb_text(fb, 14, ry + 42, line2, COL_DIM, 2);
            }
        } else {
            /* Kein Doppelpunkt -- direkt Text */
            flux_fb_text(fb, 14, ry + 24, results[i], COL_TEXT, 2);
        }

        /* Trennlinie */
        flux_fb_fill_rect(fb, 0, ry + SRCH_ROW_H - 2, fb->width, 1, 0x1E2840);
    }

    draw_keyboard(fb);
    flux_fb_present(fb);
}

int flux_ui_search_hit(const flux_fb_t *fb, int x, int y,
                       int *back, int *result_idx) {
    *back = 0; *result_idx = -1;
    /* Tastaturbereich wird von flux_ui_kbd_hit() behandelt */
    if (y >= flux_ui_kbd_top(fb)) return 0;
    int list_y = STATUSBAR_H + 8 + SRCH_INPUT_H + 10;
    int idx = (y - list_y) / SRCH_ROW_H;
    if (idx >= 0 && idx < SRCH_MAX_ROWS && y >= list_y) {
        *result_idx = idx;
        return 1;
    }
    (void)x;
    return 0;
}

/* ---- Spracheingabe-Overlay ----------------------------------------- */

void flux_ui_draw_voice_overlay(flux_fb_t *fb, int elapsed_s) {
    int w = fb->width, h = fb->height;

    /* Halbdurchsichtiger Schleier */
    for (int i = 0; i < w * h; i++) {
        uint32_t px = fb->back[i];
        fb->back[i] = ((px >> 1) & 0x7F7F7F) | 0x060010;
    }

    /* Pulsierender Ring -- Radius abhaengig von Zeit */
    int pulse = elapsed_s % 2; /* 0 oder 1 */
    int cx = w / 2, cy = h / 2;
    int outer_r = 72 + pulse * 8;
    int inner_r = 52;

    for (int dy = -outer_r; dy <= outer_r; dy++) {
        for (int dx = -outer_r; dx <= outer_r; dx++) {
            int d2 = dx*dx + dy*dy;
            if (d2 <= outer_r*outer_r && d2 > inner_r*inner_r) {
                int px = cx+dx, py = cy+dy;
                if (px>=0 && px<w && py>=0 && py<h) {
                    /* Rot mit leichter Transparenz */
                    uint32_t old = fb->back[py*w+px];
                    uint32_t r = 0xE0, g = 0x30, b = 0x30;
                    /* Blend 70% rot + 30% background */
                    uint32_t br = (old >> 16) & 0xFF;
                    uint32_t bg = (old >>  8) & 0xFF;
                    uint32_t bb =  old        & 0xFF;
                    fb->back[py*w+px] = (((r*7+br*3)/10) << 16) |
                                        (((g*7+bg*3)/10) <<  8) |
                                         ((b*7+bb*3)/10);
                }
            }
        }
    }

    /* Mikrofon-Punkt in der Mitte */
    int dot_r = 22;
    for (int dy = -dot_r; dy <= dot_r; dy++)
        for (int dx = -dot_r; dx <= dot_r; dx++)
            if (dx*dx+dy*dy <= dot_r*dot_r) {
                int px=cx+dx, py=cy+dy;
                if (px>=0 && px<w && py>=0 && py<h)
                    fb->back[py*w+px] = 0xE05252;
            }

    /* Timer */
    char timer_buf[16];
    int m = elapsed_s / 60, s = elapsed_s % 60;
    snprintf(timer_buf, sizeof(timer_buf), "%02d:%02d", m, s);
    int tw = flux_fb_text_width(timer_buf, 4);
    flux_fb_text(fb, (w-tw)/2, cy + outer_r + 14, timer_buf, 0xE05252, 4);

    /* Anweisung */
    const char *hint = "Sprich jetzt -- nochmal tippen zum Stoppen";
    tw = flux_fb_text_width(hint, 2);
    flux_fb_text(fb, (w-tw)/2, cy + outer_r + 58, hint, COL_DIM, 2);

    flux_fb_present(fb);
}
