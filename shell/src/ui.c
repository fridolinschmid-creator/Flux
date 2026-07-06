#include "ui.h"
#include "icons.h"
#include "anim.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

/* EINE Signatur-Akzentfarbe fuer das ganze System: Indigo -> Violet.
 *
 * Flux hat bewusst KEINE pro-Bildschirm- oder Nutzer-Themes mehr: ein
 * einziger Akzent ueber das gesamte OS ist Teil der Identitaet ("kein
 * Regenbogen, gleicher Akzent ueberall", siehe docs/DESIGN.md). Die
 * Funktion bleibt aus Kompatibilitaet erhalten, ignoriert ihr Argument
 * aber bewusst -- so erzwingen wir die eine Farbe an genau einer Stelle. */
static uint32_t g_accent  = 0x6366F1;  /* Flux Indigo  */
static uint32_t g_accent2 = 0x8B5CF6;  /* Flux Violet (Gradient-Endpunkt) */
void flux_ui_set_accent(uint32_t rgb) {
    (void)rgb;                 /* eine Identitaetsfarbe -- Argument ignoriert */
    g_accent  = 0x6366F1;
    g_accent2 = 0x8B5CF6;
}

/* Premium Dark-Mode Palette "Deep Space" */
#define COL_BG          0x07080D   /* Tiefstes Schwarz-Blau */
#define COL_SURFACE     0x0F1117   /* Leicht erhoehte Oberflaeche */
#define COL_SURFACE2    0x161C27   /* Karten-Hintergrund */
#define COL_SURFACE3    0x1E2635   /* Erhoehte Karten / Aktiv-Zustand */
#define COL_ACCENT      g_accent   /* Dynamische Akzentfarbe */
#define COL_ACCENT2     g_accent2  /* Gradient-Endpunkt */
#define COL_TEXT        0xF1F5F9   /* Warm-Weiss */
#define COL_TEXT_MUTED  0x94A3B8   /* Gedaempfter Sekundaertext */
#define COL_DIM         0x64748B   /* Deaktiviert / Labels */
#define COL_STATUSBAR   0x07080D   /* Gleich wie BG (nahtloser Uebergang) */
#define COL_KEY         0x1E2635   /* Tastatur-Taste Hintergrund */
#define COL_KEY_SPEC    0x2D3748   /* Sondertasten (Enter, Backspace) */
#define COL_ROW         0x0F1117   /* Listenzeilenhintergrund */
#define COL_ROW_ALT     0x0C0E14   /* Alternierende Zeile */
#define COL_DANGER      0xEF4444   /* Rot (Loeschen / Fehler) */
#define COL_CANCEL      0x7F1D1D   /* Dunkelrot fuer Abbrechen */
#define COL_SEND        0x10B981   /* Emerald fuer Senden / Bestaetigen */
#define COL_BUBBLE_USER 0x312E81   /* Nutzer-Blase: Indigo-900 */
#define COL_BUBBLE_AI   0x0F1117   /* KI-Blase: fast BG, mit Border */
#define COL_CARD        0x161C27   /* Karten-Hintergrund */
#define COL_DIVIDER     0x1E293B   /* Trennlinie */
#define CLIP_BTN_W      52         /* Breite der Kopieren-/Einfuegen-Knoepfe */

/* Layout-Hoehen -- Touch-optimiert (Ziel: >= 48px je antippbares Element). */
#define STATUSBAR_H   36   /* Schlanke Statusleiste */
#define QUICKROW_H    56   /* Schnellzugriff-Chips */
#define INPUT_BAR_H   76   /* Eingabeleiste */
#define MIC_BTN_W     76   /* Mikrofon-Knopf (quadratisch) */
#define TITLE_AREA_H  68   /* Titelbereich in Sekundaerbildschirmen */
#define LIST_BACK_H   64   /* Zurueck-Schaltflaeche unten */
#define LIST_MAX_ROWS 12
#define PIN_LEN 4

/* Deutsche Wochentage und Monate (strftime %A/%B sind englisch ohne setlocale). */
static const char *s_de_wday[] = {
    "Sonntag","Montag","Dienstag","Mittwoch","Donnerstag","Freitag","Samstag"
};
static const char *s_de_mon[] = {
    "Januar","Februar","März","April","Mai","Juni",
    "Juli","August","September","Oktober","November","Dezember"
};

static void format_german_date(char *buf, size_t cap, const struct tm *tm) {
    snprintf(buf, cap, "%s, %d. %s",
             s_de_wday[tm->tm_wday],
             tm->tm_mday,
             s_de_mon[tm->tm_mon]);
}

/* Vorwaerts-Deklarationen (Definitionen weiter unten im Animations-Teil). */
static void fill_circle(flux_fb_t *fb, int cx, int cy, int r, uint32_t col);
static void draw_ring(flux_fb_t *fb, int cx, int cy, int r, int thick, uint32_t col);
static void draw_setting_icon(flux_fb_t *fb, int icon, int cx, int cy, int s, uint32_t col);
static void fill_round_rect(flux_fb_t *fb, int x, int y, int w, int h, int r, uint32_t col);
static void draw_thick_line(flux_fb_t *fb, int x0, int y0, int x1, int y1, int t, uint32_t col);

/* Wetter-Typen -- Deklaration vor draw_lock (Implementierung weiter unten). */
typedef enum {
    WCOND_SUNNY = 0, WCOND_PARTLY_CLOUDY, WCOND_CLOUDY,
    WCOND_RAINY, WCOND_SNOWY, WCOND_STORMY, WCOND_FOGGY, WCOND_UNKNOWN,
} weather_cond_t;
static weather_cond_t classify_weather(const char *desc);
static void draw_weather_icon(flux_fb_t *fb, int ox, int oy, weather_cond_t cond);

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
        if (fscanf(f, "%d", &pct) != 1) pct = -1;
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
    int quality = -1;
    /* Zwei Header-Zeilen ueberspringen; wenn die Datei kuerzer ist, gibt
     * es kein WLAN-Interface zu melden. */
    if (!fgets(line, sizeof(line), f) || !fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    if (fgets(line, sizeof(line), f)) {
        char iface[64];
        int status, link;
        if (sscanf(line, " %63[^:]: %d %d.", iface, &status, &link) >= 3)
            quality = link;
    }
    fclose(f);
    return quality;
}

/* Batterie-Symbol (Outline-Stil, 22x12) bei (x,y). */
static void draw_battery_icon(flux_fb_t *fb, int x, int y, int pct) {
    uint32_t col = (pct > 50) ? 0x22C55E : (pct > 20) ? 0xF59E0B : 0xEF4444;
    /* Aeusserer Rahmen (1px Outline) */
    flux_fb_hline(fb, x, y,     20, col);
    flux_fb_hline(fb, x, y+11,  20, col);
    flux_fb_vline(fb, x, y,     12, col);
    flux_fb_vline(fb, x+19, y,  12, col);
    /* Plus-Pol */
    flux_fb_fill_rect(fb, x+20, y+4, 2, 4, col);
    /* Fuellung */
    int fill_w = 16 * pct / 100;
    if (fill_w < 1 && pct > 0) fill_w = 1;
    if (fill_w > 0)
        flux_fb_fill_rect(fb, x+2, y+2, fill_w, 8, col);
}

/* WLAN-Icon (3 Boegen, wie auf echten Smartphones). quality: 0-70 */
static void draw_wifi_icon(flux_fb_t *fb, int x, int y, int quality) {
    int bars = (quality >= 55) ? 3 : (quality >= 30) ? 2 : 1;
    uint32_t hi = COL_TEXT_MUTED, lo = 0x334455;
    /* Kleiner Punkt unten */
    flux_fb_fill_rect(fb, x+6, y+11, 3, 3, bars >= 1 ? hi : lo);
    /* Kleiner Bogen */
    flux_fb_fill_rect(fb, x+3,  y+7,  9, 3, bars >= 1 ? hi : lo);
    flux_fb_fill_rect(fb, x+4,  y+5,  7, 2, bars >= 1 ? hi : lo);
    /* Mittlerer Bogen */
    if (bars >= 2) {
        flux_fb_fill_rect(fb, x+1, y+4, 13, 2, hi);
        flux_fb_fill_rect(fb, x+2, y+2, 11, 2, hi);
    } else {
        flux_fb_fill_rect(fb, x+1, y+4, 13, 2, lo);
    }
    /* Grosser Bogen */
    if (bars >= 3) {
        flux_fb_fill_rect(fb, x, y+1, 15, 2, hi);
        flux_fb_fill_rect(fb, x+1, y, 13, 1, hi);
    } else {
        flux_fb_fill_rect(fb, x, y+1, 15, 2, lo);
    }
}

static void draw_statusbar(flux_fb_t *fb) {
    /* Kein sichtbarer Hintergrund -- gleicht sich dem Screen-BG an */
    flux_fb_fill_rect(fb, 0, 0, fb->width, STATUSBAR_H, COL_STATUSBAR);

    /* Zeit links */
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M", &tmv);
    flux_fb_text(fb, 14, (STATUSBAR_H - 14) / 2, buf, COL_TEXT, 2);

    /* Rechts: WiFi + Batterie (kein "Flux"-Label -- wirkt cleaner) */
    int rx = fb->width - 10;

    int bat = read_battery_pct();
    if (bat >= 0) {
        /* Prozentsatz */
        char pbuf[12];
        snprintf(pbuf, sizeof(pbuf), "%d", bat);
        int pw = flux_fb_text_width(pbuf, 2);
        rx -= pw;
        flux_fb_text(fb, rx, (STATUSBAR_H - 14) / 2, pbuf, COL_DIM, 2);
        rx -= 6;
        /* Batterie-Icon */
        rx -= 22;
        draw_battery_icon(fb, rx, (STATUSBAR_H - 12) / 2, bat);
        rx -= 10;
    }

    int wifi = read_wifi_quality();
    if (wifi >= 0) {
        rx -= 15;
        draw_wifi_icon(fb, rx, (STATUSBAR_H - 14) / 2, wifi);
        rx -= 4;
    }
}

static void draw_back_bar(flux_fb_t *fb, const char *label) {
    int y = fb->height - LIST_BACK_H;
    flux_fb_fill_rect(fb, 0, y, fb->width, LIST_BACK_H, COL_SURFACE);
    /* Dezente Trennlinie oben */
    flux_fb_hline(fb, 0, y, fb->width, COL_DIVIDER);
    /* "<" Pfeil-Symbol links */
    int acy = y + LIST_BACK_H / 2;
    draw_thick_line(fb, 18, acy - 8, 10, acy, 2, COL_ACCENT);
    draw_thick_line(fb, 10, acy, 18, acy + 8, 2, COL_ACCENT);
    /* Label */
    int tw = flux_fb_text_width(label, 2);
    flux_fb_text(fb, (fb->width - tw) / 2, y + (LIST_BACK_H - 14) / 2, label, COL_TEXT, 2);
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

    /* Tastatur-Hintergrund */
    int kbd_top = flux_ui_kbd_top(fb);
    int kbd_h   = fb->height - kbd_top;
    flux_fb_fill_rect(fb, 0, kbd_top, fb->width, kbd_h, COL_SURFACE);

    int pad = 5; /* mehr Abstand zwischen Tasten = luftiger */

    for (int i = 0; i < n; i++) {
        int bx = keys[i].x + pad;
        int by = keys[i].y + pad;
        int bw = keys[i].w - 2 * pad;
        int bh = keys[i].h - 2 * pad;

        /* Abgerundete Tasten */
        if (keys[i].is_enter) {
            /* Enter: Gradient-Fill mit Akzentfarbe */
            flux_fb_fill_gradient_v_rounded(fb, bx, by, bw, bh, 8, COL_ACCENT, COL_ACCENT2);
        } else if (keys[i].is_backspace) {
            /* Backspace: Dunkelrot-Tonung */
            fill_round_rect(fb, bx, by, bw, bh, 8, 0x450A0A);
        } else if (keys[i].ch == ' ') {
            /* Leertaste: etwas heller */
            fill_round_rect(fb, bx, by, bw, bh, 8, COL_KEY_SPEC);
        } else {
            fill_round_rect(fb, bx, by, bw, bh, 8, COL_KEY);
        }

        char label[3] = {0};
        if (keys[i].is_backspace) { label[0] = '<'; label[1] = '-'; }
        else if (keys[i].is_enter) { label[0] = 'O'; label[1] = 'K'; }
        else if (keys[i].ch != ' ') { label[0] = keys[i].ch; }

        if (label[0]) {
            uint32_t tcol = keys[i].is_enter ? 0xFFFFFF : COL_TEXT;
            /* Groessere Glyphen (Stufe 3) -- gut lesbare, "echte" Tastatur. */
            int ksc = (keys[i].is_backspace || keys[i].is_enter) ? 2 : 3;
            int tw = flux_fb_text_width(label, ksc);
            int tx = bx + (bw - tw) / 2;
            int ty = by + (bh - ksc * 8) / 2;
            flux_fb_text(fb, tx, ty, label, tcol, ksc);
        }
    }
}

/* ---- Lockscreen ----------------------------------------------------- */

void flux_ui_draw_lock(flux_fb_t *fb) {
    /* Hintergrund: vertikaler Verlauf von COL_BG nach leicht hellerem COL_SURFACE */
    flux_fb_fill_gradient_v(fb, 0, 0, fb->width, fb->height, COL_BG, 0x0B0D14);

    /* Ambientes Akzent-Glimmen hinter der Uhr: weiche, dunkle Indigo-Halo
     * (mehrere Kreise von gross/dunkel nach klein/heller). Ruhig, "lebt". */
    {
        int gx = fb->width / 2, gy = fb->height * 29 / 100;
        fill_circle(fb, gx, gy, 150, 0x090B16);
        fill_circle(fb, gx, gy, 112, 0x0B0D1F);
        fill_circle(fb, gx, gy, 72,  0x0F1130);
    }

    /* Statusleiste nur mit Zeit -- minimal auf dem Lockscreen */
    {
        time_t t = time(NULL);
        struct tm tmv;
        localtime_r(&t, &tmv);
        char buf[16];
        strftime(buf, sizeof(buf), "%H:%M", &tmv);
        flux_fb_text(fb, 14, (STATUSBAR_H - 14) / 2, buf, COL_DIM, 2);
    }

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char clock_buf[16], date_buf[64];
    strftime(clock_buf, sizeof(clock_buf), "%H:%M", &tmv);
    format_german_date(date_buf, sizeof(date_buf), &tmv);

    /* Riesige Zeit in der oberen Bildschirmhaelfte -- der "wow"-Moment */
    int scale_clock = fb->width / 110;
    if (scale_clock < 6) scale_clock = 6;
    if (scale_clock > 10) scale_clock = 10;
    int cw = flux_fb_text_width(clock_buf, scale_clock);
    int clock_y = fb->height * 28 / 100;
    /* Subtiler Schatten hinter der Zeit */
    flux_fb_text(fb, (fb->width - cw) / 2 + 2, clock_y + 2, clock_buf, 0x0D0F18, scale_clock);
    flux_fb_text(fb, (fb->width - cw) / 2, clock_y, clock_buf, COL_TEXT, scale_clock);

    /* Datum -- klein und elegant darunter */
    int dw = flux_fb_text_width(date_buf, 2);
    int date_y = clock_y + scale_clock * 12 + 10;
    flux_fb_text(fb, (fb->width - dw) / 2, date_y, date_buf, COL_TEXT_MUTED, 2);

    /* Benachrichtigungs-Dot (oben rechts): ungelesene Eintraege */
    {
        int cnt = 0;
        FILE *nf = fopen("/tmp/flux_notifications.txt", "r");
        if (nf) {
            char nl[32]; nl[0] = '\0';
            if (fgets(nl, sizeof(nl), nf) && strncmp(nl, "COUNT:", 6) == 0)
                cnt = atoi(nl + 6);
            fclose(nf);
        }
        if (cnt > 0) {
            char badge[12]; snprintf(badge, sizeof(badge), "%d", cnt);
            int bx = fb->width - 32, by = 8;
            fill_circle(fb, bx, by + 10, 10, 0xEF4444);
            int tw = flux_fb_text_width(badge, 2);
            flux_fb_text(fb, bx - tw / 2, by + 3, badge, 0xFFFFFF, 2);
        }
    }

    /* Wetter-Info mit Icon */
    int lock_info_y = date_y + 26; /* Start-Y fuer Info-Chips unter Datum */
    {
        char wline[160] = {0};
        FILE *wf = fopen("/tmp/flux_weather.txt", "r");
        if (wf) { if (!fgets(wline, sizeof(wline), wf)) wline[0] = '\0'; fclose(wf); }
        size_t wl = strlen(wline);
        while (wl > 0 && (wline[wl-1] == '\n' || wline[wl-1] == '\r')) wline[--wl] = '\0';
        if (wline[0]) {
            int wy = date_y + 30;
            weather_cond_t cond = classify_weather(wline);
            draw_weather_icon(fb, (fb->width - flux_fb_text_width(wline, 2)) / 2 - 36, wy, cond);
            flux_fb_text(fb, (fb->width - flux_fb_text_width(wline, 2)) / 2, wy + 5, wline, COL_DIM, 2);
            lock_info_y = wy + 46;
        }
    }

    /* Info-Chip: naechster Alarm (in < 2h) oder naechster Kalendertermin (in < 24h) */
    {
        char chip_text[160] = {0};
        time_t _now = time(NULL);

        /* Naechster Alarm */
        FILE *af = fopen("/tmp/flux_alarms.txt", "r");
        if (af) {
            char aln[256]; time_t best_t = 0;
            while (fgets(aln, sizeof(aln), af)) {
                int y2=0,mo2=0,d2=0,h2=0,mi2=0; char dsc[200]={0};
                if (sscanf(aln, "%4d-%2d-%2d %2d:%2d %199[^\n]",&y2,&mo2,&d2,&h2,&mi2,dsc) >= 5) {
                    struct tm at={0};
                    at.tm_year=y2-1900; at.tm_mon=mo2-1; at.tm_mday=d2;
                    at.tm_hour=h2; at.tm_min=mi2; at.tm_isdst=-1;
                    time_t at_t = mktime(&at);
                    long diff = (long)(at_t - _now);
                    if (diff > 0 && diff <= 7200 && (best_t == 0 || at_t < best_t)) {
                        best_t = at_t;
                        snprintf(chip_text, sizeof(chip_text),
                                 "Wecker %02d:%02d: %s", h2, mi2, dsc);
                    }
                }
            }
            fclose(af);
        }

        /* Fallback: naechster Kalendertermin (bis 24h) */
        if (!chip_text[0]) {
            FILE *cf = fopen("/etc/flux/calendar.txt", "r");
            if (cf) {
                char cln[256]; time_t best_t = 0;
                while (fgets(cln, sizeof(cln), cf)) {
                    if (cln[0]=='#'||cln[0]=='\n') continue;
                    struct tm ev={0}; char dsc[200]={0};
                    if (sscanf(cln, "%4d-%2d-%2d %2d:%2d %199[^\n]",
                               &ev.tm_year,&ev.tm_mon,&ev.tm_mday,
                               &ev.tm_hour,&ev.tm_min,dsc) >= 5) {
                        ev.tm_year-=1900; ev.tm_mon-=1; ev.tm_isdst=-1;
                        time_t ev_t = mktime(&ev);
                        long diff = (long)(ev_t - _now);
                        if (diff > 0 && diff <= 86400 && (best_t == 0 || ev_t < best_t)) {
                            best_t = ev_t;
                            struct tm bt; localtime_r(&best_t, &bt);
                            snprintf(chip_text, sizeof(chip_text),
                                     "Termin %02d:%02d: %s", bt.tm_hour, bt.tm_min, dsc);
                        }
                    }
                }
                fclose(cf);
            }
        }

        if (chip_text[0]) {
            int cx2 = 16, cw2 = fb->width - 32, ch2 = 38;
            int cy2 = lock_info_y;
            if (cy2 + ch2 < fb->height * 58 / 100) {
                fill_round_rect(fb, cx2, cy2, cw2, ch2, 8, COL_SURFACE2);
                flux_fb_fill_rect(fb, cx2, cy2, 4, ch2, COL_ACCENT);
                fill_circle(fb, cx2 + 18, cy2 + ch2/2, 5, COL_ACCENT);
                /* text -- trim to fit */
                char display[80];
                snprintf(display, sizeof(display), "%.70s", chip_text);
                flux_fb_text(fb, cx2 + 30, cy2 + (ch2 - 14) / 2, display, COL_TEXT_MUTED, 2);
            }
        }
    }

    /* Proaktive KI-Benachrichtigung -- elegante Karte im unteren Drittel */
    {
        char pline[512] = {0};
        FILE *pf = fopen("/tmp/flux_proactive.txt", "r");
        if (pf) { size_t pn = fread(pline, 1, sizeof(pline)-1, pf); pline[pn] = '\0'; fclose(pf); }
        size_t pl = strlen(pline);
        while (pl > 0 && (pline[pl-1] == '\n' || pline[pl-1] == '\r')) pline[--pl] = '\0';
        if (pline[0]) {
            int card_m = 20;
            int card_x = card_m;
            int card_w = fb->width - 2 * card_m;
            int card_y = fb->height * 60 / 100;
            int card_h = 108;
            /* Glass-Karte: abgerundet, mit Gradient-Top-Border */
            fill_round_rect(fb, card_x, card_y, card_w, card_h, 14, COL_SURFACE2);
            flux_fb_fill_gradient_h(fb, card_x, card_y, card_w, 3, COL_ACCENT, COL_ACCENT2);
            /* Icon + Label */
            fill_circle(fb, card_x + 22, card_y + 20, 8, COL_ACCENT);
            flux_fb_text(fb, card_x + 36, card_y + 12, "Flux KI", COL_ACCENT, 2);
            /* Text */
            const char *p = pline;
            int ty = card_y + 34;
            while (*p && ty < card_y + card_h - 12) {
                char lbuf[48]; int ll = 0;
                while (p[ll] && p[ll] != '\n' && ll < 42) ll++;
                if (ll == 42 && p[ll] && p[ll] != ' ') {
                    int sw = ll;
                    while (sw > 20 && p[sw] != ' ') sw--;
                    if (p[sw] == ' ') ll = sw;
                }
                memcpy(lbuf, p, (size_t)ll); lbuf[ll] = '\0';
                flux_fb_text(fb, card_x + 14, ty, lbuf, COL_TEXT, 2);
                ty += 20;
                p += ll;
                if (*p == ' ' || *p == '\n') p++;
            }
        }
    }

    /* KI-Begruessung (klein, unten) */
    {
        char gline[120] = {0};
        FILE *gf = fopen("/tmp/flux_greeting.txt", "r");
        if (gf) { if (!fgets(gline, sizeof(gline), gf)) gline[0] = '\0'; fclose(gf); }
        size_t gl = strlen(gline);
        while (gl > 0 && (gline[gl-1] == '\n' || gline[gl-1] == '\r')) gline[--gl] = '\0';
        if (gline[0]) {
            int gw = flux_fb_text_width(gline, 2);
            flux_fb_text(fb, (fb->width - gw) / 2, fb->height - 90,
                         gline, COL_DIM, 2);
        }
    }

    /* Wisch-nach-oben Indikator -- stilisierte animierte Linie statt Text */
    {
        int ind_y = fb->height - 42;
        int ind_x = fb->width / 2;
        /* Schloss-Symbol als ruhiger Hinweis "gesperrt -> nach oben wischen". */
        flux_icon_draw(fb, FLUX_ICON_LOCK, ind_x, ind_y - 30, 16, COL_DIM);
        /* Drei nach oben zeigende Pfeile, leicht verblasst */
        for (int i = 0; i < 3; i++) {
            int oy = ind_y - i * 10;
            uint32_t ac = (i == 0) ? COL_ACCENT :
                          (i == 1) ? 0x4A4CAF : 0x2F3070;
            /* V-Form */
            draw_thick_line(fb, ind_x - 12, oy + 8, ind_x, oy, 2, ac);
            draw_thick_line(fb, ind_x, oy, ind_x + 12, oy + 8, 2, ac);
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

/* Eingangsanimation der Schnellzugriff-Knoepfe: Anzahl voll sichtbarer
 * Knoepfe und Aufpopp-Skalierung des hereinkommenden. Standard = alle voll. */
static int s_quick_shown = 4;
static int s_quick_grow  = 100;
void flux_ui_set_quick_reveal(int shown, int grow_pct) {
    s_quick_shown = shown; s_quick_grow = grow_pct;
}

/* Kleine Navigations-Symbole fuer die Schnellzugriff-Leiste. */
typedef enum { NAV_GEAR, NAV_FOLDER, NAV_CALENDAR, NAV_PERSON } nav_icon_t;

static void draw_nav_icon(flux_fb_t *fb, nav_icon_t kind, int cx, int cy, int s,
                          uint32_t col) {
    if (s < 6) return;
    /* Echte Lucide-Vektor-Icons statt der frueheren Strich-Zeichnung. */
    flux_icon_t id = FLUX_ICON_SETTINGS;
    switch (kind) {
        case NAV_GEAR:     id = FLUX_ICON_SETTINGS; break;
        case NAV_FOLDER:   id = FLUX_ICON_FOLDER;   break;
        case NAV_CALENDAR: id = FLUX_ICON_CALENDAR; break;
        case NAV_PERSON:   id = FLUX_ICON_USER;     break;
    }
    flux_icon_draw(fb, id, cx, cy, s, col);
}

static void draw_quickrow(flux_fb_t *fb) {
    int y = STATUSBAR_H;
    int bw = fb->width / 4;
    /* Hintergrund der gesamten Schnellzugriff-Leiste */
    flux_fb_fill_rect(fb, 0, y, fb->width, QUICKROW_H, COL_SURFACE);
    flux_fb_hline(fb, 0, y + QUICKROW_H - 1, fb->width, COL_DIVIDER);

    static const char *labels[4] = { "Einst.", "Dateien", "Kalend.", "Kontakte" };
    static const nav_icon_t icons[4] = { NAV_GEAR, NAV_FOLDER, NAV_CALENDAR, NAV_PERSON };

    for (int i = 0; i < 4; i++) {
        int grow = (i < s_quick_shown) ? 100 : (i == s_quick_shown ? s_quick_grow : 0);
        if (grow <= 0) continue;

        int bx   = i * bw;
        int pill_w = bw - 10;
        int pill_h = QUICKROW_H - 12;
        int pill_x = bx + 5;
        int pill_y = y + 6;

        /* Icon-first: zentriertes Lucide-Symbol, kein Textlabel mehr --
         * Zahnrad/Ordner/Kalender/Person sind selbsterklaerend. */
        fill_round_rect(fb, pill_x, pill_y, pill_w, pill_h, pill_h / 2, COL_SURFACE3);
        int isz = 22 * grow / 100;
        draw_nav_icon(fb, icons[i], pill_x + pill_w / 2, pill_y + pill_h / 2,
                      isz, COL_ACCENT);
        (void)labels;
    }
}

int flux_ui_quickrow_hit(const flux_fb_t *fb, int x, int y) {
    if (y < STATUSBAR_H || y >= STATUSBAR_H + QUICKROW_H) return 0;
    int bw = fb->width / 4;
    return (x / bw) + 1; /* 1=Einst, 2=Dateien, 3=Kalender, 4=Kontakte */
}

/* ---- Wetter-Widget ---------------------------------------------------- */

#define WEATHER_CACHE  "/tmp/flux_weather.txt"

/* Pixel-Art-Ikone (28x28) fuer verschiedene Wetterbedingungen */

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

#define BUBBLE_PAD_X 16
#define BUBBLE_PAD_Y 12
#define BUBBLE_RADIUS 12   /* Eckenradius der Blasen */

/* Zeichnet eine Nachrichtenblase, gibt den y-Wert direkt unter der Blase
 * plus 12px Abstand zurueck. right_align: 1=Nutzer rechts, 0=KI links. */
static int draw_bubble(flux_fb_t *fb, const char *text, int y,
                        int max_w, uint32_t col, int scale, int line_h,
                        int right_align) {
    (void)col;  /* Blasen-Stil haengt an right_align (Gradient bzw. KI-Streifen) */
    int margin = 14;
    int text_w = max_w - 2 * BUBBLE_PAD_X;
    int text_h = measure_wrapped_height(text_w, text, scale, line_h);
    if (text_h <= 0) text_h = line_h;
    int bubble_h = text_h + 2 * BUBBLE_PAD_Y;
    int bx = right_align ? (fb->width - max_w - margin) : margin;

    if (right_align) {
        /* User-Blase: Indigo-Gradient */
        flux_fb_fill_gradient_v_rounded(fb, bx, y, max_w, bubble_h,
                                        BUBBLE_RADIUS, COL_BUBBLE_USER,
                                        0x3730A3);
    } else {
        /* KI-Blase: dunkler Hintergrund + linker Akzent-Streifen */
        fill_round_rect(fb, bx, y, max_w, bubble_h, BUBBLE_RADIUS, COL_BUBBLE_AI);
        /* 2px linke Akzentlinie als "KI-Indikator" */
        flux_fb_fill_rect(fb, bx, y + BUBBLE_RADIUS/2,
                          2, bubble_h - BUBBLE_RADIUS, COL_ACCENT);
    }

    draw_wrapped(fb, bx + BUBBLE_PAD_X, y + BUBBLE_PAD_Y, text_w,
                 text, COL_TEXT, scale, line_h);
    return y + bubble_h + 12;
}

/* ---- Hilfsfunktion: Eingabeleiste zeichnen (Assistent + Bearbeiten) -- */

/* Symbole der Eingabeleiste: Kopieren, Einfuegen, Mikrofon, Haken (OK).
 * bg ist die Knopf-Hintergrundfarbe (fuer "ausgestanzte" Innenflaechen). */
typedef enum { INICON_COPY, INICON_PASTE, INICON_MIC, INICON_CHECK } input_icon_t;

static void draw_input_icon(flux_fb_t *fb, input_icon_t icon, int cx, int cy,
                            int s, uint32_t col, uint32_t bg) {
    (void)bg;   /* Lucide-Icons sind transparente Masken -- kein Hintergrund noetig */
    flux_icon_t id = FLUX_ICON_CHECK;
    switch (icon) {
        case INICON_COPY:  id = FLUX_ICON_COPY;      break;
        case INICON_PASTE: id = FLUX_ICON_CLIPBOARD; break;
        case INICON_MIC:   id = FLUX_ICON_MIC;       break;
        case INICON_CHECK: id = FLUX_ICON_CHECK;     break;
    }
    flux_icon_draw(fb, id, cx, cy, s, col);
}

static void draw_input_bar(flux_fb_t *fb, int input_y, const char *prompt_text,
                             int show_mic) {
    /* Hintergrund der Eingabeleiste */
    flux_fb_fill_rect(fb, 0, input_y, fb->width, INPUT_BAR_H, COL_SURFACE);
    flux_fb_hline(fb, 0, input_y, fb->width, COL_DIVIDER);

    int icy = input_y + INPUT_BAR_H / 2;

    /* Send/Mic-Knopf rechts: gefuellter Kreis mit Gradient */
    int btn_r  = INPUT_BAR_H / 2 - 8;
    int btn_cx = fb->width - btn_r - 10;
    int btn_cy = input_y + INPUT_BAR_H / 2;
    /* Kreis-Gradient (Indigo -> Violet) */
    for (int dy = -btn_r; dy <= btn_r; dy++) {
        for (int dx = -btn_r; dx <= btn_r; dx++) {
            if (dx*dx + dy*dy <= btn_r*btn_r) {
                int t = (dy + btn_r) * 100 / (2 * btn_r + 1);
                uint32_t c = (t < 50) ? COL_ACCENT : COL_ACCENT2;
                flux_fb_set_px(fb, btn_cx + dx, btn_cy + dy, c);
            }
        }
    }
    if (show_mic) draw_input_icon(fb, INICON_MIC,   btn_cx, btn_cy, 22, 0xFFFFFF, COL_ACCENT);
    else          draw_input_icon(fb, INICON_CHECK, btn_cx, btn_cy, 22, 0xFFFFFF, COL_ACCENT);

    /* Einfuegen + Kopieren: kleine Icon-Knoepfe links vom Send-Button */
    int paste_x = btn_cx - btn_r - CLIP_BTN_W - 4;
    fill_round_rect(fb, paste_x, input_y + 12, CLIP_BTN_W, INPUT_BAR_H - 24, 6, COL_SURFACE3);
    draw_input_icon(fb, INICON_PASTE, paste_x + CLIP_BTN_W/2, icy, 20, COL_DIM, COL_SURFACE3);

    int copy_x = paste_x - CLIP_BTN_W - 4;
    fill_round_rect(fb, copy_x, input_y + 12, CLIP_BTN_W, INPUT_BAR_H - 24, 6, COL_SURFACE3);
    draw_input_icon(fb, INICON_COPY, copy_x + CLIP_BTN_W/2, icy, 20, COL_DIM, COL_SURFACE3);

    /* Eingabefeld: abgerundete Box von links bis zu den Knoepfen */
    int field_x  = 10;
    int field_w  = copy_x - field_x - 6;
    int field_y  = input_y + 10;
    int field_h  = INPUT_BAR_H - 20;
    fill_round_rect(fb, field_x, field_y, field_w, field_h, field_h / 2, COL_SURFACE2);

    if (prompt_text && prompt_text[0]) {
        int avail = field_w - 24;
        const char *s = prompt_text;
        while (*s && flux_fb_text_width(s, 2) > avail) s++;
        flux_fb_text(fb, field_x + 14, field_y + (field_h - 14) / 2, s, COL_TEXT, 2);
    } else {
        flux_fb_text(fb, field_x + 14, field_y + (field_h - 14) / 2,
                     "Schreib etwas...", COL_DIM, 2);
    }
}

/* ---- Tastatur-Sichtbarkeit (nur Assistent) --------------------------
 * Die Tastatur ist auf dem Assistenten NICHT mehr permanent: Tippen auf
 * das Eingabefeld blendet sie ein, Wischen blendet sie aus -- so bleibt
 * Platz fuer lange Gespraeche. Andere Texteingabe-Screens (Bearbeiten,
 * Suche) behalten ihre permanente Tastatur. */
static int s_kbd_open = 0;
void flux_ui_set_kbd_open(int open) { s_kbd_open = open ? 1 : 0; }
int  flux_ui_kbd_is_open(void) { return s_kbd_open; }

#define SUGGEST_BAR_H 50   /* Vorschlags-/Werkzeugleiste ueber der Tastatur */

/* y-Oberkante der Assistenten-Eingabeleiste je nach Tastatur-Zustand. */
static int assist_input_y(const flux_fb_t *fb) {
    if (s_kbd_open) return flux_ui_kbd_top(fb) - SUGGEST_BAR_H - INPUT_BAR_H;
    return fb->height - INPUT_BAR_H;
}

/* ---- Autovervollstaendigung ------------------------------------------
 * Kleines, lokales deutsches Wortlexikon fuer Prefix-Vorschlaege -- keine
 * Cloud, kein Lernen. Deckt haeufige Woerter + die Schnellstart-Kontexte
 * (Mail/Termin/Wecker/Suche) ab. */
static const char *s_dict[] = {
    "ich","du","ist","eine","einen","und","das","der","die","mir","mich",
    "bitte","danke","hallo","was","wie","wann","wo","kannst","helfen",
    "schreibe","E-Mail","an","Nachricht","Termin","erstelle","Wecker",
    "stell","um","Uhr","morgen","heute","Abend","suche","nach","im","Web",
    "zeige","oeffne","Kalender","Einstellungen","Dateien","Notiz","mach",
    "erinnere","spaeter","Minuten","Stunden","Liste","fuer","mit","zum",
};
#define DICT_N ((int)(sizeof(s_dict)/sizeof(s_dict[0])))

/* Bis zu 3 Vorschlaege fuer den zuletzt getippten Wortanfang. out[i] zeigt
 * auf statischen Lexikon-Speicher. Gibt die Anzahl zurueck. Wird von der
 * Zeichenroutine UND von main.c (beim Antippen) genutzt -- gleiche Liste. */
int flux_ui_kbd_words(const char *input, const char *out[3]) {
    const char *tok = input ? input : "";
    for (const char *p = tok; *p; p++) if (*p == ' ') tok = p + 1;
    int tlen = (int)strlen(tok), n = 0;
    if (tlen == 0) {
        static const char *starters[3] = { "Ich", "Was", "Wie" };
        for (int i = 0; i < 3; i++) out[n++] = starters[i];
        return n;
    }
    for (int i = 0; i < DICT_N && n < 3; i++) {
        int match = 1;
        for (int j = 0; j < tlen; j++) {
            char a = tok[j], b = s_dict[i][j];
            if (!b) { match = 0; break; }
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match && (int)strlen(s_dict[i]) > tlen) out[n++] = s_dict[i];
    }
    return n;
}

/* Geometrie der Vorschlagsleiste: Woerter links, 3 Icons (Kopieren,
 * Einfuegen, Voice) rechts. */
#define STRIP_ICON_SLOT 46
static void strip_layout(const flux_fb_t *fb, int *sy, int *word_w, int *icon_x0) {
    *sy = flux_ui_kbd_top(fb) - SUGGEST_BAR_H;
    *icon_x0 = fb->width - 3 * STRIP_ICON_SLOT;
    *word_w = *icon_x0;
}

static void draw_suggest_strip(flux_fb_t *fb, const char *input) {
    int sy, word_w, icon_x0;
    strip_layout(fb, &sy, &word_w, &icon_x0);
    flux_fb_fill_rect(fb, 0, sy, fb->width, SUGGEST_BAR_H, COL_SURFACE);
    flux_fb_hline(fb, 0, sy, fb->width, COL_DIVIDER);

    const char *words[3]; int wn = flux_ui_kbd_words(input, words);
    int col = word_w / 3;
    for (int i = 0; i < 3; i++) {
        if (i > 0) flux_fb_vline(fb, i * col, sy + 12, SUGGEST_BAR_H - 24, COL_DIVIDER);
        if (i < wn) {
            int tw = flux_fb_text_width(words[i], 2);
            flux_fb_text(fb, i * col + (col - tw) / 2, sy + (SUGGEST_BAR_H - 16) / 2,
                         words[i], COL_TEXT, 2);
        }
    }
    flux_fb_vline(fb, icon_x0, sy + 12, SUGGEST_BAR_H - 24, COL_DIVIDER);
    int mid = sy + SUGGEST_BAR_H / 2, h = STRIP_ICON_SLOT;
    flux_icon_draw(fb, FLUX_ICON_COPY,      icon_x0 + h / 2,         mid, 22, COL_TEXT_MUTED);
    flux_icon_draw(fb, FLUX_ICON_CLIPBOARD, icon_x0 + h + h / 2,     mid, 22, COL_TEXT_MUTED);
    flux_icon_draw(fb, FLUX_ICON_MIC,       icon_x0 + 2 * h + h / 2, mid, 22, COL_ACCENT);
}

/* Assistenten-Eingabeleiste: nur das Feld (Tippen oeffnet die Tastatur). */
static void draw_assist_input(flux_fb_t *fb, int input_y, const char *input) {
    flux_fb_fill_rect(fb, 0, input_y, fb->width, INPUT_BAR_H, COL_SURFACE);
    flux_fb_hline(fb, 0, input_y, fb->width, COL_DIVIDER);
    int fx = 12, fy = input_y + 12, fw = fb->width - 24, fh = INPUT_BAR_H - 24;
    fill_round_rect(fb, fx, fy, fw, fh, fh / 2, COL_SURFACE2);
    if (input && input[0]) {
        int avail = fw - 28;
        const char *s = input;
        while (*s && flux_fb_text_width(s, 2) > avail) s++;
        flux_fb_text(fb, fx + 16, fy + (fh - 16) / 2, s, COL_TEXT, 2);
    } else {
        flux_fb_text(fb, fx + 16, fy + (fh - 16) / 2, "Schreib etwas...", COL_DIM, 2);
    }
}

int flux_ui_input_field_hit(const flux_fb_t *fb, int x, int y) {
    int iy = assist_input_y(fb);
    (void)x;
    return (y >= iy && y < iy + INPUT_BAR_H);
}

flux_strip_hit_t flux_ui_strip_hit(const flux_fb_t *fb, int x, int y, int *word_idx) {
    if (!s_kbd_open) return FLUX_STRIP_NONE;
    int sy, word_w, icon_x0;
    strip_layout(fb, &sy, &word_w, &icon_x0);
    if (y < sy || y >= sy + SUGGEST_BAR_H) return FLUX_STRIP_NONE;
    if (x < icon_x0) { if (word_idx) *word_idx = (x * 3) / (word_w > 0 ? word_w : 1); return FLUX_STRIP_WORD; }
    int s = (x - icon_x0) / STRIP_ICON_SLOT;
    if (s <= 0) return FLUX_STRIP_COPY;
    if (s == 1) return FLUX_STRIP_PASTE;
    return FLUX_STRIP_VOICE;
}

/* ---- Vorschlags-Chips (leerer Assistenten-Zustand) ------------------ *
 * Icon-first Schnellstart-Aktionen. Geometrie wird von Zeichnen UND
 * Hit-Test geteilt (2x2-Raster), damit Taps nie daneben landen. */
#define SUGGEST_N 4
static const struct { flux_icon_t icon; const char *label; } s_suggest[SUGGEST_N] = {
    { FLUX_ICON_MAIL,      "Mail"    },
    { FLUX_ICON_BELL_RING, "Wecker"  },
    { FLUX_ICON_SEARCH,    "Suche"   },
    { FLUX_ICON_CALENDAR,  "Termin"  },
};

static int suggest_geom(const flux_fb_t *fb, int i, int *x, int *y, int *w, int *h) {
    int input_y  = assist_input_y(fb);
    int chat_top = STATUSBAR_H + QUICKROW_H + 10;
    int center_y = chat_top + (input_y - chat_top) / 2;
    int top_y    = center_y + 88;
    int margin = 30, gap = 12, cols = 2, chh = 46;
    int cw = (fb->width - 2 * margin - gap) / cols;
    int row = i / cols, col = i % cols;
    *w = cw; *h = chh;
    *x = margin + col * (cw + gap);
    *y = top_y + row * (chh + gap);
    return SUGGEST_N;
}

/* Gibt 1..SUGGEST_N bei Treffer eines Vorschlags-Chips zurueck, sonst 0.
 * Der Aufrufer prueft selbst, ob der leere Zustand aktiv ist. */
int flux_ui_suggest_hit(const flux_fb_t *fb, int x, int y) {
    for (int i = 0; i < SUGGEST_N; i++) {
        int bx, by, bw, bh;
        suggest_geom(fb, i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) return i + 1;
    }
    return 0;
}

void flux_ui_draw_assistant(flux_fb_t *fb, const char *last_q,
                              const char *input, const char *answer, int thinking) {
    flux_fb_clear(fb, COL_BG);

    /* Schlanker Header: "Flux" + Indigo-Puls-Punkt */
    flux_fb_fill_rect(fb, 0, 0, fb->width, STATUSBAR_H, COL_SURFACE);
    flux_fb_hline(fb, 0, STATUSBAR_H - 1, fb->width, COL_DIVIDER);
    /* Zeit links */
    {
        time_t t = time(NULL); struct tm tmv; localtime_r(&t, &tmv);
        char buf[8]; strftime(buf, sizeof(buf), "%H:%M", &tmv);
        flux_fb_text(fb, 12, (STATUSBAR_H - 14) / 2, buf, COL_DIM, 2);
    }
    /* "Flux" zentriert */
    {
        const char *title = "Flux";
        int tw = flux_fb_text_width(title, 2);
        flux_fb_text(fb, (fb->width - tw) / 2, (STATUSBAR_H - 14) / 2, title, COL_TEXT, 2);
        /* Pulsierender Akzent-Punkt rechts neben "Flux" */
        fill_circle(fb, (fb->width + tw) / 2 + 8, STATUSBAR_H / 2, 4, COL_ACCENT);
    }
    /* Batterie rechts */
    {
        int bat = read_battery_pct();
        if (bat >= 0) {
            char pbuf[12]; snprintf(pbuf, sizeof(pbuf), "%d", bat);
            int pw = flux_fb_text_width(pbuf, 2);
            int rx = fb->width - 10 - pw;
            flux_fb_text(fb, rx, (STATUSBAR_H - 14) / 2, pbuf, COL_DIM, 2);
        }
    }

    draw_quickrow(fb);

    int input_y   = assist_input_y(fb);
    int chat_top  = STATUSBAR_H + QUICKROW_H + 10;

    int bubble_max_w = fb->width * 5 / 6;
    int cy = chat_top;

    int has_q = (last_q && *last_q);
    int has_a = (answer && *answer);

    if (!has_q && !thinking && !has_a && !s_kbd_open) {
        /* Leerer Zustand: zentiertes KI-Symbol + Einladungstext */
        int center_y = chat_top + (input_y - chat_top) / 2;

        /* Grosses Akzent-Logo: Funken-Symbol (KI) im Gradient-Kreis. */
        int logo_r = 38;
        int logo_cx = fb->width / 2;
        int logo_cy = center_y - 34;
        for (int dy = -logo_r; dy <= logo_r; dy++) {
            for (int dx = -logo_r; dx <= logo_r; dx++) {
                if (dx*dx + dy*dy <= logo_r*logo_r) {
                    int t = (dy + logo_r) * 100 / (2 * logo_r + 1);
                    uint32_t c = (t < 50) ? COL_ACCENT : COL_ACCENT2;
                    flux_fb_set_px(fb, logo_cx + dx, logo_cy + dy, c);
                }
            }
        }
        flux_icon_draw(fb, FLUX_ICON_SPARKLES, logo_cx, logo_cy, 38, 0xFFFFFF);

        /* Haupt-Einladungstext */
        const char *h = "Wie kann ich helfen?";
        int hw = flux_fb_text_width(h, 3);
        flux_fb_text(fb, (fb->width - hw) / 2, center_y + 18, h, COL_TEXT, 3);

        /* Sub-Text */
        const char *sub = "Frag mich etwas oder waehle einen Vorschlag.";
        int sw = flux_fb_text_width(sub, 2);
        if (sw > fb->width - 40)
            draw_wrapped(fb, 20, center_y + 48, fb->width - 40, sub, COL_DIM, 2, 22);
        else
            flux_fb_text(fb, (fb->width - sw) / 2, center_y + 48, sub, COL_DIM, 2);

        /* Vorschlags-Chips (Icon + Label) -- icon-first Schnellstart. */
        for (int i = 0; i < SUGGEST_N; i++) {
            int bx, by, bw, bh;
            suggest_geom(fb, i, &bx, &by, &bw, &bh);
            fill_round_rect(fb, bx, by, bw, bh, bh / 2, COL_SURFACE2);
            int isz = 20, icx = bx + 22, icy = by + bh / 2;
            flux_icon_draw(fb, s_suggest[i].icon, icx, icy, isz, COL_ACCENT);
            flux_fb_text(fb, icx + isz / 2 + 12, icy - 7, s_suggest[i].label,
                         COL_TEXT_MUTED, 2);
        }

    } else {
        /* Nutzer-Blase rechts */
        if (has_q)
            cy = draw_bubble(fb, last_q, cy, bubble_max_w, COL_BUBBLE_USER, 2, 24, 1);

        /* KI-Blase links oder Lade-Animation */
        if (thinking) {
            /* Lebendiger Denke-Indikator: Funken-Icon + Schimmer-Balken.
             * Ein wandernder Lichtpunkt laeuft durch den Balken -- ruhig,
             * "lebt", reduziert die gefuehlte Wartezeit. */
            int btop = cy, bh = 52, bwid = 168;
            fill_round_rect(fb, 14, btop, bwid, bh, BUBBLE_RADIUS, COL_BUBBLE_AI);
            flux_fb_fill_rect(fb, 14, btop + BUBBLE_RADIUS/2, 2, bh - BUBBLE_RADIUS, COL_ACCENT);
            flux_icon_draw(fb, FLUX_ICON_SPARKLES, 14 + 24, btop + bh/2, 18, COL_ACCENT);
            /* Schimmer-Balken */
            int sbx = 14 + 44, sby = btop + bh/2 - 3, sbw = bwid - 58, sbh = 6;
            fill_round_rect(fb, sbx, sby, sbw, sbh, 3, COL_SURFACE3);
            float ph = flux_shimmer(flux_now_ms(), 1100);
            int hlw = sbw / 3;
            int hlx = sbx + (int)(ph * (sbw + hlw)) - hlw;
            int cx0 = hlx < sbx ? sbx : hlx;
            int cx1 = (hlx + hlw) > (sbx + sbw) ? (sbx + sbw) : (hlx + hlw);
            if (cx1 > cx0)
                flux_fb_fill_gradient_h(fb, cx0, sby, cx1 - cx0, sbh, COL_ACCENT, COL_ACCENT2);
        } else if (has_a) {
            draw_bubble(fb, answer, cy, bubble_max_w, COL_BUBBLE_AI, 2, 24, 0);
        }
    }

    /* Eingabeleiste (nur Feld) + bei offener Tastatur die Vorschlagsleiste. */
    draw_assist_input(fb, input_y, input);
    if (s_kbd_open) {
        draw_suggest_strip(fb, input);
        draw_keyboard(fb);
    }
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

/* Sende-Symbol (Papierflieger), echtes Lucide-Icon. */
static void draw_send_arrow(flux_fb_t *fb, int cx, int cy, int s, uint32_t col) {
    flux_icon_draw(fb, FLUX_ICON_SEND, cx, cy, s, col);
}

/* ---- Agent-Zuordnung (indirekte Spiegelung der Subagenten) ----------
 * Flux delegiert Aktionen intern an spezialisierte Faehigkeiten. Die UI
 * zeigt nur *welche* Faehigkeit gerade ausfuehrt -- rein informativ, der
 * Nutzer verwaltet diese Agenten nie selbst (siehe docs/DESIGN.md). */
static void agent_for_action(const char *type_label, flux_icon_t *icon,
                             const char **name) {
    if (strcmp(type_label, "SMS") == 0) {
        *icon = FLUX_ICON_MESSAGE_CIRCLE; *name = "Messaging-Agent";
    } else if (strcmp(type_label, "Anruf") == 0) {
        *icon = FLUX_ICON_PHONE_CALL;     *name = "Telefon-Agent";
    } else if (strcmp(type_label, "E-Mail") == 0) {
        *icon = FLUX_ICON_MAIL;           *name = "Mail-Agent";
    } else {
        *icon = FLUX_ICON_SPARKLES;       *name = "Flux-Agent";
    }
}

/* Kleine "<Agent> fuehrt aus"-Pille: Icon + Label + pulsierender Akzentpunkt,
 * zentriert um cx_center bei y. Macht Delegation sicht- und spuerbar. */
static void draw_agent_chip(flux_fb_t *fb, int cx_center, int y,
                            flux_icon_t icon, const char *name) {
    int isz = 16, lw = flux_fb_text_width(name, 2);
    int pad = 14, gap = 8, dot = 4, dgap = 9;
    int w = pad + isz + gap + lw + dgap + dot * 2 + pad;
    int h = 30;
    int x = cx_center - w / 2, mid = y + h / 2;
    fill_round_rect(fb, x, y, w, h, h / 2, COL_SURFACE3);
    flux_icon_draw(fb, icon, x + pad + isz / 2, mid, isz, COL_ACCENT);
    flux_fb_text(fb, x + pad + isz + gap, mid - 7, name, COL_TEXT_MUTED, 2);
    /* Akzentpunkt rechts -- "lebt" via Pulsieren (Aufrufer ruft pro Frame). */
    float pulse = flux_pulse(flux_now_ms(), 1600);
    fill_circle(fb, x + pad + isz + gap + lw + dgap + dot, mid,
                dot + (int)(pulse * 1.5f), COL_ACCENT);
}

/* Drei gleich breite Symbol-Knoepfe unten: Abbrechen (X) | Bearbeiten
 * (Stift) | Senden (Haken). Icon-first statt Textknoepfe. */
static void build_confirm_buttons(const flux_fb_t *fb, btn_geom_t out[3]) {
    int h = 96;
    int y = fb->height - h;
    int w = fb->width / 3;
    out[0] = (btn_geom_t){ 0,     y, w,               h }; /* Abbrechen (X)     */
    out[1] = (btn_geom_t){ w,     y, w,               h }; /* Bearbeiten (Stift)*/
    out[2] = (btn_geom_t){ 2 * w, y, fb->width - 2*w, h }; /* Senden (Haken)    */
}

/* Gemeinsame Geometrie der antippbaren Zeilen -- von Draw UND Hit genutzt,
 * damit beide nie auseinanderlaufen. */
static void confirm_layout(const flux_fb_t *fb, int has_subject,
                           int *card_x, int *card_y, int *card_w, int *card_h,
                           int *to_y, int *subj_y, int *div_y, int *body_y) {
    btn_geom_t btn[3];
    build_confirm_buttons(fb, btn);
    *card_x = 10;
    /* Platz oben fuer: grosses Aktions-Icon + Kopfzeile + Agent-Pille. */
    *card_y = STATUSBAR_H + 168;
    *card_w = fb->width - 2 * (*card_x);
    *card_h = btn[0].y - *card_y - 10;
    *to_y   = *card_y + 16;
    *subj_y = *to_y + 36;
    *div_y  = (has_subject ? *subj_y : *to_y) + 36;
    *body_y = *div_y + 12;
}

/* Gemerkt, ob der zuletzt gezeichnete Bestaetigungs-Dialog ein
 * Flugmodus-Schalter ist (feldlos, keine Zeilen bearbeitbar). Wird von
 * flux_ui_confirm_hit gelesen -- gleiche Idee wie s_edit_title. */
static int s_confirm_is_flight = 0;

void flux_ui_draw_confirm(flux_fb_t *fb, const char *type_label,
                           const char *to, const char *subject, const char *body) {
    /* Hintergrund mit Dimm-Overlay-Feeling */
    flux_fb_fill_gradient_v(fb, 0, 0, fb->width, fb->height, 0x040508, COL_BG);
    draw_statusbar(fb);

    /* Flugmodus: feldloser Schalter -- eigene, einfache Darstellung. */
    s_confirm_is_flight = (strcmp(type_label, "flight") == 0);
    if (s_confirm_is_flight) {
        int on = (body && body[0] == 'a' && body[1] == 'n'); /* "an" */
        char header[64];
        snprintf(header, sizeof(header), "Flugmodus %s?", on ? "einschalten" : "ausschalten");
        int hw = flux_fb_text_width(header, 3);
        flux_fb_text(fb, (fb->width - hw) / 2, STATUSBAR_H + 16, header, COL_TEXT, 3);

        int card_x = 10, card_y = STATUSBAR_H + 70;
        int card_w = fb->width - 2 * card_x;
        btn_geom_t fb_btn[3];
        build_confirm_buttons(fb, fb_btn);
        int card_h = fb_btn[0].y - card_y - 8;
        fill_round_rect(fb, card_x, card_y, card_w, card_h, 16, COL_SURFACE2);
        flux_fb_fill_gradient_h(fb, card_x + 16, card_y, card_w - 32, 2, COL_ACCENT, COL_ACCENT2);

        const char *desc = on
            ? "Alle Funkmodule (WLAN, Bluetooth, Mobilfunk) werden blockiert."
            : "Funkmodule werden wieder freigegeben.";
        draw_wrapped(fb, card_x + 18, card_y + 24, card_w - 36, desc, COL_TEXT, 2, 26);

        /* Knoepfe: [Abbruch] [Bestaetigen] -- gleiche Geometrie wie sonst */
        int b1x = fb_btn[0].x + 6, b1y = fb_btn[0].y + 10;
        int b1w = fb_btn[0].w - 12, b1h = fb_btn[0].h - 20;
        fill_round_rect(fb, b1x, b1y, b1w, b1h, 12, COL_SURFACE3);
        flux_fb_hline(fb, b1x + 12, b1y, b1w - 24, 0x3D4A60);
        int lw = flux_fb_text_width("Abbruch", 2);
        flux_fb_text(fb, b1x + (b1w - lw) / 2, b1y + (b1h - 14) / 2, "Abbruch", COL_TEXT_MUTED, 2);

        int b2x = fb_btn[1].x + 6, b2y = fb_btn[1].y + 10;
        int b2w = fb_btn[1].w - 12, b2h = fb_btn[1].h - 20;
        flux_fb_fill_gradient_v_rounded(fb, b2x, b2y, b2w, b2h, 12, 0x059669, 0x10B981);
        int lw2 = flux_fb_text_width("Bestaetigen", 2);
        flux_fb_text(fb, b2x + (b2w - lw2) / 2, b2y + (b2h - 14) / 2, "Bestaetigen", 0xFFFFFF, 2);

        flux_fb_present(fb);
        return;
    }

    int has_subject = (subject && subject[0]) ? 1 : 0;

    /* Welcher Subagent fuehrt aus? -> grosses Symbol + Agent-Pille. */
    flux_icon_t act_icon; const char *agent_name;
    agent_for_action(type_label, &act_icon, &agent_name);

    /* Grosses Aktions-Symbol in einem Akzent-Gradient-Kreis (icon-first). */
    int badge_cx = fb->width / 2, badge_cy = STATUSBAR_H + 52, badge_r = 34;
    for (int dy = -badge_r; dy <= badge_r; dy++)
        for (int dx = -badge_r; dx <= badge_r; dx++)
            if (dx*dx + dy*dy <= badge_r*badge_r) {
                int tt = (dy + badge_r) * 100 / (2 * badge_r + 1);
                flux_fb_set_px(fb, badge_cx + dx, badge_cy + dy,
                               (tt < 50) ? COL_ACCENT : COL_ACCENT2);
            }
    flux_icon_draw(fb, act_icon, badge_cx, badge_cy, 34, 0xFFFFFF);

    /* Kopfzeile (zentriert, unter dem Symbol). */
    char header[80];
    if (strcmp(type_label, "Anruf") == 0)
        snprintf(header, sizeof(header), "Anrufen?");
    else
        snprintf(header, sizeof(header), "%s senden?", type_label);
    int hw = flux_fb_text_width(header, 3);
    flux_fb_text(fb, (fb->width - hw) / 2, badge_cy + badge_r + 8, header, COL_TEXT, 3);

    /* Agent-Pille: zeigt, welche Faehigkeit die Aktion ausfuehrt. */
    draw_agent_chip(fb, fb->width / 2, badge_cy + badge_r + 38, act_icon, agent_name);

    int card_x, card_y, card_w, card_h, to_y, subj_y, div_y, body_y;
    confirm_layout(fb, has_subject, &card_x, &card_y, &card_w, &card_h,
                   &to_y, &subj_y, &div_y, &body_y);

    /* Glass-Morphism-Karte */
    fill_round_rect(fb, card_x, card_y, card_w, card_h, 16, COL_SURFACE2);
    /* Subtile Border */
    for (int i = 0; i < 2; i++) {
        /* Obere + linke Border simulieren */
        flux_fb_hline(fb, card_x + 16, card_y + i, card_w - 32, 0x2A3354);
        flux_fb_vline(fb, card_x + i, card_y + 16, card_h - 32, 0x2A3354);
    }
    /* Akzent-Gradient oben */
    flux_fb_fill_gradient_h(fb, card_x + 16, card_y, card_w - 32, 2, COL_ACCENT, COL_ACCENT2);

    int cx = card_x + 18;

    /* An: */
    flux_fb_text(fb, cx, to_y, "AN", COL_DIM, 1);
    flux_fb_text(fb, cx + 28, to_y - 1,
                 to[0] ? to : "(tippen)", to[0] ? COL_TEXT : COL_ACCENT, 2);

    /* Betreff: (nur Mail) */
    if (has_subject) {
        flux_fb_hline(fb, cx, subj_y - 4, card_w - 36, COL_DIVIDER);
        flux_fb_text(fb, cx, subj_y, "BETREFF", COL_DIM, 1);
        flux_fb_text(fb, cx + 60, subj_y - 1, subject, COL_TEXT, 2);
    }

    /* Trennlinie + Nachrichtentext */
    flux_fb_hline(fb, cx, div_y - 4, card_w - 36, COL_DIVIDER);
    draw_wrapped(fb, cx, body_y, card_w - 36, body[0] ? body : "(tippen)",
                 body[0] ? COL_TEXT : COL_DIM, 2, 24);

    /* Drei Symbol-Knoepfe unten: [X] Abbrechen | [Stift] Bearbeiten |
     * [Haken] Senden. Icon-first, jeder Knopf >= 48px antippbar. */
    btn_geom_t btn[3];
    build_confirm_buttons(fb, btn);
    int m = 8;
    /* Abbrechen (X, neutrale Oberflaeche) */
    {
        int bx = btn[0].x + m, by = btn[0].y + m, bw = btn[0].w - 2*m, bh = btn[0].h - 2*m;
        fill_round_rect(fb, bx, by, bw, bh, 16, COL_SURFACE3);
        flux_icon_draw(fb, FLUX_ICON_X, bx + bw/2, by + bh/2, 26, COL_TEXT_MUTED);
    }
    /* Bearbeiten (Stift, Oberflaeche mit Akzent-Icon) */
    {
        int bx = btn[1].x + m, by = btn[1].y + m, bw = btn[1].w - 2*m, bh = btn[1].h - 2*m;
        fill_round_rect(fb, bx, by, bw, bh, 16, COL_SURFACE3);
        flux_icon_draw(fb, FLUX_ICON_PENCIL, bx + bw/2, by + bh/2, 24, COL_ACCENT);
    }
    /* Senden/Bestaetigen (Haken, gefuellter Gradient) */
    {
        int bx = btn[2].x + m, by = btn[2].y + m, bw = btn[2].w - 2*m, bh = btn[2].h - 2*m;
        flux_fb_fill_gradient_v_rounded(fb, bx, by, bw, bh, 16, COL_ACCENT, COL_ACCENT2);
        flux_icon_draw(fb, FLUX_ICON_CHECK, bx + bw/2, by + bh/2, 30, 0xFFFFFF);
    }

    flux_fb_present(fb);
}

flux_confirm_hit_t flux_ui_confirm_hit(const flux_fb_t *fb, int x, int y, int has_subject) {
    btn_geom_t btn[3];
    build_confirm_buttons(fb, btn);
    if (y >= btn[0].y) {
        int third = fb->width / 3;
        if (x < third)       return FLUX_CONFIRM_CANCEL;
        if (x < 2 * third)   return FLUX_CONFIRM_EDIT;
        return FLUX_CONFIRM_SEND;
    }

    /* Flugmodus-Dialog hat keine bearbeitbaren Zeilen -- Taps oberhalb der
     * Knoepfe ignorieren. */
    if (s_confirm_is_flight) return FLUX_CONFIRM_NONE;

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

static const int *s_setting_icons = NULL;
void flux_ui_set_setting_icons(const int *icons) { s_setting_icons = icons; }

/* Zeichnet ein kleines Symbol (zentriert bei cx,cy, Kantenmass s).
 * Bildet die Einstellungs-Symbole auf echte Lucide-Vektor-Icons ab. */
static void draw_setting_icon(flux_fb_t *fb, int icon, int cx, int cy, int s,
                              uint32_t col) {
    if (s < 6) return;
    flux_icon_t id;
    switch (icon) {
        case FLUX_SICON_LOCK:    id = FLUX_ICON_LOCK;     break;
        case FLUX_SICON_AI:      id = FLUX_ICON_SPARKLES; break;
        case FLUX_SICON_KEY:     id = FLUX_ICON_KEY;      break;
        case FLUX_SICON_CHIP:    id = FLUX_ICON_CPU;      break;
        case FLUX_SICON_MAIL:    id = FLUX_ICON_MAIL;     break;
        case FLUX_SICON_WIFI:    id = FLUX_ICON_WIFI;     break;
        case FLUX_SICON_SEARCH:  id = FLUX_ICON_SEARCH;   break;
        case FLUX_SICON_THEME:   id = FLUX_ICON_PALETTE;  break;
        case FLUX_SICON_CLOCK:   id = FLUX_ICON_CLOCK;    break;
        case FLUX_SICON_SPEAKER: id = FLUX_ICON_VOLUME;   break;
        default: return;
    }
    flux_icon_draw(fb, id, cx, cy, s, col);
}

/* Zeichnet eine Einstellungs-Zeile (Card-Stil). */
static void draw_setting_row(flux_fb_t *fb, list_row_geom_t r, const char *label,
                             const char *value, int icon, int x_off,
                             int grow_pct, int dim) {
    int x = r.x + x_off;
    /* Karten-Hintergrund mit abgerundeten Ecken */
    uint32_t row_col = dim ? 0x0C0E14 : COL_SURFACE;
    fill_round_rect(fb, x, r.y + 2, r.w, r.h - 4, 8, row_col);

    /* Linker Icon-Bereich */
    int text_x = x + 14;
    if (icon && icon != FLUX_SICON_NONE) {
        /* Icon-Hintergrund: kleiner abgerundeter Kreis */
        int icon_s = 28 * grow_pct / 100;
        int icon_cx = x + 26;
        int icon_cy = r.y + r.h / 2;
        if (icon_s >= 8)
            fill_round_rect(fb, icon_cx - icon_s/2 - 2, icon_cy - icon_s/2 - 2,
                            icon_s + 4, icon_s + 4, 6,
                            dim ? 0x141920 : 0x1C2540);
        draw_setting_icon(fb, icon, icon_cx, icon_cy, icon_s,
                          dim ? COL_DIM : COL_ACCENT);
        text_x = x + 58;
    }

    /* Label + Wert vertikal gestapelt */
    flux_fb_text(fb, text_x, r.y + r.h/2 - 16, label,
                 dim ? COL_DIM : COL_TEXT, 2);
    flux_fb_text(fb, text_x, r.y + r.h/2 + 4, value, COL_DIM, 2);

    /* Chevron rechts (> Symbol) */
    if (!dim) {
        int chx = x + r.w - 16;
        int chy = r.y + r.h / 2;
        draw_thick_line(fb, chx - 5, chy - 6, chx + 2, chy, 2, COL_DIM);
        draw_thick_line(fb, chx + 2, chy, chx - 5, chy + 6, 2, COL_DIM);
    }
}

void flux_ui_draw_settings(flux_fb_t *fb, const char **labels, const char **values, int n) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    /* Header mit Gradient-Underline */
    flux_fb_text(fb, 16, STATUSBAR_H + 16, "Einstellungen", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, 16, STATUSBAR_H + 46, 140, 2, COL_ACCENT, COL_ACCENT2);

    list_row_geom_t rows[LIST_MAX_ROWS];
    int rn = build_list_rows(fb, n, rows);
    for (int i = 0; i < rn; i++)
        draw_setting_row(fb, rows[i], labels[i], values[i],
                         s_setting_icons ? s_setting_icons[i] : 0, 0, 100, 0);
    draw_back_bar(fb, "Zurück");
    flux_fb_present(fb);
}

void flux_ui_draw_settings_reveal(flux_fb_t *fb, const char **labels,
                                  const char **values, int n, int shown,
                                  int slide_px, int grow_pct, int dir) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    flux_fb_text(fb, 16, STATUSBAR_H + 16, "Einstellungen", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, 16, STATUSBAR_H + 46, 140, 2, COL_ACCENT, COL_ACCENT2);

    list_row_geom_t rows[LIST_MAX_ROWS];
    int rn = build_list_rows(fb, n, rows);
    for (int i = 0; i < rn && i < shown; i++)
        draw_setting_row(fb, rows[i], labels[i], values[i],
                         s_setting_icons ? s_setting_icons[i] : 0, 0, 100, 0);
    if (shown < rn)
        draw_setting_row(fb, rows[shown], labels[shown], values[shown],
                         s_setting_icons ? s_setting_icons[shown] : 0,
                         dir * slide_px, grow_pct < 10 ? 10 : grow_pct, 1);
    draw_back_bar(fb, "Zurück");
    flux_fb_present(fb);
}

/* ---- WLAN -------------------------------------------------------------- */

void flux_ui_draw_wifi(flux_fb_t *fb, const char *current, const char **names,
                       const char **metas, int n, int scanning, int unavailable) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);
    flux_fb_text(fb, 16, STATUSBAR_H + 16, "WLAN", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, 16, STATUSBAR_H + 46, 80, 2, COL_ACCENT, COL_ACCENT2);

    /* Status-Pill oben rechts */
    {
        const char *status = (current && current[0]) ? "Verbunden" : "Getrennt";
        uint32_t sc = (current && current[0]) ? 0x166534 : 0x3B1414;
        uint32_t tc = (current && current[0]) ? 0x4ADE80 : COL_DANGER;
        int sw = flux_fb_text_width(status, 2);
        int px = fb->width - sw - 28, py = STATUSBAR_H + 18;
        fill_round_rect(fb, px - 8, py - 4, sw + 16, 22, 11, sc);
        flux_fb_text(fb, px, py, status, tc, 2);
    }
    if (current && current[0]) {
        char buf[80]; snprintf(buf, sizeof(buf), "%s", current);
        flux_fb_text(fb, 16, STATUSBAR_H + 48, buf, COL_TEXT_MUTED, 2);
    }

    if (unavailable) {
        draw_wrapped(fb, 16, STATUSBAR_H + TITLE_AREA_H + 8, fb->width - 32,
                     "Kein WLAN-Geraet erkannt (oder wpa_cli fehlt).",
                     COL_DIM, 2, 26);
        draw_back_bar(fb, "Zurück");
        flux_fb_present(fb);
        return;
    }
    if (scanning) {
        flux_fb_text(fb, 16, STATUSBAR_H + TITLE_AREA_H + 8, "Suche Netze...", COL_TEXT_MUTED, 2);
        /* Scan-Animation: 3 Balken */
        for (int b = 0; b < 3; b++) {
            int bx = 16 + b * 16, bh = 8 + b * 6;
            fill_round_rect(fb, bx, STATUSBAR_H + TITLE_AREA_H + 36 - bh, 10, bh, 3, COL_ACCENT);
        }
        draw_back_bar(fb, "Zurück");
        flux_fb_present(fb);
        return;
    }
    if (n == 0) {
        flux_fb_text(fb, 16, STATUSBAR_H + TITLE_AREA_H + 8,
                     "Keine Netze. Erneut tippen.", COL_DIM, 2);
        draw_back_bar(fb, "Aktualisieren");
        flux_fb_present(fb);
        return;
    }

    list_row_geom_t rows[LIST_MAX_ROWS];
    int rn = build_list_rows(fb, n, rows);
    for (int i = 0; i < rn; i++) {
        int is_current = (current && current[0] && strcmp(names[i], current) == 0);
        uint32_t bg = is_current ? 0x0D1A2A : COL_SURFACE;
        fill_round_rect(fb, rows[i].x, rows[i].y + 2, rows[i].w, rows[i].h - 4, 8, bg);
        /* WiFi-Icon links */
        draw_wifi_icon(fb, rows[i].x + 14, rows[i].y + rows[i].h/2 - 7, 50);
        flux_fb_text(fb, rows[i].x + 36, rows[i].y + rows[i].h/2 - 12, names[i],
                     is_current ? COL_ACCENT : COL_TEXT, 2);
        flux_fb_text(fb, rows[i].x + 36, rows[i].y + rows[i].h/2 + 6,
                     metas[i], COL_DIM, 2);
        /* Verbundener Haken */
        if (is_current) {
            draw_thick_line(fb, rows[i].x + rows[i].w - 22,
                            rows[i].y + rows[i].h/2,
                            rows[i].x + rows[i].w - 16,
                            rows[i].y + rows[i].h/2 + 8, 2, COL_SEND);
            draw_thick_line(fb, rows[i].x + rows[i].w - 16,
                            rows[i].y + rows[i].h/2 + 8,
                            rows[i].x + rows[i].w - 6,
                            rows[i].y + rows[i].h/2 - 8, 2, COL_SEND);
        }
    }
    draw_back_bar(fb, "Zurück");
    flux_fb_present(fb);
}

/* ---- Dateien ------------------------------------------------------------ */

#define FILES_DELETE_BTN_H  56
#define FILES_DELETE_BTN_W  120
#define NEWBTN_W            108   /* "+ Ordner"-Knopf -- Draw UND Hit teilen sich diese Masse */
#define NEWBTN_H            36

void flux_ui_draw_files(flux_fb_t *fb, const char *path, const char **names,
                         const char **metas, int n, int truncated, int selected_idx) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    /* Header with gradient underline */
    flux_fb_text(fb, 16, STATUSBAR_H + 10, "Dateien", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, 16, STATUSBAR_H + 40, 90, 2, COL_ACCENT, COL_ACCENT2);
    /* Path breadcrumb */
    flux_fb_text(fb, 16, STATUSBAR_H + 48, path, COL_DIM, 2);
    if (truncated) {
        flux_fb_text(fb, 16, STATUSBAR_H + TITLE_AREA_H - 6,
                     "(+ weitere Eintraege)", COL_DIM, 1);
    }

    /* "Neuer Ordner"-Knopf oben rechts: saubere Pille mit Plus-Icon + Label,
     * kein Akzent-Strich mehr (der wirkte wie ein verirrter Querstrich). */
    {
        int bw = NEWBTN_W, bh = NEWBTN_H;
        int bx = fb->width - bw - 10;
        int by = STATUSBAR_H + 8;
        fill_round_rect(fb, bx, by, bw, bh, bh / 2, COL_SURFACE3);
        const char *nl = "Ordner";
        int icon_w = 16, gap = 6, nlw = flux_fb_text_width(nl, 2);
        int total = icon_w + gap + nlw;
        int sx = bx + (bw - total) / 2, mid = by + bh / 2;
        flux_icon_draw(fb, FLUX_ICON_PLUS, sx + icon_w / 2, mid, icon_w, COL_ACCENT);
        flux_fb_text(fb, sx + icon_w + gap, mid - 8, nl, COL_ACCENT, 2);
    }

    if (n == 0) {
        flux_fb_text(fb, 24, STATUSBAR_H + TITLE_AREA_H + 20,
                     "(Ordner leer)", COL_DIM, 2);
    } else {
        list_row_geom_t rows[LIST_MAX_ROWS];
        int rn = build_list_rows(fb, n, rows);
        for (int i = 0; i < rn; i++) {
            int sel = (i == selected_idx);
            uint32_t bg = sel ? COL_SURFACE3 : COL_SURFACE;
            fill_round_rect(fb, rows[i].x + 6, rows[i].y + 2,
                            rows[i].w - 12, rows[i].h - 4, 8, bg);
            if (sel)
                flux_fb_fill_rect(fb, rows[i].x + 6, rows[i].y + 2,
                                  3, rows[i].h - 4, COL_ACCENT);

            /* Echtes Ordner-/Datei-Icon (icon-first). Ordner traegt die Meta
             * "Ordner" (siehe load_files); alles andere ist eine Datei. */
            int is_dir = (metas && metas[i] && strcmp(metas[i], "Ordner") == 0);
            flux_icon_draw(fb, is_dir ? FLUX_ICON_FOLDER : FLUX_ICON_FILE,
                           rows[i].x + 28, rows[i].y + rows[i].h / 2, 22,
                           is_dir ? COL_ACCENT : COL_TEXT_MUTED);

            /* Name + Meta eng zentriert (wie die Einstellungs-Karten) statt
             * an Ober-/Unterkante verteilt. */
            flux_fb_text(fb, rows[i].x + 52, rows[i].y + rows[i].h / 2 - 16,
                         names[i], COL_TEXT, 2);
            if (metas && metas[i])
                flux_fb_text(fb, rows[i].x + 52, rows[i].y + rows[i].h / 2 + 4,
                             metas[i], COL_DIM, 2);
        }
    }

    /* Loeschen-Knopf */
    if (selected_idx >= 0 && selected_idx < n) {
        int del_y = fb->height - LIST_BACK_H - FILES_DELETE_BTN_H - 8;
        int del_x = fb->width - FILES_DELETE_BTN_W - 10;
        /* Klar gefuellter roter Knopf mit Papierkorb-Icon (statt blassem
         * Block) -- liest sich eindeutig als Aktion. */
        fill_round_rect(fb, del_x, del_y, FILES_DELETE_BTN_W, FILES_DELETE_BTN_H, 14, COL_DANGER);
        const char *dlabel = "Löschen";
        int icon_w = 20, gap = 7, dlw = flux_fb_text_width(dlabel, 2);
        int total = icon_w + gap + dlw;
        int sx = del_x + (FILES_DELETE_BTN_W - total) / 2;
        int mid = del_y + FILES_DELETE_BTN_H / 2;
        flux_icon_draw(fb, FLUX_ICON_TRASH, sx + icon_w / 2, mid, icon_w, 0xFFFFFF);
        flux_fb_text(fb, sx + icon_w + gap, mid - 8, dlabel, 0xFFFFFF, 2);
    }

    draw_back_bar(fb, "Zurück");
    flux_fb_present(fb);
}

int flux_ui_files_new_btn_hit(const flux_fb_t *fb, int x, int y) {
    int bw = NEWBTN_W, bh = NEWBTN_H;
    int bx = fb->width - bw - 10;
    int by = STATUSBAR_H + 8;
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

    /* Geteilte Leiste unten: links "Zurück", rechts "KI fragen".
     * Die KI-Schaltflaeche macht die Dokument-KI sichtbar (vorher nur
     * per Wisch-nach-rechts erreichbar, siehe flux_ui_viewer_hit). */
    {
        int by = fb->height - LIST_BACK_H;
        flux_fb_fill_rect(fb, 0, by, fb->width, LIST_BACK_H, COL_STATUSBAR);
        int split = fb->width * 3 / 5;
        flux_fb_fill_rect(fb, split, by + 10, 1, LIST_BACK_H - 20, COL_DIVIDER);

        const char *back_lbl = "Zurück";
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
    /* Gradient background: very dark top -> surface bottom */
    flux_fb_fill_gradient_v(fb, 0, 0, fb->width, fb->height, 0x050709, COL_SURFACE);

    int y = 28;

    /* --- Grosse Uhrzeit + Datum ---------------------------------------- */
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char clock_buf[16], date_buf[64];
    strftime(clock_buf, sizeof(clock_buf), "%H:%M", &tmv);
    format_german_date(date_buf, sizeof(date_buf), &tmv);

    int cw = flux_fb_text_width(clock_buf, 7);
    flux_fb_text_shadow(fb, (fb->width - cw) / 2, y, clock_buf, COL_TEXT, 7);
    y += 7 * 11 + 6;

    int dw = flux_fb_text_width(date_buf, 3);
    flux_fb_text(fb, (fb->width - dw) / 2, y, date_buf, COL_TEXT_MUTED, 3);
    y += 42;

    /* --- Status-Karte (Batterie + WLAN) -------------------------------- */
    int bat = read_battery_pct();
    int wifi = read_wifi_quality();
    if (bat >= 0 || wifi >= 0) {
        int card_h = 0;
        if (bat >= 0) card_h += 32;
        if (wifi >= 0) card_h += 32;
        card_h += 16;
        fill_round_rect(fb, 12, y, fb->width - 24, card_h, 10, COL_SURFACE2);
        flux_fb_hline(fb, 12, y, fb->width - 24, COL_DIVIDER);
        int iy = y + 8;
        if (bat >= 0) {
            draw_battery_icon(fb, 26, iy + 6, bat);
            char pbuf[32];
            snprintf(pbuf, sizeof(pbuf), "Batterie: %d%%", bat);
            flux_fb_text(fb, 58, iy + 4, pbuf, COL_TEXT, 2);
            iy += 32;
        }
        if (wifi >= 0) {
            draw_wifi_icon(fb, 26, iy + 4, wifi);
            char wbuf[32];
            snprintf(wbuf, sizeof(wbuf), "WLAN: %d%%", wifi * 100 / 70);
            flux_fb_text(fb, 58, iy + 4, wbuf, COL_TEXT, 2);
            iy += 32;
        }
        y += card_h + 10;
    }

    /* --- Wetter-Karte ------------------------------------------------- */
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
            fill_round_rect(fb, 12, y, fb->width - 24, 60, 10, COL_SURFACE2);
            flux_fb_hline(fb, 12, y, fb->width - 24, COL_DIVIDER);
            weather_cond_t cond = classify_weather(weather);
            draw_weather_icon(fb, 22, y + 8, cond);
            draw_wrapped(fb, 60, y + 8, fb->width - 80, weather, COL_TEXT_MUTED, 2, 44);
            y += 70;
        }
    }

    /* --- Alarme -------------------------------------------------------- */
    int any_shown = 0;
    {
        FILE *f = fopen("/tmp/flux_alarms.txt", "r");
        if (f) {
            char line[128];
            int shown = 0;
            while (fgets(line, sizeof(line), f) && shown < 3) {
                size_t l = strlen(line);
                while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r'))
                    line[--l] = '\0';
                if (!line[0]) continue;
                if (shown == 0) {
                    fill_round_rect(fb, 12, y, fb->width - 24, 28 * 3 + 12, 8, COL_SURFACE2);
                    flux_fb_fill_rect(fb, 12, y, 3, 28 * 3 + 12, COL_ACCENT);
                    flux_fb_text(fb, 24, y + 4, "Alarme", COL_ACCENT, 2);
                    y += 28;
                }
                flux_fb_text(fb, 24, y, line, COL_TEXT, 2);
                y += 26; shown++; any_shown = 1;
            }
            fclose(f);
            if (shown) y += 8;
        }
    }

    /* --- Erinnerungen -------------------------------------------------- */
    {
        FILE *f = fopen("/tmp/flux_reminders.txt", "r");
        if (f) {
            char line[128];
            int shown = 0;
            while (fgets(line, sizeof(line), f) && shown < 2) {
                size_t l = strlen(line);
                while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r'))
                    line[--l] = '\0';
                if (!line[0]) continue;
                if (shown == 0) {
                    fill_round_rect(fb, 12, y, fb->width - 24, 28 * 2 + 12, 8, COL_SURFACE2);
                    flux_fb_fill_rect(fb, 12, y, 3, 28 * 2 + 12, 0xF59E0B);
                    flux_fb_text(fb, 24, y + 4, "Erinnerungen", 0xF59E0B, 2);
                    y += 28;
                }
                flux_fb_text(fb, 24, y, line, COL_TEXT, 2);
                y += 26; shown++; any_shown = 1;
            }
            fclose(f);
        }
    }

    /* --- Fehler (neueste zuerst) --------------------------------------- */
    {
        FILE *f = fopen("/tmp/flux_errors.txt", "r");
        if (f) {
            /* Datei ist aelteste->neueste; wir puffern und zeigen die
             * neuesten vier (neueste oben). */
            char buf[10][256];
            int n = 0;
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                size_t l = strlen(line);
                while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r'))
                    line[--l] = '\0';
                if (!line[0]) continue;
                if (n < 10) snprintf(buf[n++], sizeof(buf[0]), "%s", line);
                else {
                    memmove(buf[0], buf[1], 9 * sizeof(buf[0]));
                    snprintf(buf[9], sizeof(buf[0]), "%s", line);
                }
            }
            fclose(f);
            int show = n < 4 ? n : 4;
            if (show > 0) {
                fill_round_rect(fb, 12, y, fb->width - 24, 26 * show + 34, 8, COL_SURFACE2);
                flux_fb_fill_rect(fb, 12, y, 3, 26 * show + 34, 0xF87171);
                flux_fb_text(fb, 24, y + 6, "Fehler", 0xF87171, 2);
                y += 30;
                for (int i = 0; i < show; i++) {  /* neueste zuerst */
                    draw_wrapped(fb, 24, y, fb->width - 48, buf[n - 1 - i],
                                 COL_TEXT_MUTED, 1, 13);
                    y += 26;
                }
                y += 8;
                any_shown = 1;
            }
        }
    }

    /* Leer-Zustand wenn keine Benachrichtigungen */
    if (!any_shown) {
        fill_round_rect(fb, 12, y, fb->width - 24, 64, 10, COL_SURFACE);
        const char *empty = "Keine Benachrichtigungen";
        int ew = flux_fb_text_width(empty, 2);
        flux_fb_text(fb, (fb->width - ew) / 2, y + 24, empty, COL_DIM, 2);
        y += 74;
    }

    /* Hinweis zum Schließen */
    {
        const char *hint = "Tippen zum Schließen";
        int hw = flux_fb_text_width(hint, 2);
        flux_fb_text(fb, (fb->width - hw) / 2, fb->height - 36, hint, COL_DIM, 2);
        flux_fb_hline(fb, (fb->width - hw) / 2, fb->height - 20,
                      hw, COL_DIVIDER);
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

#define CAL_HEADER_H    60
#define CAL_DAYROW_H    24
#define CAL_CELL_W(fb)  ((fb)->width / 7)
#define CAL_CELL_H      64
#define CAL_GRID_ROWS   6   /* feste Hoehe (6 Wochen) -> stabiles Layout */
#define CAL_GRID_TOP(fb) (STATUSBAR_H + CAL_HEADER_H + CAL_DAYROW_H)

/* Akzentfarbe pro Termin: deterministisch aus dem Titel gehasht. So hat
 * jeder Termin eine stabile "Kalender-Farbe" (wie Apples Kalender-Gruppen),
 * ohne Daten zu erfinden. */
static uint32_t cal_event_color(const char *title) {
    static const uint32_t pal[] = {
        0xEF4444, 0xF97316, 0xEAB308, 0x22C55E,
        0x06B6D4, 0x3B82F6, 0xA855F7, 0xEC4899,
    };
    unsigned h = 2166136261u;
    for (const char *p = title; p && *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
    return pal[h % (sizeof(pal) / sizeof(pal[0]))];
}

/* Zerlegt eine Termin-Zeile "YYYY-MM-DD HH:MM Titel". Gibt 1 zurueck wenn
 * ein gueltiger Tag erkannt wurde. *hhmm bekommt "HH:MM" (oder ""), *title
 * zeigt in die Originalzeile hinter die Uhrzeit. */
static int cal_parse_event(const char *s, int *day, char hhmm[6], const char **title) {
    *day = 0; hhmm[0] = '\0'; *title = "";
    if (!s) return 0;
    size_t len = strlen(s);
    if (len < 10) return 0;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) { if (s[i] != '-') return 0; }
        else if (s[i] < '0' || s[i] > '9') return 0;
    }
    *day = (s[8] - '0') * 10 + (s[9] - '0');
    if (*day < 1 || *day > 31) { *day = 0; return 0; }
    if (len >= 16 && s[10] == ' ' &&
        s[11] >= '0' && s[11] <= '9' && s[13] == ':') {
        memcpy(hhmm, s + 11, 5); hhmm[5] = '\0';
        *title = (len > 17) ? s + 17 : "";
    } else {
        *title = (len > 11) ? s + 11 : "";
    }
    return 1;
}

/* Kopiert so viele Zeichen von src nach dst, wie in max_px (bei Skalierung
 * scale) passen. Fuer die knappen Ereignis-Pillen in den Tageszellen. */
static void cal_fit_text(char *dst, size_t cap, const char *src, int max_px, int scale) {
    size_t n = 0;
    char probe[64];
    for (const unsigned char *p = (const unsigned char *)src; *p && n < cap - 1; p++) {
        if (n < sizeof(probe) - 1) { probe[n] = (char)*p; probe[n + 1] = '\0'; }
        if (flux_fb_text_width(probe, scale) > max_px) break;
        dst[n++] = (char)*p;
    }
    dst[n] = '\0';
}

/* Geometrie der vier Kopfzeilen-Knoepfe -- EINZIGE Wahrheit fuer Zeichnen
 * UND Hit-Test, damit Taps nie daneben landen. */
typedef struct { int x, y, w, h; } cal_rect;
static void cal_header_geom(const flux_fb_t *fb, cal_rect *prev, cal_rect *next,
                            cal_rect *today, cal_rect *add) {
    int W = fb->width;
    int cy = STATUSBAR_H + CAL_HEADER_H / 2;
    int bs = 34;
    int by = cy - bs / 2;
    add->w  = add->h  = bs; add->x  = W - 6 - bs;        add->y  = by;
    next->w = next->h = bs; next->x = add->x  - 4 - bs;  next->y = by;
    prev->w = prev->h = bs; prev->x = next->x - 4 - bs;  prev->y = by;
    int tw = flux_fb_text_width("Heute", 2);
    today->w = tw + 22; today->h = 30;
    today->x = prev->x - 8 - today->w; today->y = cy - today->h / 2;
}

static int cal_pt_in(int x, int y, cal_rect r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

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
    int grid_h = CAL_GRID_ROWS * CAL_CELL_H;

    /* --- Header: Monat (fett) + Jahr (dezent) links ------------------- */
    int month_y = STATUSBAR_H + (CAL_HEADER_H - 21) / 2;
    int mw = flux_fb_text_width(month_name(month), 3);
    flux_fb_text(fb, 14, month_y, month_name(month), COL_TEXT, 3);
    char yearstr[8];
    snprintf(yearstr, sizeof(yearstr), "%d", year);
    flux_fb_text(fb, 14 + mw + 8, month_y + 7, yearstr, COL_DIM, 2);

    /* --- Header-Knoepfe: Chevrons, "Heute", Plus --------------------- */
    cal_rect rp, rn, rt, ra;
    cal_header_geom(fb, &rp, &rn, &rt, &ra);
    fill_round_rect(fb, rp.x, rp.y, rp.w, rp.h, rp.h / 2, COL_SURFACE2);
    flux_icon_draw(fb, FLUX_ICON_CHEVRON_LEFT,  rp.x + rp.w / 2, rp.y + rp.h / 2, 20, COL_ACCENT);
    fill_round_rect(fb, rn.x, rn.y, rn.w, rn.h, rn.h / 2, COL_SURFACE2);
    flux_icon_draw(fb, FLUX_ICON_CHEVRON_RIGHT, rn.x + rn.w / 2, rn.y + rn.h / 2, 20, COL_ACCENT);
    fill_round_rect(fb, ra.x, ra.y, ra.w, ra.h, ra.h / 2, COL_SURFACE2);
    flux_icon_draw(fb, FLUX_ICON_PLUS,          ra.x + ra.w / 2, ra.y + ra.h / 2, 20, COL_ACCENT);
    fill_round_rect(fb, rt.x, rt.y, rt.w, rt.h, rt.h / 2, COL_SURFACE2);
    int htw = flux_fb_text_width("Heute", 2);
    flux_fb_text(fb, rt.x + (rt.w - htw) / 2, rt.y + (rt.h - 14) / 2, "Heute", COL_ACCENT, 2);

    /* --- Wochenend-Spalten dezent absetzen ---------------------------- */
    flux_fb_fill_rect(fb, 5 * cw, grid_top, fb->width - 5 * cw, grid_h, COL_ROW_ALT);

    /* --- Wochentag-Kopfzeile ------------------------------------------ */
    static const char *dow_labels[] = {"Mo","Di","Mi","Do","Fr","Sa","So"};
    for (int i = 0; i < 7; i++) {
        int bx = i * cw;
        int lw = flux_fb_text_width(dow_labels[i], 2);
        uint32_t col = (i >= 5) ? 0xEE8855 : COL_TEXT_MUTED; /* Sa/So in orange */
        flux_fb_text(fb, bx + (cw - lw) / 2,
                     STATUSBAR_H + CAL_HEADER_H + (CAL_DAYROW_H - 14) / 2,
                     dow_labels[i], col, 2);
    }

    /* --- Datumsraster ------------------------------------------------- */
    int first_dow = first_weekday(year, month);
    int dim = days_in_month(year, month);

    /* Dezente Wochen-Trennlinien */
    for (int r = 1; r < CAL_GRID_ROWS; r++)
        flux_fb_hline(fb, 0, grid_top + r * CAL_CELL_H, fb->width, COL_SURFACE2);

    for (int day = 1; day <= dim; day++) {
        int cell_idx = first_dow + day - 1; /* 0-based cell in grid */
        int row = cell_idx / 7;
        int col_idx = cell_idx % 7;
        int cx2 = col_idx * cw;
        int cy = grid_top + row * CAL_CELL_H;
        int is_today = (today_day > 0 && day == today_day);
        int is_sel   = (day == selected_day);
        int is_weekend = (col_idx >= 5);

        /* Markierung des ausgewaehlten Tages: gefuellte Zelle */
        if (is_sel)
            fill_round_rect(fb, cx2 + 3, cy + 3, cw - 6, CAL_CELL_H - 6, 8, COL_SURFACE3);

        /* Tageszahl -- "Heute" als gefuellter Akzent-Kreis */
        char daystr[12]; snprintf(daystr, sizeof(daystr), "%d", day);
        int dw = flux_fb_text_width(daystr, 2);
        int num_cx = cx2 + cw / 2;
        int num_cy = cy + 15;
        if (is_today) {
            fill_circle(fb, num_cx, num_cy, 12, COL_ACCENT);
            flux_fb_text(fb, num_cx - dw / 2, num_cy - 7, daystr, COL_BG, 2);
        } else {
            uint32_t tcol = is_sel ? COL_TEXT :
                            is_weekend ? COL_TEXT_MUTED : COL_TEXT;
            flux_fb_text(fb, num_cx - dw / 2, num_cy - 7, daystr, tcol, 2);
        }

        /* Termin-Marker: bis zu zwei farbige Pillen + Ueberlauf-Zaehler */
        int ecount = 0;
        const char *etitles[2]; uint32_t ecolors[2];
        for (int e = 0; e < n_events; e++) {
            int ed; char ehh[6]; const char *et;
            if (!cal_parse_event(event_strs[e], &ed, ehh, &et)) continue;
            if (ed != day) continue;
            if (ecount < 2) {
                etitles[ecount] = (et && et[0]) ? et : "Termin";
                ecolors[ecount] = cal_event_color(et && et[0] ? et : event_strs[e]);
            }
            ecount++;
        }
        int pill_x = cx2 + 4, pill_w = cw - 8, pill_h = 11;
        int pill_y = cy + 30;
        int shown = ecount < 2 ? ecount : 2;
        for (int k = 0; k < shown; k++) {
            fill_round_rect(fb, pill_x, pill_y, pill_w, pill_h, 3, ecolors[k]);
            char fit[24];
            cal_fit_text(fit, sizeof(fit), etitles[k], pill_w - 6, 1);
            flux_fb_text(fb, pill_x + 3, pill_y + 2, fit, COL_TEXT, 1);
            pill_y += pill_h + 2;
        }
        if (ecount > 2) {
            char more[16]; snprintf(more, sizeof(more), "+%d", ecount - 2);
            flux_fb_text(fb, pill_x + 2, pill_y + 1, more, COL_TEXT_MUTED, 1);
        }
    }

    /* --- Tagesdetail unter dem Raster: Termine als Karten ------------- */
    static const char *wday_long[] = {
        "Montag","Dienstag","Mittwoch","Donnerstag","Freitag","Samstag","Sonntag"
    };
    int ev_y = grid_top + grid_h + 12;
    int ev_bottom = fb->height - LIST_BACK_H - 8;

    if (selected_day > 0) {
        int wd = (first_dow + selected_day - 1) % 7;
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "%s, %d. %s",
                 wday_long[wd], selected_day, month_name(month));
        flux_fb_text(fb, 16, ev_y, hdr, COL_TEXT, 2);
        flux_fb_fill_gradient_h(fb, 16, ev_y + 20, 80, 2, COL_ACCENT, COL_ACCENT2);
        ev_y += 32;

        int any = 0;
        for (int e = 0; e < n_events && ev_y + 44 <= ev_bottom; e++) {
            int ed; char ehh[6]; const char *et;
            if (!cal_parse_event(event_strs[e], &ed, ehh, &et)) continue;
            if (ed != selected_day) continue;
            any = 1;
            uint32_t col = cal_event_color(et && et[0] ? et : event_strs[e]);
            int card_x = 12, card_w = fb->width - 24, card_h = 40;
            fill_round_rect(fb, card_x, ev_y, card_w, card_h, 10, COL_SURFACE2);
            flux_fb_fill_rect(fb, card_x, ev_y + 6, 4, card_h - 12, col);
            int ty = ev_y + (card_h - 14) / 2;
            if (ehh[0]) flux_fb_text(fb, card_x + 16, ty, ehh, COL_TEXT, 2);
            int title_x = card_x + (ehh[0] ? 84 : 16);
            char fit[64];
            cal_fit_text(fit, sizeof(fit), (et && et[0]) ? et : "Termin",
                         card_x + card_w - title_x - 10, 2);
            flux_fb_text(fb, title_x, ty, fit, COL_TEXT_MUTED, 2);
            ev_y += card_h + 8;
        }
        if (!any)
            flux_fb_text(fb, 16, ev_y + 2, "Keine Termine an diesem Tag.", COL_DIM, 2);
    } else {
        flux_fb_text(fb, 16, ev_y, "Tippe auf einen Tag fuer Details.", COL_DIM, 2);
    }

    draw_back_bar(fb, "Zurück");
    flux_fb_present(fb);
}

int flux_ui_calendar_hit(const flux_fb_t *fb, int x, int y,
                          int *day, int *prev_month, int *next_month,
                          int *today_btn, int *add_btn) {
    *day = 0; *prev_month = 0; *next_month = 0;
    *today_btn = 0; *add_btn = 0;

    if (y >= fb->height - LIST_BACK_H) return 0; /* back bar handled by caller */

    /* Kopfzeilen-Knoepfe (gleiche Geometrie wie beim Zeichnen) */
    cal_rect rp, rn, rt, ra;
    cal_header_geom(fb, &rp, &rn, &rt, &ra);
    if (cal_pt_in(x, y, rp)) { *prev_month = 1; return 1; }
    if (cal_pt_in(x, y, rn)) { *next_month = 1; return 1; }
    if (cal_pt_in(x, y, rt)) { *today_btn  = 1; return 1; }
    if (cal_pt_in(x, y, ra)) { *add_btn    = 1; return 1; }

    /* Tageszellen */
    if (y < CAL_GRID_TOP(fb)) return 0;
    int row = (y - CAL_GRID_TOP(fb)) / CAL_CELL_H;
    if (row >= CAL_GRID_ROWS) return 0; /* Detailbereich unter dem Raster */
    int col_idx = x / CAL_CELL_W(fb);
    if (col_idx > 6) col_idx = 6;
    *day = row * 7 + col_idx; /* Roh-Zellenindex -- Aufrufer rechnet via first_weekday um */
    return 1;
}

/* ---- Kontakte -------------------------------------------------------- */

void flux_ui_draw_contacts(flux_fb_t *fb, const char **names,
                            const char **details, int n, int selected_idx) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    flux_fb_text(fb, 16, STATUSBAR_H + 10, "Kontakte", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, 16, STATUSBAR_H + 40, 96, 2, COL_ACCENT, COL_ACCENT2);

    if (n == 0) {
        int ey = STATUSBAR_H + TITLE_AREA_H + 20;
        fill_round_rect(fb, 12, ey, fb->width - 24, 72, 10, COL_SURFACE);
        flux_fb_text(fb, 24, ey + 10, "Noch keine Kontakte gespeichert.", COL_TEXT_MUTED, 2);
        flux_fb_text(fb, 24, ey + 34, "Sage der KI: \"Speichere Max, +49 151...\"", COL_DIM, 2);
    } else {
        list_row_geom_t rows[LIST_MAX_ROWS];
        int rn = build_list_rows(fb, n, rows);
        for (int i = 0; i < rn; i++) {
            int sel = (i == selected_idx);
            fill_round_rect(fb, rows[i].x + 6, rows[i].y + 2,
                            rows[i].w - 12, rows[i].h - 4, 10, sel ? COL_SURFACE3 : COL_SURFACE);
            if (sel)
                flux_fb_fill_rect(fb, rows[i].x + 6, rows[i].y + 2,
                                  3, rows[i].h - 4, COL_ACCENT);

            /* Avatar circle with initial */
            int avx = rows[i].x + 26, avy = rows[i].y + rows[i].h / 2;
            fill_circle(fb, avx, avy, 14, COL_SURFACE3);
            /* Border ring in accent */
            draw_ring(fb, avx, avy, 14, 2, COL_ACCENT);
            /* Initial letter */
            char init[2] = { names[i][0], '\0' };
            flux_fb_text(fb, avx - 4, avy - 8, init, COL_TEXT, 2);

            flux_fb_text(fb, rows[i].x + 48, rows[i].y + 10, names[i], COL_TEXT, 2);
            if (details && details[i])
                flux_fb_text(fb, rows[i].x + 48, rows[i].y + rows[i].h - 22,
                             details[i], COL_DIM, 2);
        }
    }

    draw_back_bar(fb, "Zurück");
    flux_fb_present(fb);
}

int flux_ui_notify_hit(const flux_fb_t *fb, int x, int y) {
    (void)fb; (void)x; (void)y;
    return 1; /* beliebiger Tap schliesst den Overlay */
}

/* ---- Fehler-Toast (Slide-up von unten) ------------------------------- */

#define ERR_TOAST_H      88
#define ERR_TOAST_MARGIN 14

int flux_ui_error_toast_height(const flux_fb_t *fb) {
    (void)fb;
    return ERR_TOAST_H + ERR_TOAST_MARGIN;
}

void flux_ui_draw_error_toast(flux_fb_t *fb, const char *msg, int top_y) {
    int x = ERR_TOAST_MARGIN;
    int w = fb->width - 2 * ERR_TOAST_MARGIN;
    int h = ERR_TOAST_H;

    /* Karte mit dunkelrotem Verlauf + roter Akzentkante links. */
    flux_fb_fill_gradient_v_rounded(fb, x, top_y, w, h, 14, 0x3A1418, 0x2A1015);
    flux_fb_fill_rect(fb, x, top_y + 12, 4, h - 24, 0xF87171);

    /* Warn-Symbol: roter Kreis mit Ausrufezeichen. */
    int icx = x + 30, icy = top_y + h / 2;
    flux_fb_fill_circle(fb, icx, icy, 13, 0xF87171);
    flux_fb_text(fb, icx - 2, icy - 7, "!", 0x2A1015, 2);

    flux_fb_text(fb, x + 54, top_y + 14, "Es gab einen Fehler", 0xFCA5A5, 2);
    draw_wrapped(fb, x + 54, top_y + 38, w - 70,
                 (msg && msg[0]) ? msg : "Unbekannter Fehler", COL_TEXT, 2, 20);
}

/* ---- Fotogalerie ----------------------------------------------------- */

/* ---- Fotogalerie (iOS-"Mediathek"-Stil) ------------------------------ *
 * Drei-Spalten-Raster, randlos (nur 1px Fuge), echte center-gecroppte
 * Thumbnails. Fixer Header oben, fixe schwebende Segment-Leiste unten,
 * dazwischen scrollt das Raster. Draw und Hit-Test teilen sich exakt
 * dieselben Geometrie-Helfer (gal_*), damit Taps nie danebenliegen. */

#define GAL_HEADER_H   72                          /* fixer Header (unter Statusbar) */
#define GAL_GRID_TOP   (STATUSBAR_H + GAL_HEADER_H)
#define GAL_COLS       3
#define GAL_GAP        1                           /* Fuge zwischen Kacheln */
#define GAL_TILE       FLUX_GALLERY_TILE           /* 159px Kantenlaenge */
#define GAL_HDR_LINE_H 22                          /* Hoehe einer Datums-Trennueberschrift */
#define GAL_BOTTOM_H   72                          /* fixer Bereich fuer schwebende Leiste */

/* Such-Modus (Lupe): Suchleiste oben + Tastatur unten, Raster filtert live. */
static int  s_gal_search = 0;
static char s_gal_query[64] = "";
void flux_ui_gallery_set_search(int active, const char *query) {
    s_gal_search = active ? 1 : 0;
    snprintf(s_gal_query, sizeof(s_gal_query), "%s", query ? query : "");
}

/* Such-Leiste im Header-Band: Zurueck-Pfeil | Suchfeld | KI-Knopf. */
static void gal_search_geom(const flux_fb_t *fb, cal_rect *back,
                            cal_rect *field, cal_rect *ki) {
    int h = 40, by = STATUSBAR_H + (GAL_HEADER_H - h) / 2;
    back->x = 8; back->y = by; back->w = 40; back->h = h;
    int kiw = flux_fb_text_width("KI", 2) + 30;
    ki->w = kiw; ki->h = h; ki->x = fb->width - 10 - kiw; ki->y = by;
    field->x = back->x + back->w + 6; field->y = by;
    field->w = ki->x - 8 - field->x; field->h = h;
}

/* Sichtbare Rasterhoehe (zwischen Header und schwebender Leiste bzw. der
 * Tastatur im Such-Modus). */
static int gal_grid_h(const flux_fb_t *fb) {
    int bottom = s_gal_search ? (fb->height - flux_ui_kbd_top(fb)) : GAL_BOTTOM_H;
    return fb->height - GAL_GRID_TOP - bottom;
}

/* Zerlegt ein Datum "TT.MM.JJJJ" in y/m. Gibt 1 bei Erfolg. */
static int gal_parse_date(const char *d, int *y, int *m) {
    if (!d || strlen(d) < 10) return 0;
    if (d[2] != '.' || d[5] != '.') return 0;
    *m = (d[3]-'0')*10 + (d[4]-'0');
    *y = (d[6]-'0')*1000 + (d[7]-'0')*100 + (d[8]-'0')*10 + (d[9]-'0');
    if (*m < 1 || *m > 12) return 0;
    return 1;
}

/* Ueberschrift fuer Foto i unter dem aktuellen Filter, oder NULL wenn
 * keine neue Gruppe beginnt. Schreibt nach buf. filter: FLUX_GAL_*. */
static const char *gal_group_label(const char **dates, int i, int filter,
                                   char *buf, size_t cap) {
    if (filter == FLUX_GAL_ALLE) return NULL;        /* keine Gruppen */
    int y, m;
    if (!dates || !gal_parse_date(dates[i], &y, &m)) {
        /* Foto ohne erkennbares Datum: eigene "Ohne Datum"-Gruppe nur
         * dann als Ueberschrift, wenn der Vorgaenger ein Datum hatte. */
        int py, pm;
        if (i == 0) { snprintf(buf, cap, "Ohne Datum"); return buf; }
        if (dates && gal_parse_date(dates[i-1], &py, &pm)) {
            snprintf(buf, cap, "Ohne Datum"); return buf;
        }
        return NULL;
    }
    int py = 0, pm = 0;
    int prev_ok = (i > 0 && dates && gal_parse_date(dates[i-1], &py, &pm));
    if (filter == FLUX_GAL_JAHRE) {
        if (!prev_ok || py != y) { snprintf(buf, cap, "%d", y); return buf; }
    } else { /* MONATE */
        if (!prev_ok || py != y || pm != m) {
            snprintf(buf, cap, "%s %d", month_name(m), y); return buf;
        }
    }
    return NULL;
}

/* Geometrie eines Items (Foto oder Ueberschrift) im UNGESCROLLTEN Raster:
 * liefert die absolute Y-Position (relativ zum Rasteranfang) und ob es
 * sich um eine Kachel handelt. Wir laufen alle Items einmal durch, weil
 * Ueberschriften eine variable Hoehe einschieben. */
typedef struct {
    int kind;   /* 0 = Kachel, 1 = Ueberschrift */
    int idx;    /* bei Kachel: Foto-Index; bei Ueberschrift: -1 */
    int x, y;   /* Position relativ zum Rasteranfang (y vor Scroll) */
    int w, h;
    char label[40];
} gal_item_t;

/* Baut die komplette Item-Liste (Ueberschriften + Kacheln) im Flow-Layout.
 * Gibt die Anzahl der Items und die Gesamthoehe (*total_h) zurueck.
 * out muss Platz fuer (n + n + 1) Items bieten. */
static int gal_layout(const flux_fb_t *fb, const char **dates, int n,
                      int filter, gal_item_t *out, int *total_h) {
    int tile = GAL_TILE;
    int gap = GAL_GAP;
    /* horizontale Zentrierung des Rasters (3*tile + 2*gap) */
    int grid_w = GAL_COLS * tile + (GAL_COLS - 1) * gap;
    int x0 = (fb->width - grid_w) / 2;
    if (x0 < 0) x0 = 0;

    int cnt = 0;
    int y = 0;
    int col = 0;
    char buf[40];
    for (int i = 0; i < n; i++) {
        const char *lbl = gal_group_label(dates, i, filter, buf, sizeof(buf));
        if (lbl) {
            /* offene Zeile abschliessen */
            if (col != 0) { y += tile + gap; col = 0; }
            if (cnt > 0) y += 6;                  /* etwas Luft vor Ueberschrift */
            gal_item_t *it = &out[cnt++];
            it->kind = 1; it->idx = -1;
            it->x = x0; it->y = y; it->w = grid_w; it->h = GAL_HDR_LINE_H;
            snprintf(it->label, sizeof(it->label), "%s", lbl);
            y += GAL_HDR_LINE_H + 4;
        }
        gal_item_t *it = &out[cnt++];
        it->kind = 0; it->idx = i;
        it->x = x0 + col * (tile + gap); it->y = y;
        it->w = tile; it->h = tile;
        it->label[0] = '\0';
        col++;
        if (col >= GAL_COLS) { col = 0; y += tile + gap; }
    }
    if (col != 0) y += tile + gap;
    if (total_h) *total_h = y;
    return cnt;
}

int flux_ui_gallery_max_scroll(const flux_fb_t *fb, const char **dates,
                               int n, int filter) {
    if (n <= 0) return 0;
    gal_item_t *items = malloc(sizeof(gal_item_t) * (size_t)(2 * n + 1));
    if (!items) return 0;
    int total_h = 0;
    gal_layout(fb, dates, n, filter, items, &total_h);
    free(items);
    int vis = gal_grid_h(fb);
    int extra = total_h - vis;
    if (extra <= 0) return 0;
    int step = GAL_TILE + GAL_GAP;
    return (extra + step - 1) / step;            /* in Rasterzeilen */
}

/* --- Header-Geometrie: Kamera (links), Filter + Auswaehlen (rechts) --- */
static void gal_header_geom(const flux_fb_t *fb, cal_rect *cam,
                            cal_rect *filter, cal_rect *select) {
    int W = fb->width;
    int bs = 38;                                  /* runde Knopfgroesse */
    int by = STATUSBAR_H + (GAL_HEADER_H - bs) / 2;
    /* "Auswaehlen"-Pille ganz rechts */
    int pw = flux_fb_text_width("Auswaehlen", 2) + 22;
    select->w = pw; select->h = bs; select->x = W - 12 - pw; select->y = by;
    /* Filter-Knopf links daneben */
    filter->w = filter->h = bs; filter->x = select->x - 8 - bs; filter->y = by;
    /* Kamera-Knopf am rechten Rand der Filter -- nein: links unten im Titel.
     * Kamera klein rechts neben dem Titel links. */
    cam->w = cam->h = bs;
    cam->x = 12; cam->y = by;                     /* (nicht genutzt fuer Titel) */
}

/* --- Schwebende Segment-Leiste + Such-Knopf unten -------------------- */
static void gal_bottom_geom(const flux_fb_t *fb, cal_rect *pill,
                            cal_rect seg[3], cal_rect *search) {
    int W = fb->width;
    int ph = 40;
    int py = fb->height - GAL_BOTTOM_H + (GAL_BOTTOM_H - ph) / 2;
    /* Such-Knopf rechts (rund) */
    search->w = search->h = ph; search->x = W - 12 - ph; search->y = py;
    /* Pille mittig, links vom Such-Knopf */
    int pill_w = 222;
    int pill_x = (W - pill_w) / 2 - 18;
    if (pill_x + pill_w > search->x - 8) pill_x = search->x - 8 - pill_w;
    if (pill_x < 12) pill_x = 12;
    pill->w = pill_w; pill->h = ph; pill->x = pill_x; pill->y = py;
    int seg_w = pill_w / 3;
    for (int i = 0; i < 3; i++) {
        seg[i].x = pill_x + i * seg_w; seg[i].y = py;
        seg[i].w = (i == 2) ? (pill_w - 2 * seg_w) : seg_w; seg[i].h = ph;
    }
}

/* Zeichnet eine echte Thumbnail-Kachel: center-gecroppter Puffer (vom
 * Aufrufer geliefert, genau TILE*TILE) oder ehrliche Platzhalter-Kachel. */
/* Erkennt Video-Dateien an der Endung (Foto-Galerie zeigt beides). */
static int gal_name_is_video(const char *name) {
    if (!name) return 0;
    size_t L = strlen(name);
    const char *exts[] = { ".mov", ".mp4", ".m4v", ".avi", ".mkv", ".webm" };
    for (size_t e = 0; e < sizeof(exts)/sizeof(exts[0]); e++) {
        size_t el = strlen(exts[e]);
        if (L > el) {
            int eq = 1;
            for (size_t k = 0; k < el; k++) {
                char a = name[L-el+k], b = exts[e][k];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (a != b) { eq = 0; break; }
            }
            if (eq) return 1;
        }
    }
    return 0;
}

/* Alpha-Blending eines einzelnen Pixels (Deckung a in 0..255). */
static void blend_px(flux_fb_t *fb, int x, int y, uint32_t col, int a) {
    if (a <= 0 || x < 0 || y < 0 || x >= fb->width || y >= fb->height) return;
    if (a >= 255) { flux_fb_set_px(fb, x, y, col); return; }
    uint32_t bg = fb->back[y * fb->stride_px + x];
    int br = (bg >> 16) & 0xff, bgc = (bg >> 8) & 0xff, bb = bg & 0xff;
    int cr = (col >> 16) & 0xff, cg = (col >> 8) & 0xff, cb = col & 0xff;
    int rr = (cr * a + br  * (255 - a)) / 255;
    int rg = (cg * a + bgc * (255 - a)) / 255;
    int rb = (cb * a + bb  * (255 - a)) / 255;
    fb->back[y * fb->stride_px + x] = ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
}

/* Play-Badge fuer Video-Kacheln: dunkler Kreis + weisses Play-Dreieck.
 * Anti-aliased per 4x4-Supersampling -- so glatt wie der TrueType-Text,
 * nicht mehr die harten Pixelkanten der einfachen Primitive. */
static void gal_draw_play_badge(flux_fb_t *fb, int cx, int cy) {
    const float R = 16.5f, RW = 2.0f;        /* Kreisradius + Ringbreite */
    const float bx = cx - 4, ax = cx + 8, h = 9; /* Dreieck: Basis links, Spitze rechts */
    const float v0x = bx, v0y = cy - h, v1x = ax, v1y = cy, v2x = bx, v2y = cy + h;
    const int SS = 4; const float inv = 1.0f / SS, tot = SS * SS;
    int r = (int)R + 2;
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            int disc = 0, ring = 0, tri = 0;
            for (int sj = 0; sj < SS; sj++) for (int si = 0; si < SS; si++) {
                float px = x + (si + 0.5f) * inv, py = y + (sj + 0.5f) * inv;
                float dx = px - cx, dy = py - cy, d = dx*dx + dy*dy;
                if (d <= R*R) { disc++; if (d >= (R-RW)*(R-RW)) ring++; }
                float e0 = (v1x-v0x)*(py-v0y) - (v1y-v0y)*(px-v0x);
                float e1 = (v2x-v1x)*(py-v1y) - (v2y-v1y)*(px-v1x);
                float e2 = (v0x-v2x)*(py-v2y) - (v0y-v2y)*(px-v2x);
                if ((e0>=0&&e1>=0&&e2>=0) || (e0<=0&&e1<=0&&e2<=0)) tri++;
            }
            if (disc) blend_px(fb, x, y, 0x0B0D14, (int)(205 * disc / tot));
            if (ring) blend_px(fb, x, y, 0xFFFFFF, (int)(255 * ring / tot));
            if (tri)  blend_px(fb, x, y, 0xFFFFFF, (int)(255 * tri  / tot));
        }
    }
}

static void gal_draw_tile(flux_fb_t *fb, const cal_rect *r,
                          const uint32_t *thumb, int sel, int select_mode,
                          int is_video) {
    if (thumb) {
        for (int j = 0; j < r->h; j++)
            for (int i = 0; i < r->w; i++)
                flux_fb_set_px(fb, r->x + i, r->y + j, thumb[j * r->w + i]);
    } else {
        /* Ehrliche Platzhalter-Kachel: dezente Oberflaeche + Bild-/Video-Icon. */
        flux_fb_fill_rect(fb, r->x, r->y, r->w, r->h, COL_SURFACE2);
        flux_icon_draw(fb, is_video ? FLUX_ICON_FILE : FLUX_ICON_IMAGE,
                       r->x + r->w / 2, r->y + r->h / 2, 40, COL_DIM);
    }
    if (is_video)
        gal_draw_play_badge(fb, r->x + r->w / 2, r->y + r->h / 2);
    if (select_mode) {
        /* Auswahl-Stub: Markierungskreis oben rechts */
        int cx = r->x + r->w - 16, cy = r->y + 16;
        draw_ring(fb, cx, cy, 9, 2, sel ? COL_ACCENT : 0xFFFFFF);
        if (sel) fill_circle(fb, cx, cy, 6, COL_ACCENT);
    }
}

void flux_ui_draw_gallery(flux_fb_t *fb, const char **names, const char **dates,
                           int n, int selected_idx, int scroll,
                           int filter, int select_mode,
                           flux_gallery_thumb_fn thumb_fn, void *user) {
    (void)selected_idx;
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    cal_rect cam, fbtn, sbtn;
    gal_header_geom(fb, &cam, &fbtn, &sbtn);

    if (n == 0) {
        int ey = GAL_GRID_TOP + 30;
        fill_round_rect(fb, 16, ey, fb->width - 32, 96, 12, COL_SURFACE);
        flux_icon_draw(fb, FLUX_ICON_IMAGE, fb->width / 2, ey + 34, 36, COL_DIM);
        const char *m1 = "Noch keine Fotos.";
        const char *m2 = "Tippe das Kamera-Symbol unten.";
        flux_fb_text(fb, (fb->width - flux_fb_text_width(m1, 2)) / 2, ey + 56, m1,
                     COL_TEXT_MUTED, 2);
        flux_fb_text(fb, (fb->width - flux_fb_text_width(m2, 2)) / 2, ey + 74, m2,
                     COL_DIM, 2);
    } else {
        /* --- Scrollbares Raster mit Clipping an [GAL_GRID_TOP, grid_bot) -- */
        int grid_top = GAL_GRID_TOP;
        int grid_bot = grid_top + gal_grid_h(fb);
        int scroll_px = scroll * (GAL_TILE + GAL_GAP);

        gal_item_t *items = malloc(sizeof(gal_item_t) * (size_t)(2 * n + 1));
        if (items) {
            int total_h = 0;
            int cnt = gal_layout(fb, dates, n, filter, items, &total_h);
            for (int k = 0; k < cnt; k++) {
                gal_item_t *it = &items[k];
                int sy = grid_top + it->y - scroll_px;   /* Bildschirm-Y */
                if (sy + it->h <= grid_top || sy >= grid_bot) continue; /* unsichtbar */
                if (it->kind == 1) {
                    /* Datums-Trennueberschrift */
                    if (sy >= grid_top && sy + GAL_HDR_LINE_H <= grid_bot)
                        flux_fb_text(fb, it->x + 2, sy + 4, it->label, COL_TEXT, 2);
                } else {
                    /* Kachel -- nur zeichnen wenn vollstaendig im Sichtbereich
                     * vertikal liegt (sonst saubere Kante am Header). */
                    if (sy < grid_top || sy + it->h > grid_bot) {
                        /* teilweise sichtbar: per-Pixel clippen */
                        cal_rect r = { it->x, sy, it->w, it->h };
                        const uint32_t *thumb = thumb_fn ? thumb_fn(names[it->idx], user) : NULL;
                        int is_video = gal_name_is_video(names[it->idx]);
                        for (int j = 0; j < r.h; j++) {
                            int py = r.y + j;
                            if (py < grid_top || py >= grid_bot) continue;
                            for (int i = 0; i < r.w; i++) {
                                uint32_t c = thumb ? thumb[j * r.w + i] : COL_SURFACE2;
                                flux_fb_set_px(fb, r.x + i, py, c);
                            }
                        }
                        if (!thumb && sy + it->h/2 >= grid_top && sy + it->h/2 < grid_bot)
                            flux_icon_draw(fb, is_video ? FLUX_ICON_FILE : FLUX_ICON_IMAGE,
                                           r.x + r.w/2, sy + r.h/2, 40, COL_DIM);
                        if (is_video && sy + it->h/2 >= grid_top && sy + it->h/2 < grid_bot)
                            gal_draw_play_badge(fb, r.x + r.w/2, sy + r.h/2);
                    } else {
                        cal_rect r = { it->x, sy, it->w, it->h };
                        const uint32_t *thumb = thumb_fn ? thumb_fn(names[it->idx], user) : NULL;
                        int sel = (it->idx == selected_idx);
                        gal_draw_tile(fb, &r, thumb, sel, select_mode,
                                      gal_name_is_video(names[it->idx]));
                    }
                }
            }
            free(items);
        }
    }

    /* --- Fixer Header (zuletzt, deckt drunterscrollende Kacheln ab) ---- */
    flux_fb_fill_rect(fb, 0, STATUSBAR_H, fb->width, GAL_HEADER_H, COL_BG);

    if (s_gal_search) {
        /* Such-Modus: Suchleiste oben, Tastatur unten. */
        cal_rect bk, fld, ki;
        gal_search_geom(fb, &bk, &fld, &ki);
        flux_icon_draw(fb, FLUX_ICON_CHEVRON_LEFT, bk.x + bk.w / 2, bk.y + bk.h / 2, 26, COL_ACCENT);
        fill_round_rect(fb, fld.x, fld.y, fld.w, fld.h, fld.h / 2, COL_SURFACE2);
        flux_icon_draw(fb, FLUX_ICON_SEARCH, fld.x + 18, fld.y + fld.h / 2, 16, COL_DIM);
        if (s_gal_query[0])
            flux_fb_text(fb, fld.x + 34, fld.y + (fld.h - 16) / 2, s_gal_query, COL_TEXT, 2);
        else
            flux_fb_text(fb, fld.x + 34, fld.y + (fld.h - 16) / 2, "Fotos durchsuchen", COL_DIM, 2);
        flux_fb_fill_gradient_v_rounded(fb, ki.x, ki.y, ki.w, ki.h, ki.h / 2, COL_ACCENT, COL_ACCENT2);
        int tw = flux_fb_text_width("KI", 2);
        flux_fb_text(fb, ki.x + (ki.w - tw) / 2, ki.y + (ki.h - 16) / 2, "KI", 0xFFFFFF, 2);
        draw_keyboard(fb);
        flux_fb_present(fb);
        return;
    }

    flux_fb_text(fb, 16, STATUSBAR_H + 14, "Fotos", COL_TEXT, 4);
    fill_round_rect(fb, sbtn.x, sbtn.y, sbtn.w, sbtn.h, sbtn.h / 2,
                    select_mode ? COL_ACCENT : COL_SURFACE2);
    {
        int tw = flux_fb_text_width("Auswaehlen", 2);
        flux_fb_text(fb, sbtn.x + (sbtn.w - tw) / 2, sbtn.y + (sbtn.h - 14) / 2,
                     "Auswaehlen", select_mode ? 0xFFFFFF : COL_TEXT, 2);
    }
    fill_round_rect(fb, fbtn.x, fbtn.y, fbtn.w, fbtn.h, fbtn.h / 2, COL_SURFACE2);
    flux_icon_draw(fb, FLUX_ICON_SLIDERS, fbtn.x + fbtn.w / 2,
                   fbtn.y + fbtn.h / 2, 20, COL_TEXT);

    /* --- Schwebende Segment-Leiste + Such-/Kamera-Knopf --------------- */
    flux_fb_fill_rect(fb, 0, fb->height - GAL_BOTTOM_H, fb->width, GAL_BOTTOM_H, COL_BG);
    cal_rect pill, seg[3], search;
    gal_bottom_geom(fb, &pill, seg, &search);
    fill_round_rect(fb, pill.x, pill.y, pill.w, pill.h, pill.h / 2, COL_SURFACE2);
    static const char *seg_labels[3] = { "Jahre", "Monate", "Alle" };
    for (int i = 0; i < 3; i++) {
        int active = (i == filter);
        if (active)
            flux_fb_fill_gradient_v_rounded(fb, seg[i].x + 3, seg[i].y + 4,
                                            seg[i].w - 6, seg[i].h - 8,
                                            (seg[i].h - 8) / 2, COL_ACCENT, COL_ACCENT2);
        int tw = flux_fb_text_width(seg_labels[i], 2);
        flux_fb_text(fb, seg[i].x + (seg[i].w - tw) / 2,
                     seg[i].y + (seg[i].h - 14) / 2, seg_labels[i],
                     active ? 0xFFFFFF : COL_TEXT_MUTED, 2);
    }
    /* Such-Knopf rechts (rund) */
    fill_round_rect(fb, search.x, search.y, search.w, search.h, search.h / 2, COL_SURFACE2);
    flux_icon_draw(fb, FLUX_ICON_SEARCH, search.x + search.w / 2,
                   search.y + search.h / 2, 20, COL_TEXT);
    /* Kamera-Knopf links (rund, Akzent-Gradient) */
    {
        int ch = pill.h;
        int cx = 12, cy = pill.y;
        flux_fb_fill_gradient_v_rounded(fb, cx, cy, ch, ch, ch / 2, COL_ACCENT, COL_ACCENT2);
        flux_icon_draw(fb, FLUX_ICON_CAMERA, cx + ch / 2, cy + ch / 2, 20, 0xFFFFFF);
    }

    flux_fb_present(fb);
}

/* Kamera-Knopf-Geometrie (unten links) -- gemeinsam fuer Draw & Hit. */
static void gal_camera_geom(const flux_fb_t *fb, cal_rect *cam) {
    cal_rect pill, seg[3], search;
    gal_bottom_geom(fb, &pill, seg, &search);
    cam->w = cam->h = pill.h; cam->x = 12; cam->y = pill.y;
}

int flux_ui_gallery_hit(const flux_fb_t *fb, int x, int y, const char **dates,
                        int n, int scroll, int filter, flux_gallery_hit_t *out) {
    out->tile = -1; out->filter = 0; out->select = 0;
    out->segment = -1; out->search = 0; out->camera = 0;
    out->back = 0; out->ki = 0; out->ch = 0; out->backspace = 0; out->enter = 0;

    if (s_gal_search) {
        /* Such-Modus: Zurueck | KI | Tastatur | Kacheln (Raster unten). */
        cal_rect bk, fld, ki;
        gal_search_geom(fb, &bk, &fld, &ki);
        if (cal_pt_in(x, y, bk)) { out->back = 1; return 1; }
        if (cal_pt_in(x, y, ki)) { out->ki = 1; return 1; }
        char c; int bs, en;
        if (flux_ui_kbd_hit(fb, x, y, &c, &bs, &en)) {
            if (bs) out->backspace = 1; else if (en) out->enter = 1; else out->ch = c;
            return 1;
        }
        /* sonst faellt es unten zur Raster-Pruefung durch */
    } else {
        /* Header-Knoepfe */
        cal_rect cam, fbtn, sbtn;
        gal_header_geom(fb, &cam, &fbtn, &sbtn);
        if (cal_pt_in(x, y, sbtn)) { out->select = 1; return 1; }
        if (cal_pt_in(x, y, fbtn)) { out->filter = 1; return 1; }

        /* Untere Leiste: Kamera, Segmente, Suche */
        cal_rect kam; gal_camera_geom(fb, &kam);
        if (cal_pt_in(x, y, kam)) { out->camera = 1; return 1; }
        cal_rect pill, seg[3], search;
        gal_bottom_geom(fb, &pill, seg, &search);
        if (cal_pt_in(x, y, search)) { out->search = 1; return 1; }
        for (int i = 0; i < 3; i++)
            if (cal_pt_in(x, y, seg[i])) { out->segment = i; return 1; }
    }

    /* Raster -- nur im sichtbaren Bereich, mit gleicher Layout-Berechnung */
    int grid_top = GAL_GRID_TOP;
    int grid_bot = grid_top + gal_grid_h(fb);
    if (y < grid_top || y >= grid_bot) return 0;
    if (n <= 0) return 0;
    int scroll_px = scroll * (GAL_TILE + GAL_GAP);
    gal_item_t *items = malloc(sizeof(gal_item_t) * (size_t)(2 * n + 1));
    if (!items) return 0;
    int total_h = 0;
    int cnt = gal_layout(fb, dates, n, filter, items, &total_h);
    int hit = 0;
    for (int k = 0; k < cnt; k++) {
        gal_item_t *it = &items[k];
        if (it->kind != 0) continue;
        int sy = grid_top + it->y - scroll_px;
        cal_rect r = { it->x, sy, it->w, it->h };
        /* nur antippbar, wenn der Treffer im Sichtfenster liegt */
        if (y < grid_top || y >= grid_bot) continue;
        if (cal_pt_in(x, y, r)) { out->tile = it->idx; hit = 1; break; }
    }
    free(items);
    return hit;
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
    flux_fb_fill_rect(fb, 0, btn_area_y, fb->width, IV_BTN_H, COL_BG);
    flux_fb_hline(fb, 0, btn_area_y, fb->width, COL_DIVIDER);
    int bw = fb->width / IV_BTN_COUNT;
    const char *btn_labels[] = { "< Zurück", "KI analyse", "Löschen" };
    for (int i = 0; i < IV_BTN_COUNT; i++) {
        int bx = i * bw + 6;
        int byw = btn_area_y + 6;
        int bww = bw - 12;
        int bhh = IV_BTN_H - 12;
        if (i == 1)
            flux_fb_fill_gradient_v_rounded(fb, bx, byw, bww, bhh, 8, COL_ACCENT, COL_ACCENT2);
        else if (i == 2)
            fill_round_rect(fb, bx, byw, bww, bhh, 8, 0x7F1D1D);
        else
            fill_round_rect(fb, bx, byw, bww, bhh, 8, COL_SURFACE2);
        int tw = flux_fb_text_width(btn_labels[i], 2);
        uint32_t tc = (i == 2) ? COL_DANGER : (i == 1) ? 0xFFFFFF : COL_TEXT_MUTED;
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

    /* Glass-card background with rounded corners */
    fill_round_rect(fb, cx, cy, cw, ch, 14, COL_SURFACE2);
    /* Gradient top border */
    flux_fb_fill_gradient_h(fb, cx, cy, cw, 3, COL_ACCENT, COL_ACCENT2);
    flux_fb_hline(fb, cx, cy + ch - 1, cw, COL_DIVIDER);
    flux_fb_vline(fb, cx, cy, ch, COL_DIVIDER);
    flux_fb_vline(fb, cx + cw - 1, cy, ch, COL_DIVIDER);

    /* Kontext-Label */
    if (context_label && context_label[0]) {
        flux_fb_text(fb, cx + 14, cy + 12, context_label, COL_ACCENT, 2);
    }

    /* Eingabefeld -- rounded pill */
    int inf_y = cy + 38;
    int inf_h = 52;
    fill_round_rect(fb, cx + 8, inf_y, cw - 16, inf_h, 8, COL_BG);
    flux_fb_fill_rect(fb, cx + 8, inf_y, 3, inf_h, COL_ACCENT);
    const char *disp = (input_text && input_text[0]) ? input_text : "Frage die KI...";
    uint32_t   tcol  = (input_text && input_text[0]) ? COL_TEXT : COL_DIM;
    flux_fb_text(fb, cx + 18, inf_y + (inf_h - 14) / 2, disp, tcol, 2);

    /* Zwei Buttons: [Abbrechen] [Fragen] */
    int bty  = cy + ch - AI_OVL_BTN_H - 10;
    int btnw = (cw - 28) / 2;
    int tw;

    fill_round_rect(fb, cx + 8, bty, btnw, AI_OVL_BTN_H, 8, COL_SURFACE3);
    flux_fb_hline(fb, cx + 8, bty, btnw, COL_DIVIDER);
    tw = flux_fb_text_width("Abbrechen", 2);
    flux_fb_text(fb, cx + 8 + (btnw - tw) / 2, bty + (AI_OVL_BTN_H - 14) / 2,
                 "Abbrechen", COL_TEXT_MUTED, 2);

    int b2x = cx + 8 + btnw + 12;
    flux_fb_fill_gradient_v_rounded(fb, b2x, bty, btnw, AI_OVL_BTN_H, 8,
                                     COL_ACCENT, COL_ACCENT2);
    tw = flux_fb_text_width("Fragen", 2);
    flux_fb_text(fb, b2x + (btnw - tw) / 2, bty + (AI_OVL_BTN_H - 14) / 2,
                 "Fragen", 0xFFFFFF, 2);

    /* Ergebnis-Panel */
    if (result_text && result_text[0]) {
        int ry  = cy + ch + 10;
        int rh  = fb->height - ry - 14;
        if (rh > 64) {
            fill_round_rect(fb, cx, ry, cw, rh, 10, COL_SURFACE);
            flux_fb_fill_gradient_h(fb, cx, ry, cw, 3, COL_ACCENT, COL_ACCENT2);

            draw_wrapped(fb, cx + 12, ry + 10, cw - 24,
                         result_text, COL_TEXT, 2, 20);

            /* "Speichern"-Button */
            int sy = ry + rh - 46;
            fill_round_rect(fb, cx + 8, sy, cw - 16, 38, 8, 0x064E3B);
            flux_fb_hline(fb, cx + 8, sy, cw - 16, COL_SEND);
            tw = flux_fb_text_width("Als Datei speichern", 2);
            flux_fb_text(fb, cx + 8 + (cw - 16 - tw) / 2, sy + 12,
                         "Als Datei speichern", COL_SEND, 2);
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
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    /* Title with gradient underline */
    int tw = flux_fb_text_width("Meeting-Mitschrift", 3);
    flux_fb_text(fb, (fb->width - tw) / 2, STATUSBAR_H + 10, "Meeting-Mitschrift", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, (fb->width - tw) / 2, STATUSBAR_H + 40,
                             tw, 2, COL_ACCENT, COL_ACCENT2);

    /* Recording button: gradient circle */
    int cx = fb->width / 2;
    int cy = STATUSBAR_H + 52 + MTG_BTN_R;
    uint32_t ring_col = recording ? COL_DANGER : COL_ACCENT;
    uint32_t ring_col2 = recording ? 0xB91C1C : COL_ACCENT2;
    /* Outer glow ring */
    draw_ring(fb, cx, cy, MTG_BTN_R + 4, 4, recording ? 0x7F1D1D : COL_SURFACE3);
    /* Gradient fill */
    for (int dy = -MTG_BTN_R; dy <= MTG_BTN_R; dy++) {
        for (int dx = -MTG_BTN_R; dx <= MTG_BTN_R; dx++) {
            int d2 = dx*dx + dy*dy;
            if (d2 > MTG_BTN_R * MTG_BTN_R) continue;
            /* t: 0 top -> 1 bottom */
            int t = dy + MTG_BTN_R;
            int max = MTG_BTN_R * 2;
            uint8_t r1=(ring_col>>16)&0xFF, g1=(ring_col>>8)&0xFF, b1=ring_col&0xFF;
            uint8_t r2=(ring_col2>>16)&0xFF, g2=(ring_col2>>8)&0xFF, b2=ring_col2&0xFF;
            uint32_t col = (uint32_t)((r1+(int)(r2-r1)*t/max)&0xFF)<<16 |
                           (uint32_t)((g1+(int)(g2-g1)*t/max)&0xFF)<<8  |
                           (uint32_t)((b1+(int)(b2-b1)*t/max)&0xFF);
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < fb->width && py >= 0 && py < fb->height)
                fb->back[py * fb->width + px] = col;
        }
    }
    /* Icon */
    if (recording) {
        /* White stop square */
        flux_fb_fill_rect(fb, cx - 12, cy - 12, 24, 24, 0xFFFFFF);
    } else {
        /* Mic body */
        fill_round_rect(fb, cx - 7, cy - 16, 14, 22, 5, 0xFFFFFF);
        /* Mic stand */
        flux_fb_fill_rect(fb, cx - 1, cy + 6, 2, 10, 0xFFFFFF);
        flux_fb_fill_rect(fb, cx - 8, cy + 16, 16, 2, 0xFFFFFF);
    }

    /* Timer */
    int by = cy + MTG_BTN_R + 14;
    if (recording) {
        char timer[16];
        int m = elapsed_s / 60, s = elapsed_s % 60;
        snprintf(timer, sizeof(timer), "%02d:%02d", m, s);
        tw = flux_fb_text_width(timer, 4);
        flux_fb_text(fb, (fb->width - tw) / 2, by, timer, COL_DANGER, 4);
        by += 44;
    } else {
        const char *hint = "Tippen zum Starten";
        tw = flux_fb_text_width(hint, 2);
        flux_fb_text(fb, (fb->width - tw) / 2, by, hint, COL_DIM, 2);
        by += 30;
    }

    if (status_msg && status_msg[0]) {
        tw = flux_fb_text_width(status_msg, 2);
        flux_fb_text(fb, (fb->width - tw) / 2, by, status_msg, COL_TEXT_MUTED, 2);
        by += 24;
    }

    /* Transcript area */
    int tr_top = by + 6;
    int tr_bot = fb->height - MTG_SAVE_H - MTG_BACK_H - 8;
    if (tr_top < tr_bot) {
        fill_round_rect(fb, 12, tr_top, fb->width - 24, tr_bot - tr_top, 8, COL_SURFACE);
        flux_fb_fill_rect(fb, 12, tr_top, 3, tr_bot - tr_top, COL_ACCENT);
        if (transcript && transcript[0]) {
            draw_wrapped(fb, 22, tr_top + 8,
                         fb->width - 44, transcript, COL_TEXT, 2, tr_bot - tr_top - 16);
        } else {
            const char *ph = recording ? "Transkription läuft ..." : "Kein Transkript";
            tw = flux_fb_text_width(ph, 2);
            flux_fb_text(fb, (fb->width - tw) / 2,
                         tr_top + (tr_bot - tr_top) / 2 - 8, ph, COL_DIM, 2);
        }
    }

    /* Save button: gradient */
    int save_y = fb->height - MTG_SAVE_H - MTG_BACK_H;
    flux_fb_fill_gradient_v_rounded(fb, 16, save_y + 4, fb->width - 32, MTG_SAVE_H - 8,
                                    8, COL_SEND, 0x059669);
    tw = flux_fb_text_width("Speichern & Schließen", 2);
    flux_fb_text(fb, (fb->width - tw) / 2, save_y + (MTG_SAVE_H - 16) / 2,
                 "Speichern & Schließen", 0xFFFFFF, 2);

    draw_back_bar(fb, "Zurück");
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
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    /* Header */
    flux_fb_text(fb, 16, STATUSBAR_H + 10, "KI-Gedächtnis", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, 16, STATUSBAR_H + 40, 148, 2, COL_ACCENT, COL_ACCENT2);

    int list_y = STATUSBAR_H + TITLE_AREA_H;
    int list_h = fb->height - list_y - LIST_BACK_H;

    if (n == 0) {
        int ey = list_y + 20;
        fill_round_rect(fb, 12, ey, fb->width - 24, 72, 10, COL_SURFACE);
        const char *empty = "Noch kein Wissen gespeichert.";
        int tw = flux_fb_text_width(empty, 2);
        flux_fb_text(fb, (fb->width - tw) / 2, ey + 28, empty, COL_DIM, 2);
        draw_back_bar(fb, "Zurück");
        flux_fb_present(fb);
        return;
    }

    int entry_h = 76;
    int y0 = list_y + 4;
    int max_visible = list_h / entry_h;
    if (scroll > n - max_visible) scroll = n - max_visible;
    if (scroll < 0) scroll = 0;

    for (int i = scroll; i < n && y0 + entry_h <= list_y + list_h; i++) {
        int ey = y0;
        fill_round_rect(fb, 8, ey, fb->width - 16, entry_h - 6, 8, COL_SURFACE);
        flux_fb_fill_rect(fb, 8, ey, 3, entry_h - 6, COL_ACCENT);

        const char *entry = entries[i];
        if (entry[0] == '[') {
            const char *end_bracket = strchr(entry, ']');
            if (end_bracket) {
                char ts_buf[32];
                size_t ts_len = (size_t)(end_bracket - entry + 1);
                if (ts_len >= sizeof(ts_buf)) ts_len = sizeof(ts_buf) - 1;
                memcpy(ts_buf, entry, ts_len); ts_buf[ts_len] = '\0';
                /* Timestamp as small pill */
                int tsw = flux_fb_text_width(ts_buf, 1);
                fill_round_rect(fb, 18, ey + 6, tsw + 8, 14, 4, COL_SURFACE3);
                flux_fb_text(fb, 22, ey + 8, ts_buf, COL_DIM, 1);
                const char *text = end_bracket + 1;
                while (*text == ' ') text++;
                draw_wrapped(fb, 18, ey + 26, fb->width - 34,
                             text, COL_TEXT, 2, 22);
            } else {
                flux_fb_text(fb, 18, ey + 28, entry, COL_TEXT, 2);
            }
        } else {
            flux_fb_text(fb, 18, ey + 28, entry, COL_TEXT, 2);
        }
        y0 += entry_h;
    }

    /* Scroll indicator */
    if (n > max_visible) {
        int bar_h = list_h * max_visible / n;
        int bar_y = list_y + list_h * scroll / n;
        fill_round_rect(fb, fb->width - 5, bar_y, 4, bar_h, 2, COL_ACCENT);
    }

    draw_back_bar(fb, "Zurück");
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
    if (strncmp(line, "Gedächtnis:", 12) == 0 || strncmp(line, "Memory:", 7) == 0)
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
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    int iy = STATUSBAR_H + 8;

    /* Suchfeld -- rounded pill */
    fill_round_rect(fb, 8, iy, fb->width - 16, SRCH_INPUT_H, SRCH_INPUT_H / 2, COL_SURFACE2);
    /* Accent bottom border */
    flux_fb_fill_gradient_h(fb, 8, iy + SRCH_INPUT_H - 2, fb->width - 16, 2,
                             COL_ACCENT, COL_ACCENT2);

    /* Lupe-Symbol */
    fill_circle(fb, 30, iy + SRCH_INPUT_H / 2, 9, COL_SURFACE3);
    fill_circle(fb, 30, iy + SRCH_INPUT_H / 2, 6, COL_SURFACE2);
    flux_fb_vline(fb, 37, iy + SRCH_INPUT_H / 2 + 5, 7, COL_TEXT_MUTED);

    if (query && query[0]) {
        char disp[56]; int dlen = 0;
        while (query[dlen] && dlen < 50) dlen++;
        memcpy(disp, query, (size_t)dlen); disp[dlen] = '\0';
        flux_fb_text(fb, 50, iy + (SRCH_INPUT_H - 16) / 2, disp, COL_TEXT, 2);
        int qw = flux_fb_text_width(disp, 2);
        flux_fb_fill_rect(fb, 50 + qw + 2, iy + 14, 2, 24, COL_ACCENT);
    } else {
        flux_fb_text(fb, 50, iy + (SRCH_INPUT_H - 16) / 2,
                     "Alles durchsuchen...", COL_DIM, 2);
    }

    int list_y = iy + SRCH_INPUT_H + 10;
    int kbd_top = flux_ui_kbd_top(fb);

    if (searching) {
        fill_round_rect(fb, 12, list_y, fb->width - 24, 80, 10, COL_SURFACE);
        flux_fb_fill_gradient_h(fb, 12, list_y, fb->width - 24, 2, COL_ACCENT, COL_ACCENT2);
        int sw = flux_fb_text_width("KI sucht...", 2);
        flux_fb_text(fb, (fb->width - sw) / 2, list_y + 16, "KI sucht...", COL_ACCENT, 2);
        flux_fb_text(fb, 20, list_y + 42,
                     "Gedächtnis, Kalender, Kontakte, Dateien", COL_DIM, 2);
        draw_keyboard(fb);
        flux_fb_present(fb);
        return;
    }

    if (n == 0 && query && query[0]) {
        fill_round_rect(fb, 12, list_y, fb->width - 24, 56, 10, COL_SURFACE);
        int nw = flux_fb_text_width("Keine Treffer.", 2);
        flux_fb_text(fb, (fb->width - nw) / 2, list_y + 20, "Keine Treffer.", COL_DIM, 2);
    } else if (n == 0) {
        /* Intro cards */
        const char *sources[] = {
            "Gedächtnis", "Kalender", "Kontakte", "Dateien", "Fotos"
        };
        static const uint32_t src_cols[] = {
            0x4FD1C5, 0x3B82F6, 0xA855F7, 0x6B7280, 0xF97316
        };
        flux_fb_text(fb, 16, list_y + 2, "Tippe und drücke Enter", COL_TEXT_MUTED, 2);
        int ey = list_y + 30;
        for (int i = 0; i < 5 && ey < kbd_top - 28; i++) {
            fill_round_rect(fb, 10, ey, fb->width - 20, 28, 6, COL_SURFACE);
            fill_circle(fb, 26, ey + 14, 5, src_cols[i]);
            flux_fb_text(fb, 40, ey + 7, sources[i], COL_TEXT, 2);
            ey += 34;
        }
    }

    /* Ergebnisliste als Karten */
    int max_show = (kbd_top - list_y) / SRCH_ROW_H;
    if (max_show > SRCH_MAX_ROWS) max_show = SRCH_MAX_ROWS;
    if (n > max_show) n = max_show;

    for (int i = 0; i < n; i++) {
        int ry = list_y + i * SRCH_ROW_H;
        uint32_t sc = srch_source_color(results[i]);

        fill_round_rect(fb, 8, ry, fb->width - 16, SRCH_ROW_H - 4, 8, COL_SURFACE);
        flux_fb_fill_rect(fb, 8, ry, 3, SRCH_ROW_H - 4, sc);

        const char *colon = strchr(results[i], ':');
        if (colon) {
            char src[32]; size_t sl = (size_t)(colon - results[i]);
            if (sl >= sizeof(src)) sl = sizeof(src)-1;
            memcpy(src, results[i], sl); src[sl] = '\0';
            /* Source badge */
            int sw = flux_fb_text_width(src, 1);
            fill_round_rect(fb, 18, ry + 6, sw + 8, 14, 4, COL_SURFACE3);
            flux_fb_text(fb, 22, ry + 8, src, sc, 1);
            const char *content = colon + 1;
            while (*content == ' ') content++;
            char line1[52], line2[52]; int l1=0, l2=0;
            while (content[l1] && content[l1]!='\n' && l1<46) l1++;
            memcpy(line1, content, (size_t)l1); line1[l1]='\0';
            flux_fb_text(fb, 18, ry + 26, line1, COL_TEXT, 2);
            content += l1; if (*content=='\n') content++;
            if (*content) {
                while (content[l2] && content[l2]!='\n' && l2<46) l2++;
                memcpy(line2, content, (size_t)l2); line2[l2]='\0';
                flux_fb_text(fb, 18, ry + 48, line2, COL_TEXT_MUTED, 2);
            }
        } else {
            flux_fb_text(fb, 18, ry + 24, results[i], COL_TEXT, 2);
        }
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

/* Dreieckswelle 0..amp (fuer weiches Pulsieren ohne libm). */
static int tri_wave(int x, int period, int amp) {
    if (period < 2) period = 2;
    int m = x % period; if (m < 0) m += period;
    int half = period / 2;
    int v = (m < half) ? m : (period - m);
    return v * amp / half;
}

/* ---- Journal-Screen ------------------------------------------------ */

#define JOURNAL_ENTRY_H  72

void flux_ui_draw_journal(flux_fb_t *fb, const char **names, int n,
                           int scroll, int selected) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    flux_fb_text(fb, 16, STATUSBAR_H + 12, "Tages-Journal", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, 16, STATUSBAR_H + 42, 140, 2, COL_ACCENT, COL_ACCENT2);

    int list_y = STATUSBAR_H + TITLE_AREA_H;
    int list_h = fb->height - list_y - LIST_BACK_H;

    if (n == 0) {
        fill_round_rect(fb, 12, list_y + 20, fb->width - 24, 72, 10, COL_SURFACE);
        const char *empty = "Noch keine Journal-Eintraege vorhanden.";
        int tw = flux_fb_text_width(empty, 2);
        flux_fb_text(fb, (fb->width - tw) / 2, list_y + 44, empty, COL_DIM, 2);
        draw_back_bar(fb, "Zurueck");
        flux_fb_present(fb);
        return;
    }

    int y0 = list_y + 4;
    int max_visible = list_h / JOURNAL_ENTRY_H;
    if (scroll < 0) scroll = 0;
    if (scroll > n - max_visible && n > max_visible) scroll = n - max_visible;

    for (int i = scroll; i < n && y0 + JOURNAL_ENTRY_H <= list_y + list_h; i++) {
        int ey = y0;
        int sel = (i == selected);
        fill_round_rect(fb, 8, ey, fb->width - 16, JOURNAL_ENTRY_H - 6, 8,
                        sel ? COL_SURFACE3 : COL_SURFACE);
        flux_fb_fill_rect(fb, 8, ey, 3, JOURNAL_ENTRY_H - 6, COL_ACCENT);

        /* Datum gross, Wochentag klein */
        const char *date = names[i];
        flux_fb_text(fb, 20, ey + 14, date, COL_TEXT, 2);

        /* Pfeil rechts */
        int ax = fb->width - 28;
        flux_fb_text(fb, ax, ey + 14, ">", COL_DIM, 2);

        y0 += JOURNAL_ENTRY_H;
    }

    /* Scroll-Indikator */
    if (n > max_visible) {
        int bar_h = list_h * max_visible / n;
        int bar_y = list_y + list_h * scroll / n;
        fill_round_rect(fb, fb->width - 5, bar_y, 4, bar_h, 2, COL_ACCENT);
    }

    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}

int flux_ui_journal_hit(const flux_fb_t *fb, int x, int y,
                         int n, int *back) {
    *back = 0;
    if (y >= fb->height - LIST_BACK_H) { *back = 1; return -1; }
    int list_y = STATUSBAR_H + TITLE_AREA_H;
    int list_h = fb->height - list_y - LIST_BACK_H;
    int max_visible = list_h / JOURNAL_ENTRY_H;
    if (y < list_y || x < 8 || x > fb->width - 8) return -1;
    int rel = y - list_y - 4;
    int idx = rel / JOURNAL_ENTRY_H;
    if (idx < 0 || idx >= max_visible || idx >= n) return -1;
    return idx;
}

/* ---- Stimm-Entsperrung (zweiter Faktor) ----------------------------- */

/* Zeichnet Mikrofon-Icon einfach mit Text fuer diesen Screen. */
static void draw_mic_icon_lg(flux_fb_t *fb, int cx, int cy, uint32_t col) {
    /* Koerper: abgerundetes Rechteck */
    fill_round_rect(fb, cx - 18, cy - 36, 36, 52, 14, col);
    /* Buegel unten */
    for (int dx = -24; dx <= 24; dx += 2)
        flux_fb_fill_rect(fb, cx + dx, cy + 20, 2, 3, col);
    /* Stiel */
    flux_fb_fill_rect(fb, cx - 2, cy + 22, 4, 12, col);
    /* Basis */
    flux_fb_fill_rect(fb, cx - 14, cy + 34, 28, 3, col);
}

void flux_ui_draw_voice_enroll(flux_fb_t *fb, int phase, const char *msg) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    flux_fb_text(fb, 16, STATUSBAR_H + 12, "Stimme einlernen", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, 16, STATUSBAR_H + 42, 160, 2, COL_ACCENT, COL_ACCENT2);

    int cy = STATUSBAR_H + TITLE_AREA_H + 60;

    uint32_t mic_col = (phase == 1) ? 0xEF4444 :
                       (phase == 2) ? 0x10B981 :
                       (phase == 3) ? 0xEF4444 : COL_ACCENT;
    draw_mic_icon_lg(fb, fb->width / 2, cy, mic_col);

    /* Statustext */
    int my = cy + 80;
    if (msg && *msg) {
        int tw = flux_fb_text_width(msg, 2);
        if (tw > fb->width - 32) {
            /* Zeilenumbruch bei zu langem Text */
            char tmp[256]; strncpy(tmp, msg, 255); tmp[255] = '\0';
            flux_fb_text(fb, 16, my, tmp, COL_TEXT, 2);
        } else {
            flux_fb_text(fb, (fb->width - tw) / 2, my, msg, COL_TEXT, 2);
        }
        my += 30;
    }

    /* Anleitung */
    const char *hint = (phase == 0) ? "Sagen Sie einen kurzen Satz (3 Sek.)" :
                       (phase == 1) ? "Aufnahme laeuft..." :
                       (phase == 2) ? "Stimme gespeichert!" :
                                      "Fehler -- bitte erneut versuchen";
    int tw = flux_fb_text_width(hint, 2);
    flux_fb_text(fb, (fb->width - tw) / 2, my, hint, COL_DIM, 2);

    /* Buttons */
    int bw = fb->width - 32, bh = 52;
    int b1y = fb->height - LIST_BACK_H - bh * 2 - 24;
    int b2y = b1y + bh + 12;

    /* Aufnehmen / Wiederholen */
    fill_round_rect(fb, 16, b1y, bw, bh, 10, COL_ACCENT);
    const char *btn1 = (phase == 2) ? "Nochmal aufnehmen" : "Aufnahme starten";
    tw = flux_fb_text_width(btn1, 2);
    flux_fb_text(fb, (fb->width - tw) / 2, b1y + (bh - 14) / 2, btn1, 0xFFFFFF, 2);

    /* Ueberspringen / Fertig */
    fill_round_rect(fb, 16, b2y, bw, bh, 10, COL_SURFACE2);
    const char *btn2 = (phase == 2) ? "Fertig" : "Ueberspringen";
    tw = flux_fb_text_width(btn2, 2);
    flux_fb_text(fb, (fb->width - tw) / 2, b2y + (bh - 14) / 2, btn2, COL_TEXT_MUTED, 2);

    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}

void flux_ui_draw_voice_verify(flux_fb_t *fb, int phase, const char *msg) {
    flux_fb_clear(fb, COL_BG);
    draw_statusbar(fb);

    flux_fb_text(fb, 16, STATUSBAR_H + 12, "Stimm-Verifizierung", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, 16, STATUSBAR_H + 42, 180, 2, COL_ACCENT, COL_ACCENT2);

    int cy = STATUSBAR_H + TITLE_AREA_H + 60;

    uint32_t mic_col = (phase == 1) ? 0xEF4444 :
                       (phase == 2) ? 0x10B981 :
                       (phase == 3) ? 0xEF4444 : COL_ACCENT;
    draw_mic_icon_lg(fb, fb->width / 2, cy, mic_col);

    int my = cy + 80;
    if (msg && *msg) {
        int tw = flux_fb_text_width(msg, 2);
        if (tw > fb->width - 32)
            flux_fb_text(fb, 16, my, msg, COL_TEXT, 2);
        else
            flux_fb_text(fb, (fb->width - tw) / 2, my, msg, COL_TEXT, 2);
        my += 30;
    }

    const char *hint = (phase == 0) ? "Sprechen Sie bitte (3 Sekunden)" :
                       (phase == 1) ? "Hoere zu..." :
                       (phase == 2) ? "Entsperrt!" :
                                      "Nicht erkannt -- PIN verwenden";
    int tw = flux_fb_text_width(hint, 2);
    flux_fb_text(fb, (fb->width - tw) / 2, my,
                 hint, (phase == 2) ? 0x10B981 : (phase == 3) ? COL_DANGER : COL_DIM, 2);

    int bw = fb->width - 32, bh = 52;
    int b1y = fb->height - LIST_BACK_H - bh * 2 - 24;
    int b2y = b1y + bh + 12;

    /* Aufnehmen */
    fill_round_rect(fb, 16, b1y, bw, bh, 10, (phase == 2) ? 0x10B981 : COL_ACCENT);
    const char *btn1 = (phase == 2) ? "Weiter" : "Sprechen";
    tw = flux_fb_text_width(btn1, 2);
    flux_fb_text(fb, (fb->width - tw) / 2, b1y + (bh - 14) / 2, btn1, 0xFFFFFF, 2);

    /* PIN verwenden */
    fill_round_rect(fb, 16, b2y, bw, bh, 10, COL_SURFACE2);
    tw = flux_fb_text_width("PIN verwenden", 2);
    flux_fb_text(fb, (fb->width - tw) / 2, b2y + (bh - 14) / 2, "PIN verwenden", COL_TEXT_MUTED, 2);

    flux_fb_present(fb);
}

int flux_ui_voice_hit(const flux_fb_t *fb, int x, int y) {
    int bw = fb->width - 32, bh = 52;
    int b1y = fb->height - LIST_BACK_H - bh * 2 - 24;
    int b2y = b1y + bh + 12;
    if (x >= 16 && x < 16 + bw && y >= b1y && y < b1y + bh) return 1; /* Aufnehmen */
    if (x >= 16 && x < 16 + bw && y >= b2y && y < b2y + bh) return 2; /* PIN/Skip */
    return 0;
}

/* ---- Alarm-Screen --------------------------------------------------- */

void flux_ui_draw_alarm(flux_fb_t *fb, const char *label) {
    flux_fb_fill_gradient_v(fb, 0, 0, fb->width, fb->height, 0x2A0000, 0x180000);

    int cx = fb->width / 2;
    int icon_y = fb->height * 25 / 100;

    /* Klingelnde Glocke (Lucide bell-ring) */
    flux_icon_draw(fb, FLUX_ICON_BELL_RING, cx, icon_y + 40, 96, 0xFF3333);

    /* "WECKER" in gross */
    const char *head = "WECKER";
    int hw = flux_fb_text_width(head, 4);
    flux_fb_text(fb, (fb->width - hw) / 2, fb->height * 52 / 100, head, 0xFF6666, 4);

    /* Alarm-Beschriftung */
    if (label && *label) {
        char disp[80]; snprintf(disp, sizeof(disp), "%.72s", label);
        int lw = flux_fb_text_width(disp, 2);
        if (lw > fb->width - 40) {
            /* zu lang -- zweizeilig umbrechen */
            char l1[40], l2[40];
            int split = 35;
            while (split > 0 && disp[split] != ' ') split--;
            if (split == 0) split = 35;
            snprintf(l1, sizeof(l1), "%.*s", split, disp);
            snprintf(l2, sizeof(l2), "%s", disp + split + 1);
            int y1 = fb->height * 63 / 100;
            flux_fb_text(fb, (fb->width - flux_fb_text_width(l1, 2)) / 2, y1, l1, 0xFFCCCC, 2);
            flux_fb_text(fb, (fb->width - flux_fb_text_width(l2, 2)) / 2, y1+20, l2, 0xFFCCCC, 2);
        } else {
            flux_fb_text(fb, (fb->width - lw) / 2, fb->height * 63 / 100, disp, 0xFFCCCC, 2);
        }
    }

    /* Abbrechen-Button */
    int bw2 = fb->width - 48, bh2 = 56, bx2 = 24;
    int by2 = fb->height * 79 / 100;
    fill_round_rect(fb, bx2, by2, bw2, bh2, 14, 0xCC2222);
    int tw2 = flux_fb_text_width("Abbrechen", 2);
    flux_fb_text(fb, bx2 + (bw2 - tw2) / 2, by2 + (bh2 - 14) / 2, "Abbrechen", 0xFFFFFF, 2);

    flux_fb_present(fb);
}

/* ---- Anruf-Screen (Vollbild, wie der Alarm) ------------------------- */

/* Gemeinsame Geometrie der runden Anruf-Knoepfe -- von Zeichnen UND
 * Hit-Test genutzt, damit beide nie auseinanderdriften. gx<0 = kein
 * gruener Knopf (Gespraech laeuft, nur Auflegen). */
static void call_btn_geom(const flux_fb_t *fb, int connected,
                          int *gx, int *rx, int *by, int *r) {
    *r  = 42;
    *by = fb->height * 80 / 100;
    if (connected) { *gx = -1;                   *rx = fb->width / 2; }
    else           { *gx = fb->width * 30 / 100; *rx = fb->width * 70 / 100; }
}

void flux_ui_draw_call(flux_fb_t *fb, const char *name, const char *number,
                       int connected, int elapsed_s) {
    flux_fb_fill_gradient_v(fb, 0, 0, fb->width, fb->height, 0x0C1A16, 0x06100D);

    int cx = fb->width / 2;
    int icon_cy = fb->height * 24 / 100;

    /* Telefon-Symbol in einem dezenten Kreis (Lucide phone-call) */
    fill_circle(fb, cx, icon_cy, 56, 0x143026);
    flux_icon_draw(fb, FLUX_ICON_PHONE_CALL, cx, icon_cy, 64, 0x34D399);

    /* Name gross */
    const char *nm = (name && *name) ? name : "Unbekannt";
    char dispn[48]; snprintf(dispn, sizeof(dispn), "%.44s", nm);
    int nscale = (flux_fb_text_width(dispn, 4) > fb->width - 40) ? 3 : 4;
    int nw = flux_fb_text_width(dispn, nscale);
    flux_fb_text(fb, (fb->width - nw) / 2, fb->height * 44 / 100, dispn, 0xFFFFFF, nscale);

    /* Nummer darunter */
    if (number && *number) {
        char dn[48]; snprintf(dn, sizeof(dn), "%.44s", number);
        int w = flux_fb_text_width(dn, 2);
        flux_fb_text(fb, (fb->width - w) / 2, fb->height * 52 / 100, dn, 0x9CA3AF, 2);
    }

    /* Status-Zeile: bei Verbindung mit laufendem Timer */
    char st[40];
    if (connected) snprintf(st, sizeof(st), "Verbunden  %d:%02d", elapsed_s / 60, elapsed_s % 60);
    else           snprintf(st, sizeof(st), "Eingehender Anruf...");
    int sw = flux_fb_text_width(st, 2);
    flux_fb_text(fb, (fb->width - sw) / 2, fb->height * 58 / 100, st,
                 connected ? 0x34D399 : 0x9CA3AF, 2);

    /* Aufnahme-Indikator: pulsierender roter Punkt + Hinweis, dass die KI
     * den Anruf mitschneidet (Transkription per Whisper, Stub ohne Mikrofon). */
    if (connected) {
        int ry = fb->height * 64 / 100;
        const char *rec = "KI nimmt auf";
        int rw = flux_fb_text_width(rec, 2);
        int total = rw + 20;
        int rx0 = (fb->width - total) / 2;
        if (flux_pulse(flux_now_ms(), 1100) > 0.45f)
            fill_circle(fb, rx0 + 6, ry + 7, 5, 0xEF4444);          /* blinkender Rec-Punkt */
        flux_fb_text(fb, rx0 + 20, ry, rec, 0xEF4444, 2);
        const char *hint = "Mitschnitt wird beim Auflegen gespeichert";
        int hw = flux_fb_text_width(hint, 1);
        flux_fb_text(fb, (fb->width - hw) / 2, ry + 22, hint, 0x6B7280, 1);
    }

    /* Runde Aktions-Knoepfe */
    int gx, rx, by, r; call_btn_geom(fb, connected, &gx, &rx, &by, &r);
    if (gx >= 0) {
        fill_circle(fb, gx, by, r, 0x22C55E);                 /* gruen: annehmen */
        flux_icon_draw(fb, FLUX_ICON_PHONE, gx, by, r + 4, 0xFFFFFF);
        const char *la = "Annehmen"; int lw = flux_fb_text_width(la, 2);
        flux_fb_text(fb, gx - lw / 2, by + r + 14, la, 0xCCCCCC, 2);
    }
    fill_circle(fb, rx, by, r, 0xEF4444);                     /* rot: auflegen */
    flux_icon_draw(fb, FLUX_ICON_PHONE_OFF, rx, by, r + 4, 0xFFFFFF);
    const char *lh = "Auflegen"; int lw2 = flux_fb_text_width(lh, 2);
    flux_fb_text(fb, rx - lw2 / 2, by + r + 14, lh, 0xCCCCCC, 2);

    flux_fb_present(fb);
}

flux_call_hit_t flux_ui_call_hit(const flux_fb_t *fb, int x, int y, int connected) {
    int gx, rx, by, r; call_btn_geom(fb, connected, &gx, &rx, &by, &r);
    int hr = r + 14;   /* etwas grosszuegiger Tap-Bereich */
    if (gx >= 0) {
        int dx = x - gx, dy = y - by;
        if (dx * dx + dy * dy <= hr * hr) return FLUX_CALL_ACCEPT;
    }
    int dx = x - rx, dy = y - by;
    if (dx * dx + dy * dy <= hr * hr) return FLUX_CALL_HANGUP;
    return FLUX_CALL_NONE;
}

/* ---- Nutzungsgewohnheiten ------------------------------------------- */

#define HABITS_ROW_H 52

void flux_ui_draw_habits(flux_fb_t *fb, const char **lines, int n, int scroll) {
    flux_fb_fill_rect(fb, 0, 0, fb->width, fb->height, COL_BG);
    draw_statusbar(fb);

    flux_fb_text(fb, 16, STATUSBAR_H + 12, "Nutzungsgewohnheiten", COL_TEXT, 3);
    flux_fb_fill_gradient_h(fb, 16, STATUSBAR_H + 42, 230, 2, COL_ACCENT, COL_ACCENT2);

    int list_y = STATUSBAR_H + TITLE_AREA_H;
    int list_h  = fb->height - list_y - LIST_BACK_H;
    int visible = list_h / HABITS_ROW_H;

    for (int i = 0; i < visible && (scroll + i) < n; i++) {
        int ry = list_y + i * HABITS_ROW_H;
        flux_fb_fill_rect(fb, 0, ry, fb->width, HABITS_ROW_H,
                          (i % 2 == 0) ? COL_ROW : COL_ROW_ALT);
        flux_fb_fill_rect(fb, 0, ry + 4, 3, HABITS_ROW_H - 8, COL_ACCENT);

        /* Format: [YYYY-MM-DD HH:MM] screen | topic
         * Zeige HH:MM (klein, gedaempft) und topic (gedaempft) zweizeilig. */
        const char *raw = lines[scroll + i];
        char time_label[8] = {0};
        const char *topic = raw;

        /* Timestamp parsen: [YYYY-MM-DD HH:MM] */
        if (raw[0] == '[') {
            const char *p = raw + 1;
            /* springe zum HH:MM Teil (nach dem Datum-Leerzeichen) */
            const char *space = strchr(p, ' ');
            if (space) {
                snprintf(time_label, sizeof(time_label), "%.5s", space + 1);
            }
            const char *close = strchr(p, ']');
            if (close) topic = close + 2; /* skip "] " */
        }
        /* Thema: nach " | " suchen */
        const char *pipe = strstr(topic, " | ");
        if (pipe) topic = pipe + 3;

        int center_y = ry + HABITS_ROW_H / 2;
        if (time_label[0]) {
            flux_fb_text(fb, 10, center_y - 13, time_label, COL_DIM, 2);
        }
        char tbuf[56]; snprintf(tbuf, sizeof(tbuf), "%.50s", topic);
        flux_fb_text(fb, 10, center_y + 1, tbuf, COL_TEXT_MUTED, 2);
    }

    if (n == 0) {
        const char *empty = "Noch keine Gewohnheiten erfasst.";
        int tw = flux_fb_text_width(empty, 2);
        flux_fb_text(fb, (fb->width - tw) / 2, list_y + 60, empty, COL_DIM, 2);
    }

    draw_back_bar(fb, "Zurueck");
    flux_fb_present(fb);
}

int flux_ui_habits_hit(const flux_fb_t *fb, int x, int y, int *back) {
    (void)x;
    *back = (y >= fb->height - LIST_BACK_H);
    return *back;
}

/* Animierter Aufnahme-Indikator: Mikrofon + pulsierende Ringe +
 * laufende Wellenform. frame zaehlt mit jedem Redraw hoch (~10 fps). */
void flux_ui_draw_voice_overlay(flux_fb_t *fb, int elapsed_s, int frame) {
    int w = fb->width, h = fb->height;
    int cx = w / 2, cy = h / 2 - 40;
    const uint32_t RED = 0xE05252;

    flux_fb_clear(fb, 0x0A0613);

    /* Pulsierende Ringe (weich ueber Dreieckswelle) */
    for (int i = 0; i < 3; i++) {
        int r = 58 + i*18 + tri_wave(frame + i*5, 16, 12);
        uint32_t c = (i == 0) ? RED : (i == 1) ? 0x7A2330 : 0x49202A;
        draw_ring(fb, cx, cy, r, 3, c);
    }

    /* Mikrofon in der Mitte (leichtes Aufpulsieren) */
    int msz = 70 + tri_wave(frame, 16, 6);
    draw_input_icon(fb, INICON_MIC, cx, cy, msz, RED, 0x0A0613);

    /* Laufende Wellenform unter dem Mikrofon */
    int bars = 11, bw = 5, gap = 7;
    int x0 = cx - (bars*bw + (bars-1)*gap) / 2;
    int wy = cy + 96;
    for (int i = 0; i < bars; i++) {
        int bh = 4 + tri_wave(frame*2 + i*3, 14, 30);
        flux_fb_fill_rect(fb, x0 + i*(bw+gap), wy - bh, bw, bh*2, RED);
    }

    /* Timer */
    char timer_buf[16];
    int m = elapsed_s / 60, s = elapsed_s % 60;
    snprintf(timer_buf, sizeof(timer_buf), "%02d:%02d", m, s);
    int tw = flux_fb_text_width(timer_buf, 4);
    flux_fb_text(fb, (w-tw)/2, wy + 36, timer_buf, RED, 4);

    /* Anweisung */
    const char *hint = "Sprich jetzt -- nochmal tippen zum Stoppen";
    tw = flux_fb_text_width(hint, 2);
    flux_fb_text(fb, (w-tw)/2, wy + 84, hint, COL_DIM, 2);

    flux_fb_present(fb);
}
