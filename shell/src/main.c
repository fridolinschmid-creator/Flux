/* flux-shell -- der einzige "App"-Prozess, der am Anfang laeuft.
 * Kein Homescreen mit Icon-Grid: man entsperrt direkt in den
 * KI-Assistenten (optional hinter einem PIN-Code). Zeichnet auf den
 * Framebuffer, fragt fluxaid ueber den Unix-Socket. Bedienung
 * touch-first (Wischen, eigene Bildschirmtastatur) -- eine
 * Hardware-Tastatur funktioniert weiterhin, ist aber nicht mehr
 * Voraussetzung (siehe input.h).
 *
 * Einstellungen und Dateien sind ueber den Assistenten erreichbar
 * (Schnellzugriff-Knoepfe oder Tippen/Sprechen von "Einstellungen"/
 * "Dateien"), nicht ueber ein zweites App-Grid -- siehe ui.h.
 *
 * Will die KI eine Mail/SMS/einen Anruf ausloesen, antwortet fluxaid
 * mit einem ACTION:-Block (siehe fluxai/src/provider.c). Dieser wird
 * hier geparst (action.h) und fuehrt NIE direkt etwas aus -- es gibt
 * immer erst den Bestaetigungs-Dialog (FLUX_SCREEN_CONFIRM).
 */
#include "fb.h"
#include "input.h"
#include "ipc.h"
#include "ui.h"
#include "action.h"
#include "camera.h"
#include "voice.h"
#include "voice_unlock.h"
#include "wifi.h"
#include "../../common/flux_protocol.h"
#include "../../common/flux_config.h"
#include "../../common/flux_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>

/* ---- Uebergangs-Animation (Einblenden von unten) --------------------- */

/* Speichert den aktuellen Backbuffer-Zustand. Muss mit free() freigegeben
 * werden. Gibt NULL bei Speicherfehler. */
static uint32_t *capture_frame(const flux_fb_t *fb) {
    size_t npx = (size_t)fb->width * fb->height;
    uint32_t *buf = malloc(npx * sizeof(uint32_t));
    if (buf) memcpy(buf, fb->back, npx * sizeof(uint32_t));
    return buf;
}

/* Animiert den Uebergang vom gespeicherten Bild im old_buf zum aktuellen
 * Inhalt des Backbuffers (neuer Bildschirm).
 * Slide-in von unten mit leichtem Spring-Overshoot ("babbeln"). */
static void animate_slide_in(flux_fb_t *fb, uint32_t *old_buf) {
    if (!old_buf || !fb->mmio) return; /* kein echter Framebuffer */

    size_t npx = (size_t)fb->width * fb->height;
    uint32_t *new_buf = malloc(npx * sizeof(uint32_t));
    if (!new_buf) return;
    memcpy(new_buf, fb->back, npx * sizeof(uint32_t));

    int h = fb->height, w = fb->width;

    /* Offsets: positive = neuer Screen beginnt weiter unten (noch nicht voll da)
     * Negative = Overshoot (neuer Screen leicht ueber Ziel -- "federt") */
    int offsets[] = { h, h*4/5, h*3/5, h*2/5, h/5, 0, -18, -7, -2, 0 };
    int nframes = (int)(sizeof(offsets) / sizeof(offsets[0]));

    for (int f = 0; f < nframes; f++) {
        int off = offsets[f]; /* y-Position des Tops des neuen Screens */

        for (int y = 0; y < h; y++) {
            int src_new = y - off; /* Quellzeile im neuen Screen */
            uint32_t *dst = fb->back + y * w;
            if (src_new < 0 || src_new >= h) {
                /* Ausserhalb des neuen Screens: alten Screen zeigen */
                memcpy(dst, old_buf + y * w, (size_t)w * sizeof(uint32_t));
            } else {
                memcpy(dst, new_buf + src_new * w, (size_t)w * sizeof(uint32_t));
            }
        }
        /* Alle Zeilen als dirty markieren */
        memset(fb->prev, 0xFF, npx * sizeof(uint32_t));
        flux_fb_present(fb);
        usleep(14000); /* ~70 fps */
    }

    /* Backbuffer auf Endzustand restaurieren */
    memcpy(fb->back, new_buf, npx * sizeof(uint32_t));
    memset(fb->prev, 0xFF, npx * sizeof(uint32_t));
    flux_fb_present(fb);
    free(new_buf);
}

/* Animiert einen Expanding-Ring-Effekt beim Tap-Punkt.
 * Speichert den aktuellen Backbuffer, zeichnet 5 Frames, stellt ihn wieder her. */
static void animate_ripple(flux_fb_t *fb, int cx, int cy) {
    if (!fb->mmio) return;
    size_t npx = (size_t)fb->width * fb->height;
    uint32_t *saved = malloc(npx * sizeof(uint32_t));
    if (!saved) return;
    memcpy(saved, fb->back, npx * sizeof(uint32_t));
    for (int f = 0; f < 5; f++) {
        memcpy(fb->back, saved, npx * sizeof(uint32_t));
        flux_ui_draw_ripple(fb, cx, cy, f);
        memset(fb->prev, 0xFF, npx * sizeof(uint32_t));
        flux_fb_present(fb);
        usleep(45000);
    }
    memcpy(fb->back, saved, npx * sizeof(uint32_t));
    free(saved);
}

/* Spielt ein animiertes Aktions-Symbol fuer 'frames' Bilder ab. */
static void animate_action(flux_fb_t *fb, flux_anim_kind_t kind,
                           const char *caption, int frames, int delay_us) {
    for (int f = 0; f < frames; f++) {
        flux_ui_draw_action_anim(fb, kind, f, caption);
        usleep(delay_us);
    }
}

/* Speichert den KI-Mitschnitt eines Anrufs als Markdown unter
 * /home/user/Anrufe/. Ohne Mikrofon/Whisper bleibt das Transkript leer --
 * dann wird ein ehrlicher Hinweis geschrieben. Die Struktur ist schon die,
 * die ein echtes Whisper-Backend spaeter nur noch fuellen muss. Liegt
 * echter Transkript-Text vor, holt die KI zusaetzlich eine Zusammenfassung.
 * status: Rueckmeldung fuer die UI (Pfad oder Fehler). */
static void save_call_log(const char *name, const char *number,
                          const char *transcript, const char *tel_note,
                          int duration_s, char *status, size_t status_cap) {
    mkdir("/home/user/Anrufe", 0755);
    time_t nt = time(NULL); struct tm tmv; localtime_r(&nt, &tmv);

    /* Dateiname: Anruf_<Name>_<Zeit>.md, Name auf [A-Za-z0-9_] reduziert. */
    char safe[40]; int j = 0;
    for (const char *p = (name && *name) ? name : "Unbekannt";
         *p && j < (int)sizeof(safe) - 1; p++) {
        char c = *p;
        safe[j++] = ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9')) ? c : '_';
    }
    safe[j] = '\0';
    char stamp[20]; strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);
    char path[256];
    snprintf(path, sizeof(path), "/home/user/Anrufe/Anruf_%s_%s.md", safe, stamp);

    FILE *f = fopen(path, "w");
    if (!f) { snprintf(status, status_cap, "Mitschnitt konnte nicht gespeichert werden."); return; }

    char hdr[64]; strftime(hdr, sizeof(hdr), "%d.%m.%Y %H:%M", &tmv);
    fprintf(f, "# Anruf mit %s\n\n", (name && *name) ? name : "Unbekannt");
    fprintf(f, "- Datum: %s\n", hdr);
    if (number && *number) fprintf(f, "- Nummer: %s\n", number);
    fprintf(f, "- Dauer: %d:%02d\n", duration_s / 60, duration_s % 60);
    if (tel_note && *tel_note) fprintf(f, "- Verbindung: %s\n", tel_note);

    fprintf(f, "\n## Mitschnitt (KI-Transkription)\n\n");
    if (transcript && *transcript) {
        fprintf(f, "%s\n", transcript);
    } else {
        fprintf(f, "*(Keine Transkription -- kein Mikrofon/Whisper auf diesem Geraet. "
                   "Mit Mikrofon + lokalem whisper.cpp fuellt sich dieser Abschnitt "
                   "automatisch waehrend des Gespraechs.)*\n");
    }

    fprintf(f, "\n## Zusammenfassung (KI)\n\n");
    if (transcript && *transcript) {
        char q[8300]; char sum[2048];
        snprintf(q, sizeof(q),
                 "Fasse dieses Telefonat in hoechstens 3 Stichpunkten zusammen:\n%.7000s",
                 transcript);
        flux_ipc_ask(q, sum, sizeof(sum));
        fprintf(f, "%s\n", sum);
    } else {
        fprintf(f, "*(Folgt automatisch, sobald ein Transkript vorliegt.)*\n");
    }
    fclose(f);
    snprintf(status, status_cap, "Mitschnitt gespeichert: %s", path);
}

/* Eingangsanimation der Einstellungen: die Zeilen fliegen nacheinander
 * (abwechselnd von links/rechts) herein, ihr Symbol "poppt" auf. Ein Tap
 * beschleunigt auf 4x, ein weiterer Tap ueberspringt den Rest -- damit man
 * produktiv bleibt. labels/values muessen vorher geladen sein. */
static void animate_settings_intro(flux_fb_t *fb, flux_input_t *in,
                                   const char **labels, const char **values, int n) {
    const int FPR = 5;             /* Frames pro Zeile */
    int base_delay = 16000;        /* ~60 fps */
    int speed = 1;
    int total = n * FPR;
    for (int f = 0; f <= total; f++) {
        flux_event_t ev = flux_input_poll(in);
        if (ev.type == FLUX_EV_TAP) {
            if (speed >= 4) break;  /* zweiter Tap -> ueberspringen */
            speed = 4;              /* erster Tap -> 4x schneller */
        }
        int shown = f / FPR;
        int sub   = f % FPR;
        int slide = (FPR - sub) * 80 / FPR;      /* 80..0 px */
        int grow  = sub * 100 / FPR;             /* 0..100 % */
        int dir   = (shown % 2) ? -1 : 1;        /* abwechselnd links/rechts */
        flux_ui_draw_settings_reveal(fb, labels, values, n, shown, slide, grow, dir);
        usleep(base_delay / speed);
    }
    flux_ui_draw_settings(fb, labels, values, n);  /* sauberes Endbild */
}

/* Eingangsanimation des Homescreens nach dem Entsperren: die vier
 * Schnellzugriff-Knoepfe poppen nacheinander auf. Tap = 4x, zweiter Tap
 * ueberspringt. Setzt die Anzeige danach wieder auf "alle sichtbar". */
static void animate_home_intro(flux_fb_t *fb, flux_input_t *in,
                               const char *last_q, const char *input_buf,
                               const char *answer_buf) {
    const int FPB = 4;             /* Frames pro Knopf */
    int base_delay = 16000, speed = 1;
    int total = 4 * FPB;
    for (int f = 0; f <= total; f++) {
        flux_event_t ev = flux_input_poll(in);
        if (ev.type == FLUX_EV_TAP) { if (speed >= 4) break; speed = 4; }
        int shown = f / FPB;
        int grow  = (f % FPB) * 100 / FPB;
        flux_ui_set_quick_reveal(shown, grow);
        flux_ui_draw_assistant(fb, last_q, input_buf, answer_buf, 0);
        usleep(base_delay / speed);
    }
    flux_ui_set_quick_reveal(4, 100);
    flux_ui_draw_assistant(fb, last_q, input_buf, answer_buf, 0);
}

/* Slide-in-von-links fuer Zurueck-Navigationen (neuer Screen kommt von links). */
static void animate_slide_from_left(flux_fb_t *fb, uint32_t *old_buf) {
    if (!old_buf || !fb->mmio) return;
    size_t npx = (size_t)fb->width * fb->height;
    uint32_t *new_buf = malloc(npx * sizeof(uint32_t));
    if (!new_buf) return;
    memcpy(new_buf, fb->back, npx * sizeof(uint32_t));

    int h = fb->height, w = fb->width;
    /* Negative Werte: neuer Screen kommt von links, bewegt sich nach rechts */
    int offsets[] = { -w, -w*4/5, -w*3/5, -w*2/5, -w/5, 0, 12, 4, 1, 0 };
    int nframes = (int)(sizeof(offsets) / sizeof(offsets[0]));

    for (int f = 0; f < nframes; f++) {
        int off = offsets[f];
        for (int y = 0; y < h; y++) {
            uint32_t *dst = fb->back + y * w;
            for (int x = 0; x < w; x++) {
                int src_x = x - off;
                dst[x] = (src_x >= 0 && src_x < w) ? new_buf[y*w + src_x] : old_buf[y*w + x];
            }
        }
        memset(fb->prev, 0xFF, npx * sizeof(uint32_t));
        flux_fb_present(fb);
        usleep(14000);
    }
    memcpy(fb->back, new_buf, npx * sizeof(uint32_t));
    memset(fb->prev, 0xFF, npx * sizeof(uint32_t));
    flux_fb_present(fb);
    free(new_buf);
}

/* ---- Sprachausgabe (TTS) --------------------------------------------- */

static void tts_speak(const char *text) {
    char enabled[8] = {0};
    flux_config_get("tts", enabled, sizeof(enabled));
    if (enabled[0] != '1') return;
    if (!text || !*text) return;

    pid_t p = fork();
    if (p == 0) {
        /* espeak bevorzugt (Embedded Linux), dann flite als Fallback */
        execlp("espeak", "espeak", "-v", "de", "-s", "160", text, NULL);
        execlp("flite",  "flite",  "-t", text, NULL);
        _exit(0);
    }
    if (p > 0) waitpid(p, NULL, WNOHANG); /* Zombie sofort abraeumen */
}

/* ---- Screenshot --------------------------------------------------------- */

static void save_screenshot(const flux_fb_t *fb, char *msg_out, size_t msg_cap) {
    mkdir("/home/user", 0755);
    mkdir("/home/user/Screenshots", 0755);
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char path[256];
    strftime(path, sizeof(path),
             "/home/user/Screenshots/flux_%Y%m%d_%H%M%S.ppm", &tmv);
    FILE *f = fopen(path, "wb");
    if (!f) {
        snprintf(msg_out, msg_cap, "Screenshot-Fehler: %s", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", fb->width, fb->height);
    for (int i = 0; i < fb->width * fb->height; i++) {
        uint32_t px = fb->back[i];
        fputc((px >> 16) & 0xff, f);
        fputc((px >>  8) & 0xff, f);
        fputc( px        & 0xff, f);
    }
    fclose(f);
    snprintf(msg_out, msg_cap, "Screenshot gespeichert: %s", path);
}

/* ---- Farbthema ---------------------------------------------------------- */

static void apply_theme(void) {
    char theme[32] = {0};
    flux_config_get("theme", theme, sizeof(theme));
    if      (!strcmp(theme, "blau"))   flux_ui_set_accent(0x3B82F6);
    else if (!strcmp(theme, "lila"))   flux_ui_set_accent(0xA855F7);
    else if (!strcmp(theme, "orange")) flux_ui_set_accent(0xF97316);
    else if (!strcmp(theme, "gruen"))  flux_ui_set_accent(0x22C55E);
    else if (!strcmp(theme, "rot"))    flux_ui_set_accent(0xEF4444);
    /* teal ist default -- kein else noetig */
}

/* Generiert eine KI-Begruessung asynchron (Fork) wenn noch keine fuer heute existiert. */
static void maybe_generate_greeting(void) {
    time_t t = time(NULL); struct tm tmv; localtime_r(&t, &tmv);
    char flag[64];
    strftime(flag, sizeof(flag), "/tmp/flux_greet_%Y%m%d.done", &tmv);
    if (access(flag, F_OK) == 0) return;
    pid_t p = fork();
    if (p == 0) {
        sleep(3); /* Warten bis fluxaid bereit ist */
        char weather[128] = {0};
        FILE *wf = fopen("/tmp/flux_weather.txt", "r");
        if (wf) { if (!fgets(weather, sizeof(weather), wf)) weather[0] = '\0'; fclose(wf); }
        char q[256];
        if (weather[0])
            snprintf(q, sizeof(q), "Kurze freundliche Lockscreen-Begruessung (1 Satz, max 60 Zeichen). Wetter: %.60s", weather);
        else
            snprintf(q, sizeof(q), "Kurze freundliche Lockscreen-Begruessung (1 Satz, max 60 Zeichen).");
        char resp[256] = {0};
        flux_ipc_ask(q, resp, sizeof(resp));
        if (resp[0]) {
            FILE *f = fopen("/tmp/flux_greeting.txt", "w");
            if (f) { fprintf(f, "%s\n", resp); fclose(f); }
            FILE *g = fopen(flag, "w"); if (g) { fputc('1', g); fclose(g); }
        }
        _exit(0);
    }
    if (p > 0) waitpid(p, NULL, WNOHANG);
}

#define FLUX_PIN_LEN       4
#define FLUX_FILES_MAX     12
#define FLUX_SETTINGS_N    11   /* + WLAN + Stimme */
#define VIEWER_CONTENT_MAX 32768

typedef enum {
    EDIT_NONE = 0,
    EDIT_ACTION_BODY,
    EDIT_ACTION_TO,       /* Empfaenger im Bestaetigungs-Dialog aendern */
    EDIT_ACTION_SUBJECT,  /* Betreff im Bestaetigungs-Dialog aendern */
    EDIT_SETTING_FIELD,
    EDIT_NEW_FOLDER,
    EDIT_EMAIL_ADDR,    /* Schritt 1: E-Mail-Adresse */
    EDIT_EMAIL_PASS,    /* Schritt 2: App-Passwort */
    EDIT_WIFI_PASS,     /* WLAN-Passwort fuer das gewaehlte Netz */
} edit_target_t;

/* Zwischengespeicherte E-Mail-Adresse zwischen Schritt 1 und 2. */
static char email_pending[256] = {0};

/* ---- Einstellungen: Feldliste -------------------------------------
 * main.c maskiert Geheimnisse, bevor sie an ui.c gehen (siehe ui.h) --
 * ui.c/draw_settings weiss nichts von der Konfigurationsdatei. */

/* Die Felder "__active_key" und "__active_model" sind Platzhalter: sie
 * werden zur Laufzeit auf den Config-Key des aktuell gewaehlten Anbieters
 * abgebildet (Anthropic/DeepSeek/NVIDIA). So bleibt die Liste kurz und der
 * Key/Modell-Eintrag passt immer zum gewaehlten Anbieter. */
static const char *setting_keys[FLUX_SETTINGS_N] = {
    "pin_hash",
    "ai_provider",      /* anthropic|deepseek|nvidia -- per Tap durchschalten */
    "__active_key",     /* -> api_key | deepseek_key | nvidia_key */
    "__active_model",   /* -> anthropic_model | deepseek_model | nvidia_model */
    "__email",          /* E-Mail-Adresse + App-Passwort (leitet SMTP/IMAP ab) */
    "__wifi",           /* oeffnet den WLAN-Screen (Scan + Verbinden) */
    "searxng_url",      /* Web-Suche ueber eigene SearXNG-Instanz (z.B. MacBook) */
    "theme",    /* teal|blau|lila|orange|gruen|rot */
    "auto_lock",      /* 0=aus, 30, 60, 120, 300 Sekunden */
    "tts",            /* 0=aus, 1=ein */
    "__voice_enroll", /* oeffnet Stimm-Einlern-Screen */
};
static const char *setting_labels[FLUX_SETTINGS_N] = {
    "PIN-Code",
    "KI-Anbieter",          /* tippen schaltet anthropic/deepseek/nvidia */
    "API-Key (Anbieter)",
    "Modell (Anbieter)",
    "E-Mail Einstellungen", /* Adresse + App-Passwort, Rest automatisch */
    "WLAN",                 /* oeffnet Netz-Scan + Verbinden */
    "Web-Suche (SearXNG)",  /* URL der eigenen SearXNG-Instanz */
    "Farbthema",    /* teal/blau/lila/orange/gruen/rot */
    "Auto-Sperre",  /* 0=aus */
    "Sprache (TTS)",/* 0=aus, 1=ein */
    "Stimme (2. Faktor)", /* Stimm-Entsperrung einlernen */
};
static const int setting_secret[FLUX_SETTINGS_N] = {
    1, /* pin */
    0, /* provider */
    1, /* active key */
    0, /* active model */
    0, /* email (zeigt Adresse) */
    0, /* wifi (zeigt Verbindung) */
    0, /* searxng_url */
    0, 0, 0,  /* theme/auto_lock/tts */
    0,        /* voice_enroll */
};

/* Symbol je Einstellungs-Zeile (parallel zu setting_keys). */
static const int setting_icons[FLUX_SETTINGS_N] = {
    FLUX_SICON_LOCK,   /* pin */
    FLUX_SICON_AI,     /* ai_provider */
    FLUX_SICON_KEY,    /* __active_key */
    FLUX_SICON_CHIP,   /* __active_model */
    FLUX_SICON_MAIL,   /* __email */
    FLUX_SICON_WIFI,   /* __wifi */
    FLUX_SICON_SEARCH, /* searxng_url */
    FLUX_SICON_THEME,  /* theme */
    FLUX_SICON_CLOCK,  /* auto_lock */
    FLUX_SICON_SPEAKER,/* tts */
    FLUX_SICON_LOCK,   /* voice_enroll */
};

/* Aktuell gewaehlter Anbieter aus der Config (Standard: anthropic). */
static void get_active_provider(char *out, size_t cap) {
    out[0] = '\0';
    flux_config_get("ai_provider", out, cap);
    if (!out[0]) snprintf(out, cap, "anthropic");
}

/* Bildet "__active_key"/"__active_model" auf den realen Config-Key des
 * aktiven Anbieters ab; alle anderen Keys bleiben unveraendert. */
static const char *resolve_setting_key(const char *key) {
    char prov[64]; get_active_provider(prov, sizeof(prov));
    if (!strcmp(key, "__active_key")) {
        if (!strcmp(prov, "deepseek")) return "deepseek_key";
        if (!strcmp(prov, "nvidia"))   return "nvidia_key";
        return "api_key";
    }
    if (!strcmp(key, "__active_model")) {
        if (!strcmp(prov, "deepseek")) return "deepseek_model";
        if (!strcmp(prov, "nvidia"))   return "nvidia_model";
        return "anthropic_model";
    }
    /* E-Mail-Eintrag zeigt die konfigurierte Absenderadresse an */
    if (!strcmp(key, "__email")) return "smtp_from";
    return key;
}

/* Leitet SMTP-/IMAP-Server und -Ports aus der E-Mail-Domain ab und schreibt
 * alle noetigen Config-Keys. So muss der Nutzer nur Adresse + App-Passwort
 * eingeben; Hosts/Ports und Benutzernamen werden automatisch gesetzt. */
static void email_account_apply(const char *email, const char *pass) {
    const char *at = strrchr(email, '@');
    if (!at || !at[1]) return; /* ungueltige Adresse -> nichts aendern */

    char domain[128];
    snprintf(domain, sizeof(domain), "%s", at + 1);
    for (char *p = domain; *p; p++)
        if (*p >= 'A' && *p <= 'Z') *p += 32;

    struct { const char *dom, *sh, *sp, *ih, *ip; } tbl[] = {
        { "gmail.com",     "smtp.gmail.com",         "587", "imap.gmail.com",          "993" },
        { "googlemail.com","smtp.gmail.com",         "587", "imap.gmail.com",          "993" },
        { "outlook.com",   "smtp-mail.outlook.com",  "587", "outlook.office365.com",   "993" },
        { "hotmail.com",   "smtp-mail.outlook.com",  "587", "outlook.office365.com",   "993" },
        { "live.com",      "smtp-mail.outlook.com",  "587", "outlook.office365.com",   "993" },
        { "msn.com",       "smtp-mail.outlook.com",  "587", "outlook.office365.com",   "993" },
        { "icloud.com",    "smtp.mail.me.com",       "587", "imap.mail.me.com",        "993" },
        { "me.com",        "smtp.mail.me.com",       "587", "imap.mail.me.com",        "993" },
        { "mac.com",       "smtp.mail.me.com",       "587", "imap.mail.me.com",        "993" },
        { "yahoo.com",     "smtp.mail.yahoo.com",    "587", "imap.mail.yahoo.com",     "993" },
        { "gmx.de",        "mail.gmx.net",           "587", "imap.gmx.net",            "993" },
        { "gmx.net",       "mail.gmx.net",           "587", "imap.gmx.net",            "993" },
        { "gmx.com",       "mail.gmx.com",           "587", "imap.gmx.com",            "993" },
        { "web.de",        "smtp.web.de",            "587", "imap.web.de",             "993" },
        { "t-online.de",   "securesmtp.t-online.de", "587", "secureimap.t-online.de",  "993" },
        { "posteo.de",     "posteo.de",              "587", "posteo.de",               "993" },
        { "mailbox.org",   "smtp.mailbox.org",       "587", "imap.mailbox.org",        "993" },
        { NULL, NULL, NULL, NULL, NULL }
    };

    const char *sh = NULL, *sp = NULL, *ih = NULL, *ip = NULL;
    for (int i = 0; tbl[i].dom; i++)
        if (strcmp(domain, tbl[i].dom) == 0) {
            sh = tbl[i].sh; sp = tbl[i].sp; ih = tbl[i].ih; ip = tbl[i].ip; break;
        }

    char sh_buf[160], ih_buf[160];
    if (!sh) { /* unbekannte Domain: gaengige Konvention smtp./imap.<domain> */
        snprintf(sh_buf, sizeof(sh_buf), "smtp.%s", domain); sh = sh_buf; sp = "587";
        snprintf(ih_buf, sizeof(ih_buf), "imap.%s", domain); ih = ih_buf; ip = "993";
    }

    flux_config_set("smtp_host", sh);
    flux_config_set("smtp_port", sp);
    flux_config_set("smtp_user", email);
    flux_config_set("smtp_pass", pass);
    flux_config_set("smtp_from", email);
    flux_config_set("imap_host", ih);
    flux_config_set("imap_port", ip);
    flux_config_set("imap_user", email);
    flux_config_set("imap_pass", pass);
}

/* Standardmodell des aktiven Anbieters (muss mit fluxai/src/provider.c
 * uebereinstimmen). */
static const char *active_default_model(void) {
    char prov[64]; get_active_provider(prov, sizeof(prov));
    if (!strcmp(prov, "deepseek")) return "deepseek-chat";
    if (!strcmp(prov, "nvidia"))   return "meta/llama-3.1-8b-instruct";
    return "claude-haiku-4-5-20251001";
}

static char setting_values_buf[FLUX_SETTINGS_N][200];
static const char *setting_values[FLUX_SETTINGS_N];

static void load_settings_values(void) {
    for (int i = 0; i < FLUX_SETTINGS_N; i++) {
        const char *cfg_key = resolve_setting_key(setting_keys[i]);
        char raw[200] = {0};
        flux_config_get(cfg_key, raw, sizeof(raw));
        if (strcmp(setting_keys[i], "pin_hash") == 0) {
            snprintf(setting_values_buf[i], sizeof(setting_values_buf[0]), "%s",
                      raw[0] ? "gesetzt" : "nicht gesetzt");
        } else if (strcmp(setting_keys[i], "ai_provider") == 0) {
            const char *p = raw[0] ? raw : "anthropic";
            const char *nice = !strcmp(p, "deepseek") ? "DeepSeek"
                             : !strcmp(p, "nvidia")   ? "NVIDIA NIM"
                                                      : "Anthropic Claude";
            snprintf(setting_values_buf[i], sizeof(setting_values_buf[0]), "%s", nice);
        } else if (strcmp(setting_keys[i], "__active_model") == 0) {
            snprintf(setting_values_buf[i], sizeof(setting_values_buf[0]), "%s",
                      raw[0] ? raw : active_default_model());
        } else if (strcmp(setting_keys[i], "__wifi") == 0) {
            char cur[64] = {0};
            flux_wifi_current(cur, sizeof(cur));
            snprintf(setting_values_buf[i], sizeof(setting_values_buf[0]), "%s",
                      cur[0] ? cur : "nicht verbunden");
        } else if (strcmp(setting_keys[i], "__voice_enroll") == 0) {
            snprintf(setting_values_buf[i], sizeof(setting_values_buf[0]), "%s",
                      voice_unlock_enrolled() ? "eingelernt" :
                      voice_unlock_available() ? "nicht eingelernt" :
                      "kein Mikrofon (QEMU)");
        } else if (setting_secret[i]) {
            snprintf(setting_values_buf[i], sizeof(setting_values_buf[0]), "%s",
                      raw[0] ? "********" : "(nicht gesetzt)");
        } else {
            snprintf(setting_values_buf[i], sizeof(setting_values_buf[0]), "%s",
                      raw[0] ? raw : "(nicht gesetzt)");
        }
        setting_values[i] = setting_values_buf[i];
    }
}

static void apply_setting_edit(int index, const char *value) {
    if (strcmp(setting_keys[index], "pin_hash") == 0) {
        if (value[0] == '\0') {
            flux_config_set("pin_hash", "");
        } else {
            char hash[65];
            flux_sha256_hex(value, hash);
            flux_config_set("pin_hash", hash);
        }
    } else {
        flux_config_set(resolve_setting_key(setting_keys[index]), value);
    }
    /* Farbthema sofort anwenden */
    if (strcmp(setting_keys[index], "theme") == 0)
        apply_theme();
}

/* ---- Dateien: einfacher Read-Only-Browser --------------------------
 * Kein Loeschen/Umbenennen -- ein erster, sicherer Schritt (siehe
 * ui.c-Kommentar bei flux_ui_draw_files). */

static char files_path[1024] = "/";
static char file_names_buf[FLUX_FILES_MAX][256];
static char file_metas_buf[FLUX_FILES_MAX][32];
static int  file_is_dir[FLUX_FILES_MAX];
static const char *file_names[FLUX_FILES_MAX];
static const char *file_metas[FLUX_FILES_MAX];
static int  file_n = 0;
static int  file_truncated = 0;
static int  file_selected = -1;   /* markierter Eintrag im Dateibrowser */

/* Kalender */
static int cal_year  = 2026;
static int cal_month = 1;
static int cal_today_day = 0;
static int cal_selected_day = 0;
#define CAL_EVENTS_MAX 20
static char cal_event_strs_buf[CAL_EVENTS_MAX][128];
static const char *cal_event_strs[CAL_EVENTS_MAX];
static int  cal_n_events = 0;

static void load_cal_events(int year, int month) {
    cal_n_events = 0;
    char prefix[12];
    snprintf(prefix, sizeof(prefix), "%04d-%02d", year, month);
    FILE *f = fopen("/etc/flux/calendar.txt", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f) && cal_n_events < CAL_EVENTS_MAX) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (!line[0] || line[0] == '#') continue;
        if (strncmp(line, prefix, 7) != 0) continue;
        snprintf(cal_event_strs_buf[cal_n_events], sizeof(cal_event_strs_buf[0]), "%s", line);
        cal_event_strs[cal_n_events] = cal_event_strs_buf[cal_n_events];
        cal_n_events++;
    }
    fclose(f);
}

/* "Heute"-Tag nur hervorheben, wenn der angezeigte Monat auch der reale
 * aktuelle Monat ist -- sonst leuchtet sonst in jedem Monat dieselbe Zahl. */
static int cal_today_view(void) {
    time_t t = time(NULL); struct tm m; localtime_r(&t, &m);
    if (cal_year == m.tm_year + 1900 && cal_month == m.tm_mon + 1)
        return m.tm_mday;
    return 0;
}

/* Kontakte */
#define CONTACTS_MAX 50
static char contact_names_buf[CONTACTS_MAX][64];
static char contact_details_buf[CONTACTS_MAX][128];
static const char *contact_names_p[CONTACTS_MAX];
static const char *contact_details_p[CONTACTS_MAX];
static int  contact_n = 0;
static int  contact_selected = -1;

static void load_contacts_list(void) {
    contact_n = 0;
    FILE *f = fopen("/etc/flux/contacts.txt", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f) && contact_n < CONTACTS_MAX) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (!line[0] || line[0] == '#') continue;
        char *sep = strchr(line, ',');
        if (sep) {
            *sep = '\0';
            snprintf(contact_names_buf[contact_n], sizeof(contact_names_buf[0]), "%s", line);
            snprintf(contact_details_buf[contact_n], sizeof(contact_details_buf[0]), "%s", sep+1);
        } else {
            snprintf(contact_names_buf[contact_n], sizeof(contact_names_buf[0]), "%s", line);
            contact_details_buf[contact_n][0] = '\0';
        }
        contact_names_p[contact_n]   = contact_names_buf[contact_n];
        contact_details_p[contact_n] = contact_details_buf[contact_n];
        contact_n++;
    }
    fclose(f);
}

/* Datei-Betrachter */
static char viewer_path[1024] = {0};
static char viewer_content[VIEWER_CONTENT_MAX] = {0};
static int  viewer_scroll = 0;

/* Fotogalerie */
#define GALLERY_MAX 50
static char gallery_names_buf[GALLERY_MAX][128];
static char gallery_dates_buf[GALLERY_MAX][32];
static const char *gallery_names[GALLERY_MAX];
static const char *gallery_dates[GALLERY_MAX];
static int gallery_n = 0;
static int gallery_selected = -1;
static int gallery_scroll = 0;          /* gescrollte Rasterzeilen */
static int gallery_filter = FLUX_GAL_ALLE;
static int gallery_select_mode = 0;     /* Auswahl-Stub */

/* Thumbnail-Cache: pro Dateiname genau eine fertige TILE*TILE Kachel
 * (center-gecroppt). Wird beim Neuladen der Galerie geleert, damit kein
 * Speicher pro Frame leckt (Muster wie der Icon-Cache in icons.c). */
typedef struct { char name[128]; uint32_t *tile; } thumb_entry_t;
static thumb_entry_t thumb_cache[GALLERY_MAX];
static int thumb_cache_n = 0;

static void thumb_cache_clear(void) {
    for (int i = 0; i < thumb_cache_n; i++) free(thumb_cache[i].tile);
    thumb_cache_n = 0;
}

/* KI-Kontext-Overlay (Wisch nach rechts von ueberall) */
static int  ai_ovl_active  = 0;
static char ai_ovl_input[512]   = {0};
static char ai_ovl_result[2048] = {0};
static char ai_ovl_label[80]    = {0};
static char ai_ovl_ctx[8192]    = {0};   /* Volltext-Kontext fuer die KI */
static char ai_ovl_save_path[256] = {0}; /* Pfad fuer "Als Datei speichern" */

/* Bild-Betrachter */
static char   image_path[512]   = {0};
static char   image_caption[512] = {0};
static int    image_analyzing   = 0;
static uint32_t *image_pixels   = NULL;
static int    image_w = 0, image_h = 0;

/* Meeting-Mitschrift */
static int  meeting_recording  = 0;
static time_t meeting_start_t  = 0;
static char meeting_transcript[8192] = {0};
static char meeting_status[128] = "Aufnahme-Knopf druecken um zu beginnen.";

/* KI-Gedaechtnis */
#define MEMORY_MAX 200
static char memory_entries_buf[MEMORY_MAX][256];
static const char *memory_entries[MEMORY_MAX];
static int  memory_n     = 0;
static int  memory_scroll = 0;

/* Journal-Screen */
#define JOURNAL_MAX 128
static char        journal_names_buf[JOURNAL_MAX][32];
static const char *journal_names_p[JOURNAL_MAX];
static int         journal_n      = 0;
static int         journal_scroll = 0;

static void load_journal_list(void) {
    mkdir("/home/user/Journal", 0755);
    DIR *d = opendir("/home/user/Journal");
    journal_n = 0;
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && journal_n < JOURNAL_MAX) {
        if (de->d_name[0] == '.') continue;
        size_t l = strlen(de->d_name);
        if (l > 4 && strcmp(de->d_name + l - 4, ".txt") == 0) {
            strncpy(journal_names_buf[journal_n], de->d_name, 31);
            journal_names_buf[journal_n][31] = '\0';
            /* Strip .txt */
            char *dot = strrchr(journal_names_buf[journal_n], '.');
            if (dot) *dot = '\0';
            journal_names_p[journal_n] = journal_names_buf[journal_n];
            journal_n++;
        }
    }
    closedir(d);
    /* sort descending (newest first) */
    for (int i = 0; i < journal_n - 1; i++)
        for (int j = i+1; j < journal_n; j++)
            if (strcmp(journal_names_buf[i], journal_names_buf[j]) < 0) {
                char tmp[32];
                memcpy(tmp, journal_names_buf[i], 32);
                memcpy(journal_names_buf[i], journal_names_buf[j], 32);
                memcpy(journal_names_buf[j], tmp, 32);
                journal_names_p[i] = journal_names_buf[i];
                journal_names_p[j] = journal_names_buf[j];
            }
}

/* Stimm-Entsperrung */
static int  voice_enroll_phase  = 0;  /* 0=Anleitung 1=Aufnahme 2=OK 3=Fehler */
static char voice_enroll_msg[256] = {0};
static int  voice_verify_phase  = 0;  /* 0=Warten 1=Aufnahme 2=OK 3=Fehler */
static char voice_verify_msg[256] = {0};

/* Alarm */
static char alarm_label[256] = {0};

/* Anruf-Vollbildschirm + KI-Mitschnitt */
static char   call_name[96]   = {0};
static char   call_number[96] = {0};
static int    call_connected  = 0;
static time_t call_start_t    = 0;
static char   call_transcript[8192] = {0};   /* Whisper-Transkript (Stub: leer) */
static char   call_tel_note[160]    = {0};   /* ehrliche Telefonie-Meldung */

/* Nutzungsgewohnheiten */
#define HABITS_MAX 200
static char        habits_buf[HABITS_MAX][128];
static const char *habits_p[HABITS_MAX];
static int         habits_n    = 0;
static int         habits_scroll = 0;

static void load_habits(void) {
    habits_n = 0;
    FILE *f = fopen("/etc/flux/habits.txt", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f) && habits_n < HABITS_MAX) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (!line[0]) continue;
        snprintf(habits_buf[habits_n], sizeof(habits_buf[0]), "%s", line);
        habits_p[habits_n] = habits_buf[habits_n];
        habits_n++;
    }
    fclose(f);
    /* Neueste zuerst: Array umkehren */
    for (int i = 0, j = habits_n - 1; i < j; i++, j--) {
        char tmp[128];
        memcpy(tmp, habits_buf[i], 128);
        memcpy(habits_buf[i], habits_buf[j], 128);
        memcpy(habits_buf[j], tmp, 128);
        habits_p[i] = habits_buf[i];
        habits_p[j] = habits_buf[j];
    }
}

/* Spracheingabe */
static int    voice_active  = 0;   /* 1 = Aufnahme laeuft, Overlay sichtbar */
static time_t voice_start_t = 0;
static int    voice_frame   = 0;   /* Animationszaehler des Aufnahme-Indikators */

/* Semantische KI-Suche */
#define SRCH_MAX 8
static char        search_query[256]         = {0};
static char        search_results_buf[SRCH_MAX][128];
static const char *search_results_p[SRCH_MAX];
static int         search_n                  = 0;
static int         search_searching          = 0;

/* WLAN */
#define WIFI_MAX 16
static flux_wifi_net_t wifi_nets[WIFI_MAX];
static char        wifi_names_buf[WIFI_MAX][64];
static char        wifi_metas_buf[WIFI_MAX][48];
static const char *wifi_names_p[WIFI_MAX];
static const char *wifi_metas_p[WIFI_MAX];
static int         wifi_n            = 0;
static char        wifi_current[64]  = {0};
static char        wifi_sel_ssid[64] = {0};
static int         wifi_sel_secured  = 0;

/* Scannt WLAN-Netze und befuellt die Anzeigepuffer. Zeigt waehrend der
 * Wartezeit ein animiertes WLAN-Such-Symbol (statt Text). */
static void wifi_rescan(flux_fb_t *fb) {
    flux_wifi_current(wifi_current, sizeof(wifi_current));
    wifi_n = 0;
    if (!flux_wifi_available()) return;

    flux_wifi_scan_trigger();
    for (int f = 0; f < 30 && fb; f++) {           /* ~2 s animiert */
        flux_ui_draw_action_anim(fb, FLUX_ANIM_SCAN, f, "WLAN-Suche");
        usleep(66000);
    }
    wifi_n = flux_wifi_scan_results(wifi_nets, WIFI_MAX);
    if (wifi_n < 0) wifi_n = 0;
    for (int i = 0; i < wifi_n; i++) {
        snprintf(wifi_names_buf[i], sizeof(wifi_names_buf[0]), "%s", wifi_nets[i].ssid);
        snprintf(wifi_metas_buf[i], sizeof(wifi_metas_buf[0]), "Signal %d%% - %s",
                 wifi_nets[i].signal_pct, wifi_nets[i].secured ? "gesichert" : "offen");
        wifi_names_p[i] = wifi_names_buf[i];
        wifi_metas_p[i] = wifi_metas_buf[i];
    }
}

static void load_memory(void) {
    memory_n = 0;
    FILE *f = fopen("/etc/flux/memory.txt", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f) && memory_n < MEMORY_MAX) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (!line[0]) continue;
        snprintf(memory_entries_buf[memory_n], sizeof(memory_entries_buf[0]), "%s", line);
        memory_entries[memory_n] = memory_entries_buf[memory_n];
        memory_n++;
    }
    fclose(f);
}

static void load_gallery(void) {
    gallery_n = 0;
    thumb_cache_clear();        /* Thumbnails neu aufbauen lassen (kein Leak) */
    DIR *d = opendir(FLUX_PICTURES_DIR);
    if (!d) { mkdir(FLUX_PICTURES_DIR, 0755); return; }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && gallery_n < GALLERY_MAX) {
        const char *name = e->d_name;
        size_t nl = strlen(name);
        if (nl < 4) continue;
        const char *ext = name + nl - 4;
        if (strcmp(ext, ".ppm") != 0 && strcmp(ext, ".jpg") != 0 &&
            strcmp(ext, ".png") != 0) continue;
        snprintf(gallery_names_buf[gallery_n], sizeof(gallery_names_buf[0]), "%s", name);
        /* Datum aus Dateinamen lesen (IMG_YYYYMMDD_HHMMSS.ppm) */
        gallery_dates_buf[gallery_n][0] = '\0';
        if (nl >= 20 && strncmp(name, "IMG_", 4) == 0) {
            char tmp[9];
            strncpy(tmp, name + 4, 8); tmp[8] = '\0';
            /* YYYYMMDD → TT.MM.JJJJ */
            snprintf(gallery_dates_buf[gallery_n], sizeof(gallery_dates_buf[0]),
                     "%.2s.%.2s.%.4s", tmp + 6, tmp + 4, tmp);
        } else {
            /* Datum per stat */
            char full[640];
            snprintf(full, sizeof(full), "%s/%s", FLUX_PICTURES_DIR, name);
            struct stat st;
            if (stat(full, &st) == 0) {
                struct tm tmv; localtime_r(&st.st_mtime, &tmv);
                strftime(gallery_dates_buf[gallery_n], sizeof(gallery_dates_buf[0]),
                         "%d.%m.%Y", &tmv);
            }
        }
        gallery_names[gallery_n] = gallery_names_buf[gallery_n];
        gallery_dates[gallery_n] = gallery_dates_buf[gallery_n];
        gallery_n++;
    }
    closedir(d);

    /* Nach Aufnahmedatum absteigend sortieren (neueste zuerst), damit
     * Monats-/Jahres-Gruppen im Raster zusammenhaengend liegen. Sortier-
     * schluessel "JJJJMMTT" aus dem Datum "TT.MM.JJJJ"; Fotos ohne Datum
     * ans Ende. Einfaches Insertion-Sort ueber die Index-Reihenfolge. */
    for (int i = 1; i < gallery_n; i++) {
        char ndate[32], nname[128];
        snprintf(ndate, sizeof(ndate), "%s", gallery_dates_buf[i]);
        snprintf(nname, sizeof(nname), "%s", gallery_names_buf[i]);
        char keyi[9];
        if (strlen(ndate) >= 10)
            snprintf(keyi, sizeof(keyi), "%.4s%.2s%.2s", ndate+6, ndate+3, ndate);
        else snprintf(keyi, sizeof(keyi), "00000000");
        int j = i - 1;
        while (j >= 0) {
            char keyj[9];
            if (strlen(gallery_dates_buf[j]) >= 10)
                snprintf(keyj, sizeof(keyj), "%.4s%.2s%.2s",
                         gallery_dates_buf[j]+6, gallery_dates_buf[j]+3, gallery_dates_buf[j]);
            else snprintf(keyj, sizeof(keyj), "00000000");
            if (strcmp(keyj, keyi) >= 0) break;     /* j ist neuer/gleich -> bleibt vor i */
            snprintf(gallery_dates_buf[j+1], sizeof(gallery_dates_buf[0]), "%s", gallery_dates_buf[j]);
            snprintf(gallery_names_buf[j+1], sizeof(gallery_names_buf[0]), "%s", gallery_names_buf[j]);
            j--;
        }
        snprintf(gallery_dates_buf[j+1], sizeof(gallery_dates_buf[0]), "%s", ndate);
        snprintf(gallery_names_buf[j+1], sizeof(gallery_names_buf[0]), "%s", nname);
    }
    for (int i = 0; i < gallery_n; i++) {
        gallery_names[i] = gallery_names_buf[i];
        gallery_dates[i] = gallery_dates_buf[i];
    }
}

/* Laedt eine PPM-Datei und skaliert sie per Nearest-Neighbor auf max target_w x target_h.
 * Gibt einen malloc'd RGB32-Puffer zurueck (Aufrufer muss free() aufrufen).
 * img_w/img_h: tatsaechliche Ausgabegroesse. */
static uint32_t *load_ppm_scaled(const char *path, int target_w, int target_h,
                                   int *out_w, int *out_h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[4]; int W, H, maxval;
    if (fscanf(f, "%3s %d %d %d", magic, &W, &H, &maxval) != 4 ||
        strcmp(magic, "P6") != 0 || W <= 0 || H <= 0 || maxval <= 0) {
        fclose(f); return NULL;
    }
    /* Ein weiteres Leerzeichen/Newline nach dem Header */
    fgetc(f);
    size_t npx = (size_t)W * H;
    unsigned char *rgb = malloc(npx * 3);
    if (!rgb) { fclose(f); return NULL; }
    if (fread(rgb, 3, npx, f) != npx) { free(rgb); fclose(f); return NULL; }
    fclose(f);

    /* Skalierung berechnen */
    float scaleX = (float)target_w / W;
    float scaleY = (float)target_h / H;
    float scale  = scaleX < scaleY ? scaleX : scaleY;
    if (scale > 1.0f) scale = 1.0f; /* nicht vergroessern */
    int sw = (int)(W * scale);
    int sh = (int)(H * scale);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;

    uint32_t *out = malloc((size_t)sw * sh * sizeof(uint32_t));
    if (!out) { free(rgb); return NULL; }
    for (int y = 0; y < sh; y++) {
        int src_y = (int)(y / scale);
        if (src_y >= H) src_y = H - 1;
        for (int x = 0; x < sw; x++) {
            int src_x = (int)(x / scale);
            if (src_x >= W) src_x = W - 1;
            int idx = (src_y * W + src_x) * 3;
            out[y * sw + x] = ((uint32_t)rgb[idx] << 16)
                             | ((uint32_t)rgb[idx+1] << 8)
                             |  (uint32_t)rgb[idx+2];
        }
    }
    free(rgb);
    *out_w = sw;
    *out_h = sh;
    return out;
}

/* Dekodiert eine P6-.ppm und erzeugt eine quadratische, center-gecroppte
 * Thumbnail-Kachel von genau `tile`x`tile` Pixeln (RGB32). Im Gegensatz zu
 * load_ppm_scaled wird hier auf FUELLEN skaliert (kurze Seite passt) und
 * mittig zugeschnitten -- so wie iOS-Foto-Kacheln. Gibt malloc'd Puffer
 * oder NULL (z.B. .jpg/.png -> ehrliche Platzhalter-Kachel in der UI). */
static uint32_t *load_ppm_thumb(const char *path, int tile) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[4]; int W, H, maxval;
    if (fscanf(f, "%3s %d %d %d", magic, &W, &H, &maxval) != 4 ||
        strcmp(magic, "P6") != 0 || W <= 0 || H <= 0 || maxval <= 0) {
        fclose(f); return NULL;
    }
    fgetc(f);
    size_t npx = (size_t)W * H;
    unsigned char *rgb = malloc(npx * 3);
    if (!rgb) { fclose(f); return NULL; }
    if (fread(rgb, 3, npx, f) != npx) { free(rgb); fclose(f); return NULL; }
    fclose(f);

    uint32_t *out = malloc((size_t)tile * tile * sizeof(uint32_t));
    if (!out) { free(rgb); return NULL; }

    /* Fuell-Skalierung: scale = tile / min(W,H); center-crop. */
    float scale = (W < H) ? (float)tile / W : (float)tile / H;
    int crop_w = (int)(tile / scale);   /* Quellbreite, die abgebildet wird */
    int crop_h = (int)(tile / scale);
    if (crop_w > W) crop_w = W;
    if (crop_h > H) crop_h = H;
    int src_x0 = (W - crop_w) / 2;
    int src_y0 = (H - crop_h) / 2;
    for (int ty = 0; ty < tile; ty++) {
        int sy = src_y0 + (int)(ty / scale);
        if (sy >= H) sy = H - 1;
        for (int tx = 0; tx < tile; tx++) {
            int sx = src_x0 + (int)(tx / scale);
            if (sx >= W) sx = W - 1;
            int idx = (sy * W + sx) * 3;
            out[ty * tile + tx] = ((uint32_t)rgb[idx] << 16)
                                 | ((uint32_t)rgb[idx+1] << 8)
                                 |  (uint32_t)rgb[idx+2];
        }
    }
    free(rgb);
    return out;
}

/* Thumbnail-Provider fuer flux_ui_draw_gallery: liefert die gecachte
 * TILE-Kachel fuer `name` oder dekodiert sie einmalig. Gibt NULL bei
 * nicht dekodierbaren Formaten -> UI zeichnet Platzhalter. */
static const uint32_t *gallery_thumb_provider(const char *name, void *user) {
    (void)user;
    for (int i = 0; i < thumb_cache_n; i++)
        if (strcmp(thumb_cache[i].name, name) == 0)
            return thumb_cache[i].tile;            /* kann NULL sein (Platzhalter gemerkt) */
    char path[640];
    snprintf(path, sizeof(path), "%s/%s", FLUX_PICTURES_DIR, name);
    uint32_t *tile = load_ppm_thumb(path, FLUX_GALLERY_TILE);
    if (thumb_cache_n < GALLERY_MAX) {
        snprintf(thumb_cache[thumb_cache_n].name, sizeof(thumb_cache[0].name), "%s", name);
        thumb_cache[thumb_cache_n].tile = tile;    /* NULL -> Platzhalter, ebenfalls gecacht */
        thumb_cache_n++;
    } else {
        free(tile);                                /* Cache voll: nicht behalten */
    }
    return tile;
}

/* Sammelt suchbaren Geraete-Inhalt fuer die semantische KI-Suche. */
static void collect_search_context(const char *query, char *out, size_t cap) {
    size_t pos = 0;
    pos += snprintf(out + pos, cap - pos,
                    "Suche nach: '%s'\n\nVerfuegbare Daten:\n", query);

    /* Gedaechtnis */
    FILE *mf = fopen("/etc/flux/memory.txt", "r");
    if (mf) {
        pos += snprintf(out + pos, cap - pos, "\n[Gedaechtnis]\n");
        char ln[128]; int cnt = 0;
        while (fgets(ln, sizeof(ln), mf) && cnt < 30 && pos < cap - 200) {
            pos += snprintf(out + pos, cap - pos, "%s", ln);
            cnt++;
        }
        fclose(mf);
    }

    /* Kalender */
    FILE *cf = fopen("/etc/flux/calendar.txt", "r");
    if (cf) {
        pos += snprintf(out + pos, cap - pos, "\n[Kalender]\n");
        char ln[256]; int cnt = 0;
        while (fgets(ln, sizeof(ln), cf) && cnt < 30 && pos < cap - 200) {
            if (ln[0] != '#' && ln[0] != '\n') {
                pos += snprintf(out + pos, cap - pos, "%s", ln);
                cnt++;
            }
        }
        fclose(cf);
    }

    /* Kontakte */
    FILE *kf = fopen("/etc/flux/contacts.txt", "r");
    if (kf) {
        pos += snprintf(out + pos, cap - pos, "\n[Kontakte]\n");
        char ln[256]; int cnt = 0;
        while (fgets(ln, sizeof(ln), kf) && cnt < 30 && pos < cap - 200) {
            if (ln[0] != '#' && ln[0] != '\n') {
                pos += snprintf(out + pos, cap - pos, "%s", ln);
                cnt++;
            }
        }
        fclose(kf);
    }

    /* Notizen */
    FILE *nf = fopen("/etc/flux/notes.txt", "r");
    if (nf) {
        pos += snprintf(out + pos, cap - pos, "\n[Notizen]\n");
        char ln[256]; int cnt = 0;
        while (fgets(ln, sizeof(ln), nf) && cnt < 20 && pos < cap - 200) {
            pos += snprintf(out + pos, cap - pos, "%s", ln);
            cnt++;
        }
        fclose(nf);
    }

    out[cap-1] = '\0';
}

/* Parst KI-Suchantwort in Array von Ergebnis-Strings. */
static int parse_search_results(const char *answer) {
    search_n = 0;
    if (!answer || !*answer) return 0;
    char tmp[FLUX_MAX_RESPONSE];
    strncpy(tmp, answer, sizeof(tmp) - 1);
    tmp[sizeof(tmp)-1] = '\0';
    char *ptr = tmp;
    char *line;
    while ((line = strsep(&ptr, "\n")) != NULL && search_n < SRCH_MAX) {
        while (*line == ' ' || *line == '-') line++;
        if (!*line || *line == '\r') continue;
        snprintf(search_results_buf[search_n], sizeof(search_results_buf[0]),
                 "%.126s", line);
        search_results_p[search_n] = search_results_buf[search_n];
        search_n++;
    }
    return search_n;
}

/* Zeichnet den aktuellen Screen neu (benoetigt fuer Overlay-Hintergrund). */
static void redraw_current_screen(flux_fb_t *fb, flux_screen_t screen,
                                   const char *last_q, const char *input_buf,
                                   const char *answer_buf) {
    switch (screen) {
        case FLUX_SCREEN_ASSISTANT:
            flux_ui_draw_assistant(fb, last_q, input_buf, answer_buf, 0); break;
        case FLUX_SCREEN_SETTINGS:
            flux_ui_draw_settings(fb, setting_labels, setting_values, FLUX_SETTINGS_N); break;
        case FLUX_SCREEN_WIFI:
            flux_ui_draw_wifi(fb, wifi_current, wifi_names_p, wifi_metas_p,
                              wifi_n, 0, !flux_wifi_available()); break;
        case FLUX_SCREEN_FILES:
            flux_ui_draw_files(fb, files_path, file_names, file_metas,
                               file_n, file_truncated, file_selected); break;
        case FLUX_SCREEN_FILE_VIEWER:
            flux_ui_draw_file_viewer(fb, viewer_path, viewer_content, viewer_scroll); break;
        case FLUX_SCREEN_CALENDAR:
            flux_ui_draw_calendar(fb, cal_year, cal_month, cal_today_view(),
                                   cal_selected_day, cal_event_strs, cal_n_events); break;
        case FLUX_SCREEN_CONTACTS:
            flux_ui_draw_contacts(fb, contact_names_p, contact_details_p,
                                   contact_n, contact_selected); break;
        case FLUX_SCREEN_GALLERY:
            flux_ui_draw_gallery(fb, gallery_names, gallery_dates,
                                  gallery_n, gallery_selected, gallery_scroll,
                                  gallery_filter, gallery_select_mode,
                                  gallery_thumb_provider, NULL); break;
        case FLUX_SCREEN_IMAGE_VIEWER:
            flux_ui_draw_image_viewer(fb, image_path, image_pixels,
                                       image_w, image_h, image_caption, 0); break;
        case FLUX_SCREEN_MEMORY:
            flux_ui_draw_memory(fb, memory_entries, memory_n, memory_scroll); break;
        case FLUX_SCREEN_MEETING: {
            int elapsed = meeting_recording ? (int)(time(NULL) - meeting_start_t) : 0;
            flux_ui_draw_meeting(fb, meeting_recording, elapsed,
                                 meeting_transcript, meeting_status);
            break;
        }
        case FLUX_SCREEN_SEARCH:
            flux_ui_draw_search(fb, search_query, search_results_p,
                                search_n, search_searching); break;
        case FLUX_SCREEN_JOURNAL:
            flux_ui_draw_journal(fb, journal_names_p, journal_n, journal_scroll, -1); break;
        case FLUX_SCREEN_VOICE_ENROLL:
            flux_ui_draw_voice_enroll(fb, voice_enroll_phase, voice_enroll_msg); break;
        case FLUX_SCREEN_VOICE_VERIFY:
            flux_ui_draw_voice_verify(fb, voice_verify_phase, voice_verify_msg); break;
        case FLUX_SCREEN_ALARM:
            flux_ui_draw_alarm(fb, alarm_label); break;
        case FLUX_SCREEN_CALL:
            flux_ui_draw_call(fb, call_name, call_number, call_connected,
                              call_connected ? (int)(time(NULL) - call_start_t) : 0); break;
        case FLUX_SCREEN_HABITS:
            flux_ui_draw_habits(fb, habits_p, habits_n, habits_scroll); break;
        default: break;
    }
}

/* Befuellt den KI-Overlay mit Kontext des aktuellen Screens. */
static void build_ai_overlay_context(flux_screen_t screen,
                                      const char *last_q, const char *answer_buf) {
    static const char *mnames[] = {
        "", "Januar","Februar","Maerz","April","Mai","Juni",
        "Juli","August","September","Oktober","November","Dezember"
    };
    ai_ovl_label[0] = ai_ovl_ctx[0] = ai_ovl_save_path[0] = '\0';

    switch (screen) {
        case FLUX_SCREEN_FILE_VIEWER: {
            const char *fname = strrchr(viewer_path, '/');
            fname = fname ? fname + 1 : viewer_path;
            snprintf(ai_ovl_label, sizeof(ai_ovl_label), "Datei: %.50s", fname);
            snprintf(ai_ovl_ctx, sizeof(ai_ovl_ctx),
                     "Du hilfst dem Nutzer mit der Datei '%s'. "
                     "Inhalt (ggf. gekuerzt):\n%.6000s",
                     viewer_path, viewer_content);
            /* Speicherpfad: gleiche Datei + _Zusammenfassung.txt */
            char base[256]; snprintf(base, sizeof(base), "%s", viewer_path);
            char *dot = strrchr(base, '.'); if (dot) *dot = '\0';
            snprintf(ai_ovl_save_path, sizeof(ai_ovl_save_path),
                     "%s_KI-Zusammenfassung.txt", base);
            break;
        }
        case FLUX_SCREEN_IMAGE_VIEWER: {
            const char *fname = strrchr(image_path, '/');
            fname = fname ? fname + 1 : image_path;
            snprintf(ai_ovl_label, sizeof(ai_ovl_label), "Foto: %.50s", fname);
            snprintf(ai_ovl_ctx, sizeof(ai_ovl_ctx),
                     "Du hilfst dem Nutzer mit dem Foto '%s'. "
                     "KI-Bildbeschreibung: %s",
                     image_path,
                     image_caption[0] ? image_caption : "(noch nicht analysiert -- frage per image_analyze-Tool)");
            snprintf(ai_ovl_save_path, sizeof(ai_ovl_save_path),
                     "%s.beschreibung.txt", image_path);
            break;
        }
        case FLUX_SCREEN_CALENDAR: {
            const char *mn = (cal_month >= 1 && cal_month <= 12) ? mnames[cal_month] : "";
            snprintf(ai_ovl_label, sizeof(ai_ovl_label), "Kalender: %s %04d", mn, cal_year);
            snprintf(ai_ovl_ctx, sizeof(ai_ovl_ctx),
                     "Der Nutzer betrachtet den Kalender: %s %04d. "
                     "Ausgewaehlter Tag: %d.",
                     mn, cal_year, cal_selected_day);
            snprintf(ai_ovl_save_path, sizeof(ai_ovl_save_path),
                     "/home/user/Kalender_%04d-%02d_KI.txt", cal_year, cal_month);
            break;
        }
        case FLUX_SCREEN_CONTACTS:
            snprintf(ai_ovl_label, sizeof(ai_ovl_label),
                     "Kontakte (%d Eintraege)", contact_n);
            snprintf(ai_ovl_ctx, sizeof(ai_ovl_ctx),
                     "Der Nutzer ist in der Kontakte-Liste (%d Kontakte).", contact_n);
            snprintf(ai_ovl_save_path, sizeof(ai_ovl_save_path),
                     "/home/user/Kontakte_Export.txt");
            break;
        case FLUX_SCREEN_GALLERY:
            snprintf(ai_ovl_label, sizeof(ai_ovl_label),
                     "Fotogalerie (%d Fotos)", gallery_n);
            snprintf(ai_ovl_ctx, sizeof(ai_ovl_ctx),
                     "Der Nutzer ist in der Fotogalerie (%d Fotos).", gallery_n);
            snprintf(ai_ovl_save_path, sizeof(ai_ovl_save_path),
                     "/home/user/Galerie_KI.txt");
            break;
        case FLUX_SCREEN_FILES:
            snprintf(ai_ovl_label, sizeof(ai_ovl_label),
                     "Dateien: %.40s", files_path);
            snprintf(ai_ovl_ctx, sizeof(ai_ovl_ctx),
                     "Der Nutzer ist im Datei-Browser: %s (%d Eintraege).",
                     files_path, file_n);
            snprintf(ai_ovl_save_path, sizeof(ai_ovl_save_path),
                     "/home/user/Dateiliste_KI.txt");
            break;
        case FLUX_SCREEN_SETTINGS:
            snprintf(ai_ovl_label, sizeof(ai_ovl_label), "Einstellungen");
            snprintf(ai_ovl_ctx, sizeof(ai_ovl_ctx),
                     "Der Nutzer ist in den Systemeinstellungen von Flux.");
            snprintf(ai_ovl_save_path, sizeof(ai_ovl_save_path),
                     "/home/user/Einstellungen_KI.txt");
            break;
        case FLUX_SCREEN_MEMORY:
            snprintf(ai_ovl_label, sizeof(ai_ovl_label), "KI-Gedaechtnis");
            snprintf(ai_ovl_ctx, sizeof(ai_ovl_ctx),
                     "Der Nutzer betrachtet sein KI-Gedaechtnis (%d Eintraege). "
                     "Er kann dich fragen was gespeichert ist oder dich bitten etwas zu aendern.",
                     memory_n);
            snprintf(ai_ovl_save_path, sizeof(ai_ovl_save_path),
                     "/home/user/Gedaechtnis_Export.txt");
            break;
        default: /* ASSISTANT + alle anderen */ {
            snprintf(ai_ovl_label, sizeof(ai_ovl_label), "KI-Assistent");
            /* Load today's conversation transcript for richer context */
            char tfile[256]; time_t _tt = time(NULL); struct tm _ttm; localtime_r(&_tt, &_ttm);
            strftime(tfile, sizeof(tfile), "/home/user/Journal/Gespraeche_%Y-%m-%d.md", &_ttm);
            char transcript[4096] = {0};
            FILE *_tf = fopen(tfile, "r");
            if (_tf) { size_t _n = fread(transcript, 1, sizeof(transcript)-1, _tf); transcript[_n] = '\0'; fclose(_tf); }
            if (transcript[0])
                snprintf(ai_ovl_ctx, sizeof(ai_ovl_ctx),
                         "Heutiges Gespraechs-Transkript:\n%.3000s", transcript);
            else if (last_q && last_q[0])
                snprintf(ai_ovl_ctx, sizeof(ai_ovl_ctx),
                         "Letztes Gespraech -- Frage: '%s' Antwort: '%.500s'",
                         last_q, answer_buf ? answer_buf : "");
            snprintf(ai_ovl_save_path, sizeof(ai_ovl_save_path),
                     "/home/user/KI-Antwort.txt");
            break;
        }
    }
}

/* Laedt ein Foto in den Bild-Betrachter. */
static void open_image(flux_fb_t *fb, const char *name) {
    snprintf(image_path, sizeof(image_path), "%s/%s", FLUX_PICTURES_DIR, name);
    image_caption[0] = '\0';
    image_analyzing  = 0;
    free(image_pixels);
    /* Bildbereich: Bildschirmbreite x ~450px */
    int target_h = fb->height - 40 - 48 - 72 - 56; /* statusbar+header+caption+buttons */
    if (target_h < 100) target_h = 100;
    image_pixels = load_ppm_scaled(image_path, fb->width, target_h, &image_w, &image_h);
}

static void format_size(off_t size, char *out, size_t cap) {
    if (size < 1024) snprintf(out, cap, "%lld B", (long long)size);
    else if (size < 1024 * 1024) snprintf(out, cap, "%.1f KB", size / 1024.0);
    else snprintf(out, cap, "%.1f MB", size / (1024.0 * 1024.0));
}

static void load_files(const char *path) {
    file_n = 0;
    file_truncated = 0;
    int has_parent = strcmp(path, "/") != 0;
    int capacity = FLUX_FILES_MAX - (has_parent ? 1 : 0);

    struct dirent **entries;
    int total = scandir(path, &entries, NULL, alphasort);
    if (total < 0) total = 0;

    if (has_parent) {
        snprintf(file_names_buf[file_n], sizeof(file_names_buf[0]), "..");
        snprintf(file_metas_buf[file_n], sizeof(file_metas_buf[0]), "Ordner");
        file_is_dir[file_n] = 1;
        file_n++;
    }

    int real_count = 0;
    for (int i = 0; i < total; i++) {
        const char *name = entries[i]->d_name;
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
            if (real_count < capacity) {
                char full[1280];
                snprintf(full, sizeof(full), "%s/%s", path, name);
                struct stat st;
                int is_dir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
                snprintf(file_names_buf[file_n], sizeof(file_names_buf[0]), "%s", name);
                if (is_dir) snprintf(file_metas_buf[file_n], sizeof(file_metas_buf[0]), "Ordner");
                else format_size(st.st_size, file_metas_buf[file_n], sizeof(file_metas_buf[0]));
                file_is_dir[file_n] = is_dir;
                file_n++;
            }
            real_count++;
        }
        free(entries[i]);
    }
    free(entries);
    if (real_count > capacity) file_truncated = 1;

    for (int i = 0; i < file_n; i++) {
        file_names[i] = file_names_buf[i];
        file_metas[i] = file_metas_buf[i];
    }
}

static void files_go_parent(void) {
    char *slash = strrchr(files_path, '/');
    if (slash && slash != files_path) *slash = '\0';
    else { files_path[0] = '/'; files_path[1] = '\0'; }
    file_selected = -1;
}

static void files_enter(const char *name) {
    size_t len = strlen(files_path);
    if (len > 1) snprintf(files_path + len, sizeof(files_path) - len, "/%s", name);
    else snprintf(files_path, sizeof(files_path), "/%s", name);
    file_selected = -1;
}

static void load_file_content(const char *path) {
    snprintf(viewer_path, sizeof(viewer_path), "%s", path);
    viewer_scroll = 0;
    viewer_content[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(viewer_content, sizeof(viewer_content),
                 "(Datei konnte nicht geoeffnet werden)");
        return;
    }
    size_t n = fread(viewer_content, 1, sizeof(viewer_content) - 1, f);
    viewer_content[n] = '\0';
    fclose(f);
    /* Nicht-druckbare Bytes (ausser \n \t) ersetzen */
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)viewer_content[i];
        if (c < 0x20 && c != '\n' && c != '\t')
            viewer_content[i] = '.';
    }
}

int main(void) {
    flux_fb_t fb;
    if (flux_fb_open(&fb, "/dev/fb0") != 0) {
        fprintf(stderr, "flux-shell: /dev/fb0 nicht verfuegbar.\n");
        return 1;
    }

    /* Farbthema vor dem ersten Zeichnen laden */
    apply_theme();
    flux_ui_set_setting_icons(setting_icons);   /* Symbole fuer Einstellungen */
    maybe_generate_greeting();
    /* Kalender auf aktuellen Monat initialisieren */
    {
        time_t _t = time(NULL); struct tm _tm; localtime_r(&_t, &_tm);
        cal_year  = _tm.tm_year + 1900;
        cal_month = _tm.tm_mon + 1;
        cal_today_day = _tm.tm_mday;
        cal_selected_day = _tm.tm_mday;
    }
    image_pixels = NULL; /* explizit NULL stellen fuer free()-Sicherheit */

    flux_input_t in;
    int have_input = (flux_input_open(&in, fb.width, fb.height) == 0);
    if (!have_input)
        fprintf(stderr, "flux-shell: keine Eingabegeraete gefunden, nur Uhr wird angezeigt.\n");

    flux_screen_t screen = FLUX_SCREEN_LOCK;
    time_t last_event_time = time(NULL);
    flux_screen_t pre_notify_screen = FLUX_SCREEN_ASSISTANT; /* Rueckkehr-Ziel fuer Notify */
    char input_buf[256] = {0};
    char answer_buf[FLUX_MAX_RESPONSE] = {0};
    char edit_buf[4096] = {0};
    char last_q[256] = {0};      /* zuletzt gestellte Frage (Nutzer-Blase) */
    char clipboard[4096] = {0};  /* Zwischenablage fuer [C]/[V] in der Eingabeleiste */

    char pin_buf[FLUX_PIN_LEN + 1] = {0};
    int  pin_len = 0;
    int  pin_error = 0;

    flux_action_t pending_action;
    memset(&pending_action, 0, sizeof(pending_action));

    edit_target_t edit_target = EDIT_NONE;
    int edit_setting_index = 0;

    flux_ui_draw_lock(&fb);

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = have_input ? flux_input_add_fds(&in, &rfds) : -1;
        /* Waehrend der Aufnahme schneller ticken (~10 fps) fuer eine fluessige
         * Mikrofon-Animation, sonst 1 s (stromsparend). */
        struct timeval tv = (voice_active ||
                             (screen == FLUX_SCREEN_CALL && call_connected))
                                ? (struct timeval){ 0, 100000 }
                                : (struct timeval){ 1, 0 };
        int ready = (maxfd >= 0) ? select(maxfd + 1, &rfds, NULL, NULL, &tv) : (sleep(1), 0);

        if (ready <= 0) {
            /* Alarm-Klingeldatei pruefen (alle ~30s via 1s-Timeout-Zaehler) */
            {
                static time_t last_alarm_check = 0;
                time_t _now = time(NULL);
                if (screen != FLUX_SCREEN_ALARM && _now - last_alarm_check >= 30) {
                    last_alarm_check = _now;
                    FILE *arf = fopen("/tmp/flux_alarm_ring.txt", "r");
                    if (arf) {
                        alarm_label[0] = '\0';
                        if (fgets(alarm_label, sizeof(alarm_label), arf)) {
                            size_t l = strlen(alarm_label);
                            while (l > 0 && (alarm_label[l-1]=='\n'||alarm_label[l-1]=='\r')) alarm_label[--l]='\0';
                        }
                        fclose(arf);
                        remove("/tmp/flux_alarm_ring.txt");
                        if (alarm_label[0]) {
                            screen = FLUX_SCREEN_ALARM;
                            flux_ui_draw_alarm(&fb, alarm_label);
                            continue;
                        }
                    }
                }
            }
            /* Kein Input -- Uhr auf dem Lockscreen, Auto-Sperre pruefen. */
            if (screen == FLUX_SCREEN_LOCK) {
                flux_ui_draw_lock(&fb);
            } else if (screen == FLUX_SCREEN_CALL && call_connected) {
                /* Laufendes Gespraech: Timer + Rec-Punkt aktualisieren,
                 * Auto-Sperre ausgesetzt. */
                flux_ui_draw_call(&fb, call_name, call_number, call_connected,
                                  (int)(time(NULL) - call_start_t));
            } else {
                char auto_lock_s[16] = {0};
                flux_config_get("auto_lock", auto_lock_s, sizeof(auto_lock_s));
                int timeout = atoi(auto_lock_s);
                if (timeout > 0 && time(NULL) - last_event_time >= (time_t)timeout) {
                    if (voice_active) { flux_voice_cancel(); voice_active = 0; }
                    screen = FLUX_SCREEN_LOCK;
                    flux_ui_draw_lock(&fb);
                }
            }
            /* Spracheingabe: animierten Aufnahme-Indikator weiterzeichnen */
            if (voice_active && screen == FLUX_SCREEN_ASSISTANT) {
                int elapsed = (int)(time(NULL) - voice_start_t);
                flux_ui_draw_voice_overlay(&fb, elapsed, voice_frame++);
            }
            /* Zombie-Kinder (TTS-Prozesse) aufraumen */
            while (waitpid(-1, NULL, WNOHANG) > 0) {}
            continue;
        }
        /* Jedes verarbeitete Event setzt den Inaktivitaets-Timer zurueck */
        last_event_time = time(NULL);

        flux_event_t ev = flux_input_poll(&in);
        if (ev.type == FLUX_EV_NONE) continue;

        /* ---- Globaler KI-Overlay (Wisch nach rechts) ----------------
         * Funktioniert auf ALLEN Screens ausser Lock/PIN.
         * Overlay abfangen bevor irgendein Screen-Handler greift. */
        if (screen != FLUX_SCREEN_LOCK && screen != FLUX_SCREEN_PIN) {

            /* Wisch nach rechts oeffnet den Overlay */
            if (ev.type == FLUX_EV_SWIPE_RIGHT && !ai_ovl_active) {
                ai_ovl_active = 1;
                ai_ovl_input[0] = ai_ovl_result[0] = '\0';
                build_ai_overlay_context(screen, last_q, answer_buf);
                redraw_current_screen(&fb, screen, last_q, input_buf, answer_buf);
                flux_ui_draw_ai_overlay(&fb, ai_ovl_label, ai_ovl_input, ai_ovl_result);
                continue;
            }

            /* Solange Overlay aktiv: alle Events abfangen */
            if (ai_ovl_active) {
                if (ev.type == FLUX_EV_TAP) {
                    int ovl_cancel = 0, ovl_submit = 0, ovl_save = 0;
                    flux_ui_ai_overlay_hit(&fb, ev.x, ev.y,
                                           &ovl_cancel, &ovl_submit, &ovl_save);
                    if (ovl_cancel) {
                        ai_ovl_active = 0;
                        ai_ovl_input[0] = ai_ovl_result[0] = '\0';
                        redraw_current_screen(&fb, screen, last_q, input_buf, answer_buf);
                    } else if (ovl_submit && ai_ovl_input[0]) {
                        /* KI anfragen mit Screen-Kontext */
                        char q_full[8960];
                        if (ai_ovl_ctx[0])
                            snprintf(q_full, sizeof(q_full),
                                     "[Systemkontext: %s]\n\nNutzerfrage: %s",
                                     ai_ovl_ctx, ai_ovl_input);
                        else
                            snprintf(q_full, sizeof(q_full), "%s", ai_ovl_input);
                        ai_ovl_result[0] = '\0';
                        redraw_current_screen(&fb, screen, last_q, input_buf, answer_buf);
                        flux_ui_draw_ai_overlay(&fb, ai_ovl_label, ai_ovl_input,
                                                "KI denkt nach ...");
                        flux_ipc_ask(q_full, ai_ovl_result, sizeof(ai_ovl_result));
                        redraw_current_screen(&fb, screen, last_q, input_buf, answer_buf);
                        flux_ui_draw_ai_overlay(&fb, ai_ovl_label, ai_ovl_input, ai_ovl_result);
                    } else if (ovl_save && ai_ovl_result[0]) {
                        /* Ergebnis als .txt speichern */
                        FILE *sf = fopen(ai_ovl_save_path, "w");
                        if (sf) {
                            fprintf(sf, "KI-Antwort zu: %s\nFrage: %s\n\n%s\n",
                                    ai_ovl_label, ai_ovl_input, ai_ovl_result);
                            fclose(sf);
                        }
                        /* kurzes Feedback */
                        char saved_msg[320];
                        snprintf(saved_msg, sizeof(saved_msg),
                                 "Gespeichert: %s", ai_ovl_save_path);
                        /* Ergebnis-Text kurz ersetzen */
                        snprintf(ai_ovl_result, sizeof(ai_ovl_result),
                                 "[Gespeichert unter %s]", ai_ovl_save_path);
                        redraw_current_screen(&fb, screen, last_q, input_buf, answer_buf);
                        flux_ui_draw_ai_overlay(&fb, ai_ovl_label, ai_ovl_input, ai_ovl_result);
                    }
                } else if (ev.type == FLUX_EV_CHAR) {
                    size_t len = strlen(ai_ovl_input);
                    if (len + 1 < sizeof(ai_ovl_input)) {
                        ai_ovl_input[len] = ev.ch;
                        ai_ovl_input[len + 1] = '\0';
                    }
                    redraw_current_screen(&fb, screen, last_q, input_buf, answer_buf);
                    flux_ui_draw_ai_overlay(&fb, ai_ovl_label, ai_ovl_input, ai_ovl_result);
                } else if (ev.type == FLUX_EV_BACKSPACE) {
                    size_t len = strlen(ai_ovl_input);
                    if (len > 0) ai_ovl_input[len - 1] = '\0';
                    redraw_current_screen(&fb, screen, last_q, input_buf, answer_buf);
                    flux_ui_draw_ai_overlay(&fb, ai_ovl_label, ai_ovl_input, ai_ovl_result);
                } else if (ev.type == FLUX_EV_ENTER && ai_ovl_input[0]) {
                    /* Enter = Fragen absenden */
                    char q_full[8960];
                    if (ai_ovl_ctx[0])
                        snprintf(q_full, sizeof(q_full),
                                 "[Systemkontext: %s]\n\nNutzerfrage: %s",
                                 ai_ovl_ctx, ai_ovl_input);
                    else
                        snprintf(q_full, sizeof(q_full), "%s", ai_ovl_input);
                    ai_ovl_result[0] = '\0';
                    redraw_current_screen(&fb, screen, last_q, input_buf, answer_buf);
                    flux_ui_draw_ai_overlay(&fb, ai_ovl_label, ai_ovl_input,
                                            "KI denkt nach ...");
                    flux_ipc_ask(q_full, ai_ovl_result, sizeof(ai_ovl_result));
                    redraw_current_screen(&fb, screen, last_q, input_buf, answer_buf);
                    flux_ui_draw_ai_overlay(&fb, ai_ovl_label, ai_ovl_input, ai_ovl_result);
                } else if (ev.type == FLUX_EV_SWIPE_RIGHT || ev.type == FLUX_EV_SWIPE_LEFT) {
                    /* Nochmal wischen schliesst den Overlay */
                    ai_ovl_active = 0;
                    ai_ovl_input[0] = ai_ovl_result[0] = '\0';
                    redraw_current_screen(&fb, screen, last_q, input_buf, answer_buf);
                }
                continue; /* Overlay schluckt ALLE Events */
            }
        }
        /* ---- Ende KI-Overlay ---------------------------------------- */

        if (screen == FLUX_SCREEN_LOCK) {
            if (ev.type == FLUX_EV_ENTER || ev.type == FLUX_EV_SWIPE_UP) {
                char stored[128] = {0};
                if (flux_config_get("pin_hash", stored, sizeof(stored)) && stored[0]) {
                    screen = FLUX_SCREEN_PIN;
                    pin_len = 0;
                    pin_error = 0;
                    uint32_t *old = capture_frame(&fb);
                    flux_ui_draw_pin(&fb, pin_len, pin_error);
                    animate_slide_in(&fb, old);
                    free(old);
                } else {
                    screen = FLUX_SCREEN_ASSISTANT;
                    input_buf[0] = '\0';
                    answer_buf[0] = '\0';
                    flux_ui_set_quick_reveal(0, 0);     /* Knoepfe erst verbergen */
                    uint32_t *old = capture_frame(&fb);
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                    animate_slide_in(&fb, old);
                    free(old);
                    animate_home_intro(&fb, &in, last_q, input_buf, answer_buf);
                }
            }
            continue;
        }

        if (screen == FLUX_SCREEN_PIN) {
            char digit = 0;
            int backspace = 0;
            int hit = 0;
            if (ev.type == FLUX_EV_TAP) {
                hit = flux_ui_pin_hit(&fb, ev.x, ev.y, &digit, &backspace);
            } else if (ev.type == FLUX_EV_CHAR && ev.ch >= '0' && ev.ch <= '9') {
                digit = ev.ch;
                hit = 1;
            } else if (ev.type == FLUX_EV_BACKSPACE) {
                backspace = 1;
                hit = 1;
            }
            if (!hit) continue;

            pin_error = 0;
            if (backspace) {
                if (pin_len > 0) pin_len--;
            } else if (digit && pin_len < FLUX_PIN_LEN) {
                pin_buf[pin_len++] = digit;
            }

            if (pin_len == FLUX_PIN_LEN) {
                pin_buf[pin_len] = '\0';
                char hash[65];
                flux_sha256_hex(pin_buf, hash);
                char stored[128] = {0};
                flux_config_get("pin_hash", stored, sizeof(stored));
                pin_len = 0;
                pin_buf[0] = '\0';
                if (strcmp(hash, stored) == 0) {
                    /* PIN korrekt -- zweiten Faktor pruefen (falls eingerichtet) */
                    if (voice_unlock_enrolled()) {
                        voice_verify_phase = 0;
                        voice_verify_msg[0] = '\0';
                        uint32_t *old = capture_frame(&fb);
                        screen = FLUX_SCREEN_VOICE_VERIFY;
                        flux_ui_draw_voice_verify(&fb, voice_verify_phase, voice_verify_msg);
                        animate_slide_in(&fb, old);
                        free(old);
                    } else {
                        screen = FLUX_SCREEN_ASSISTANT;
                        input_buf[0] = '\0';
                        answer_buf[0] = '\0';
                        flux_ui_set_quick_reveal(0, 0);
                        uint32_t *old = capture_frame(&fb);
                        flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                        animate_slide_in(&fb, old);
                        free(old);
                        animate_home_intro(&fb, &in, last_q, input_buf, answer_buf);
                    }
                } else {
                    pin_error = 1;
                    flux_ui_draw_pin(&fb, pin_len, pin_error);
                }
            } else {
                flux_ui_draw_pin(&fb, pin_len, pin_error);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_CONFIRM) {
            if (ev.type != FLUX_EV_TAP) continue;
            flux_confirm_hit_t hit = flux_ui_confirm_hit(&fb, ev.x, ev.y,
                                                         pending_action.subject[0] != 0);
            if (hit == FLUX_CONFIRM_CANCEL) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                snprintf(answer_buf, sizeof(answer_buf), "Abgebrochen.");
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_in(&fb, old);
                free(old);
            } else if (hit == FLUX_CONFIRM_EDIT_TO ||
                       hit == FLUX_CONFIRM_EDIT_SUBJECT ||
                       hit == FLUX_CONFIRM_EDIT_BODY) {
                /* Direkt die angetippte Zeile bearbeiten -- kein extra Knopf */
                if (hit == FLUX_CONFIRM_EDIT_TO) {
                    snprintf(edit_buf, sizeof(edit_buf), "%s", pending_action.to);
                    edit_target = EDIT_ACTION_TO;
                    flux_ui_set_edit_title("Empfaenger");
                } else if (hit == FLUX_CONFIRM_EDIT_SUBJECT) {
                    snprintf(edit_buf, sizeof(edit_buf), "%s", pending_action.subject);
                    edit_target = EDIT_ACTION_SUBJECT;
                    flux_ui_set_edit_title("Betreff");
                } else {
                    snprintf(edit_buf, sizeof(edit_buf), "%s", pending_action.body);
                    edit_target = EDIT_ACTION_BODY;
                    flux_ui_set_edit_title("Nachricht");
                }
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_EDIT_BODY;
                flux_ui_draw_edit_body(&fb, edit_buf);
                animate_slide_in(&fb, old);
                free(old);
            } else if (hit == FLUX_CONFIRM_SEND) {
                /* Anruf: Vollbild-Anruf-Screen (annehmen/auflegen) statt
                 * Textmeldung. Der eigentliche X:-Request geht erst beim
                 * Tippen auf "Annehmen" raus (siehe FLUX_SCREEN_CALL). */
                if (pending_action.type == FLUX_ACTION_CALL) {
                    snprintf(call_name, sizeof(call_name), "%.90s",
                             pending_action.to[0] ? pending_action.to : "Unbekannt");
                    call_number[0] = '\0';
                    call_connected = 0;
                    call_transcript[0] = '\0';
                    call_tel_note[0] = '\0';
                    screen = FLUX_SCREEN_CALL;
                    flux_ui_draw_call(&fb, call_name, call_number, call_connected, 0);
                    continue;
                }
                /* Mail/SMS: animiertes Symbol (Papierflieger / Sprechblase). */
                flux_anim_kind_t ak =
                    pending_action.type == FLUX_ACTION_SMS  ? FLUX_ANIM_SMS  :
                                                              FLUX_ANIM_MAIL;
                char cap[80];
                snprintf(cap, sizeof(cap), "%.60s", pending_action.to);
                animate_action(&fb, ak, cap, 18, 60000);

                char req[FLUX_MAX_LINE];
                flux_action_build_request(&pending_action, req, sizeof(req));
                flux_ipc_send_raw(req, answer_buf, sizeof(answer_buf));

                /* Ergebnis-Symbol: Haken bei klarem Erfolg, X bei Fehler. */
                int ok  = strstr(answer_buf, "gesendet") || strstr(answer_buf, "Verbunden");
                int err = strstr(answer_buf, "fehlgeschlagen") || strstr(answer_buf, "Fehler");
                if (ok)       animate_action(&fb, FLUX_ANIM_OK,   NULL, 10, 45000);
                else if (err) animate_action(&fb, FLUX_ANIM_FAIL, NULL,  8, 60000);

                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_EDIT_BODY) {
            flux_event_type_t kind = ev.type;
            char ch = ev.ch;

            if (kind == FLUX_EV_TAP) {
                /* OK-Knopf (= MIC-Position auf diesem Bildschirm) -> Speichern */
                if (flux_ui_mic_hit(&fb, ev.x, ev.y)) {
                    kind = FLUX_EV_ENTER;
                    goto edit_body_enter;
                }
                /* Kopieren */
                if (flux_ui_copy_hit(&fb, ev.x, ev.y)) {
                    if (edit_buf[0]) snprintf(clipboard, sizeof(clipboard), "%s", edit_buf);
                    continue;
                }
                /* Einfuegen */
                if (flux_ui_paste_hit(&fb, ev.x, ev.y)) {
                    if (clipboard[0]) {
                        size_t clen = strlen(clipboard);
                        size_t elen = strlen(edit_buf);
                        size_t avail = sizeof(edit_buf) - elen - 1;
                        size_t copy = clen < avail ? clen : avail;
                        strncat(edit_buf, clipboard, copy);
                        flux_ui_draw_edit_body(&fb, edit_buf);
                    }
                    continue;
                }
                char tap_ch = 0;
                int tap_backspace = 0, tap_enter = 0;
                if (!flux_ui_kbd_hit(&fb, ev.x, ev.y, &tap_ch, &tap_backspace, &tap_enter))
                    continue;
                if (tap_backspace) kind = FLUX_EV_BACKSPACE;
                else if (tap_enter) kind = FLUX_EV_ENTER;
                else { kind = FLUX_EV_CHAR; ch = tap_ch; }
            } else if (kind == FLUX_EV_SWIPE_UP) {
                continue;
            }

            if (kind == FLUX_EV_CHAR) {
                size_t len = strlen(edit_buf);
                if (len + 1 < sizeof(edit_buf)) {
                    edit_buf[len] = ch;
                    edit_buf[len + 1] = '\0';
                }
                flux_ui_draw_edit_body(&fb, edit_buf);
            } else if (kind == FLUX_EV_BACKSPACE) {
                size_t len = strlen(edit_buf);
                if (len > 0) edit_buf[len - 1] = '\0';
                flux_ui_draw_edit_body(&fb, edit_buf);
            } else if (kind == FLUX_EV_ENTER) {
                edit_body_enter:
                if (edit_target == EDIT_ACTION_BODY ||
                    edit_target == EDIT_ACTION_TO ||
                    edit_target == EDIT_ACTION_SUBJECT) {
                    if (edit_target == EDIT_ACTION_TO)
                        snprintf(pending_action.to, sizeof(pending_action.to), "%s", edit_buf);
                    else if (edit_target == EDIT_ACTION_SUBJECT)
                        snprintf(pending_action.subject, sizeof(pending_action.subject), "%s", edit_buf);
                    else
                        snprintf(pending_action.body, sizeof(pending_action.body), "%s", edit_buf);
                    flux_ui_set_edit_title(NULL);
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_CONFIRM;
                    flux_ui_draw_confirm(&fb, flux_action_type_label(pending_action.type),
                                          pending_action.to, pending_action.subject, pending_action.body);
                    animate_slide_in(&fb, old);
                    free(old);
                } else if (edit_target == EDIT_NEW_FOLDER) {
                    if (edit_buf[0]) {
                        char new_dir[1280];
                        size_t plen = strlen(files_path);
                        if (plen > 1)
                            snprintf(new_dir, sizeof(new_dir), "%s/%s", files_path, edit_buf);
                        else
                            snprintf(new_dir, sizeof(new_dir), "/%s", edit_buf);
                        mkdir(new_dir, 0755);
                    }
                    file_selected = -1;
                    load_files(files_path);
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_FILES;
                    flux_ui_draw_files(&fb, files_path, file_names, file_metas,
                                       file_n, file_truncated, file_selected);
                    animate_slide_in(&fb, old);
                    free(old);
                } else if (edit_target == EDIT_EMAIL_ADDR) {
                    /* Adresse merken, dann Schritt 2: App-Passwort abfragen */
                    snprintf(email_pending, sizeof(email_pending), "%s", edit_buf);
                    if (!email_pending[0]) {
                        /* leer -> abbrechen, zurueck zu den Einstellungen */
                        flux_ui_set_edit_title(NULL);
                        load_settings_values();
                        screen = FLUX_SCREEN_SETTINGS;
                        flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
                    } else {
                        edit_target = EDIT_EMAIL_PASS;
                        edit_buf[0] = '\0';
                        flux_ui_set_edit_title("App-Passwort");
                        flux_ui_draw_edit_body(&fb, edit_buf);
                    }
                } else if (edit_target == EDIT_EMAIL_PASS) {
                    /* Konto ableiten + speichern (SMTP/IMAP automatisch) */
                    if (edit_buf[0]) email_account_apply(email_pending, edit_buf);
                    email_pending[0] = '\0';
                    flux_ui_set_edit_title(NULL);
                    load_settings_values();
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_SETTINGS;
                    flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
                    animate_slide_in(&fb, old);
                    free(old);
                } else if (edit_target == EDIT_WIFI_PASS) {
                    /* WLAN verbinden mit gewaehltem Netz + eingegebenem Passwort */
                    char msg[160];
                    flux_wifi_connect(wifi_sel_ssid, edit_buf, msg, sizeof(msg));
                    flux_ui_set_edit_title(NULL);
                    wifi_rescan(&fb);
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_WIFI;
                    flux_ui_draw_wifi(&fb, wifi_current, wifi_names_p, wifi_metas_p,
                                      wifi_n, 0, !flux_wifi_available());
                    animate_slide_in(&fb, old);
                    free(old);
                } else {
                    apply_setting_edit(edit_setting_index, edit_buf);
                    flux_ui_set_edit_title(NULL);
                    load_settings_values();
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_SETTINGS;
                    flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
                    animate_slide_in(&fb, old);
                    free(old);
                }
            }
            continue;
        }

        if (screen == FLUX_SCREEN_SETTINGS) {
            if (ev.type == FLUX_EV_SWIPE_LEFT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            if (ev.type != FLUX_EV_TAP) continue;
            int idx, back;
            if (!flux_ui_list_hit(&fb, ev.x, ev.y, FLUX_SETTINGS_N, &idx, &back)) continue;
            if (back) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_in(&fb, old);
                free(old);
            } else if (strcmp(setting_keys[idx], "ai_provider") == 0) {
                /* KI-Anbieter per Tap durchschalten statt Texteingabe */
                char cur[64] = {0};
                flux_config_get("ai_provider", cur, sizeof(cur));
                const char *next = "deepseek";
                if (!strcmp(cur, "deepseek")) next = "nvidia";
                else if (!strcmp(cur, "nvidia")) next = "anthropic";
                else next = "deepseek"; /* von anthropic/leer aus */
                flux_config_set("ai_provider", next);
                load_settings_values();
                flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
            } else if (strcmp(setting_keys[idx], "__wifi") == 0) {
                /* WLAN-Screen oeffnen; wifi_rescan zeigt das animierte Such-Symbol */
                screen = FLUX_SCREEN_WIFI;
                wifi_rescan(&fb);
                flux_ui_draw_wifi(&fb, wifi_current, wifi_names_p, wifi_metas_p,
                                  wifi_n, 0, !flux_wifi_available());
            } else if (strcmp(setting_keys[idx], "__voice_enroll") == 0) {
                /* Stimm-Einlern-Screen oeffnen */
                voice_enroll_phase = 0;
                voice_enroll_msg[0] = '\0';
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_VOICE_ENROLL;
                flux_ui_draw_voice_enroll(&fb, voice_enroll_phase, voice_enroll_msg);
                animate_slide_in(&fb, old);
                free(old);
            } else if (strcmp(setting_keys[idx], "__email") == 0) {
                /* Schritt 1: E-Mail-Adresse (mit aktueller Adresse vorbelegt) */
                edit_target = EDIT_EMAIL_ADDR;
                email_pending[0] = '\0';
                flux_config_get("smtp_from", edit_buf, sizeof(edit_buf));
                flux_ui_set_edit_title("E-Mail-Adresse");
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_EDIT_BODY;
                flux_ui_draw_edit_body(&fb, edit_buf);
                animate_slide_in(&fb, old);
                free(old);
            } else {
                edit_target = EDIT_SETTING_FIELD;
                edit_setting_index = idx;
                if (setting_secret[idx]) edit_buf[0] = '\0';
                else flux_config_get(resolve_setting_key(setting_keys[idx]), edit_buf, sizeof(edit_buf));
                flux_ui_set_edit_title(setting_labels[idx]);
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_EDIT_BODY;
                flux_ui_draw_edit_body(&fb, edit_buf);
                animate_slide_in(&fb, old);
                free(old);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_WIFI) {
            if (ev.type == FLUX_EV_SWIPE_LEFT) {
                uint32_t *old = capture_frame(&fb);
                load_settings_values();
                screen = FLUX_SCREEN_SETTINGS;
                flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            if (ev.type != FLUX_EV_TAP) continue;
            int idx = 0, back = 0;
            if (!flux_ui_list_hit(&fb, ev.x, ev.y, wifi_n, &idx, &back)) continue;
            if (back) {
                if (wifi_n == 0) {
                    /* untere Leiste = "Aktualisieren" bei leerer Liste */
                    wifi_rescan(&fb);
                    flux_ui_draw_wifi(&fb, wifi_current, wifi_names_p, wifi_metas_p,
                                      wifi_n, 0, !flux_wifi_available());
                } else {
                    uint32_t *old = capture_frame(&fb);
                    load_settings_values();
                    screen = FLUX_SCREEN_SETTINGS;
                    flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
                    animate_slide_in(&fb, old);
                    free(old);
                }
            } else if (idx < wifi_n) {
                snprintf(wifi_sel_ssid, sizeof(wifi_sel_ssid), "%s", wifi_nets[idx].ssid);
                wifi_sel_secured = wifi_nets[idx].secured;
                if (wifi_sel_secured) {
                    edit_target = EDIT_WIFI_PASS;
                    edit_buf[0] = '\0';
                    flux_ui_set_edit_title("WLAN-Passwort");
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_EDIT_BODY;
                    flux_ui_draw_edit_body(&fb, edit_buf);
                    animate_slide_in(&fb, old);
                    free(old);
                } else {
                    char msg[160];
                    flux_wifi_connect(wifi_sel_ssid, "", msg, sizeof(msg));
                    wifi_rescan(&fb);
                    flux_ui_draw_wifi(&fb, wifi_current, wifi_names_p, wifi_metas_p,
                                      wifi_n, 0, !flux_wifi_available());
                }
            }
            continue;
        }

        if (screen == FLUX_SCREEN_NOTIFY) {
            /* Jeder Tap oder Wisch schliesst den Overlay */
            if (ev.type == FLUX_EV_TAP || ev.type == FLUX_EV_SWIPE_UP ||
                ev.type == FLUX_EV_SWIPE_DOWN) {
                uint32_t *old = capture_frame(&fb);
                screen = pre_notify_screen;
                if (screen == FLUX_SCREEN_ASSISTANT)
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                else if (screen == FLUX_SCREEN_FILES)
                    flux_ui_draw_files(&fb, files_path, file_names, file_metas,
                                       file_n, file_truncated, file_selected);
                else if (screen == FLUX_SCREEN_CALENDAR)
                    flux_ui_draw_calendar(&fb, cal_year, cal_month, cal_today_view(),
                                          cal_selected_day, cal_event_strs, cal_n_events);
                else if (screen == FLUX_SCREEN_CONTACTS)
                    flux_ui_draw_contacts(&fb, contact_names_p, contact_details_p,
                                          contact_n, contact_selected);
                else if (screen == FLUX_SCREEN_GALLERY)
                    flux_ui_draw_gallery(&fb, gallery_names, gallery_dates,
                                         gallery_n, gallery_selected, gallery_scroll,
                                         gallery_filter, gallery_select_mode,
                                         gallery_thumb_provider, NULL);
                animate_slide_in(&fb, old);
                free(old);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_FILES) {
            if (ev.type == FLUX_EV_SWIPE_LEFT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            /* "Neuer Ordner"-Knopf */
            if (ev.type == FLUX_EV_TAP && flux_ui_files_new_btn_hit(&fb, ev.x, ev.y)) {
                edit_buf[0] = '\0';
                edit_target = EDIT_NEW_FOLDER;
                flux_ui_set_edit_title("Neuer Ordner-Name");
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_EDIT_BODY;
                flux_ui_draw_edit_body(&fb, edit_buf);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }

            if (ev.type != FLUX_EV_TAP) continue;

            /* Loeschen-Knopf (nur sichtbar wenn Datei ausgewaehlt) */
            if (file_selected >= 0 && flux_ui_files_delete_hit(&fb, ev.x, ev.y)) {
                /* Nur Dateien loeschen, nicht Ordner oder ".." */
                if (file_selected < file_n && !file_is_dir[file_selected]) {
                    char full_path[1280];
                    size_t plen = strlen(files_path);
                    if (plen > 1)
                        snprintf(full_path, sizeof(full_path), "%s/%s",
                                 files_path, file_names_buf[file_selected]);
                    else
                        snprintf(full_path, sizeof(full_path), "/%s",
                                 file_names_buf[file_selected]);

                    /* Sicherheit: nur /home/user/ loeschen */
                    if (strncmp(full_path, "/home/user/", 11) == 0)
                        remove(full_path);
                }
                file_selected = -1;
                load_files(files_path);
                flux_ui_draw_files(&fb, files_path, file_names, file_metas,
                                   file_n, file_truncated, file_selected);
                continue;
            }

            int idx, back;
            if (!flux_ui_list_hit(&fb, ev.x, ev.y, file_n, &idx, &back)) continue;
            if (back) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_in(&fb, old);
                free(old);
            } else if (idx < file_n) {
                if (file_is_dir[idx]) {
                    /* Ordner betreten */
                    if (strcmp(file_names_buf[idx], "..") == 0) files_go_parent();
                    else files_enter(file_names_buf[idx]);
                    load_files(files_path);
                    flux_ui_draw_files(&fb, files_path, file_names, file_metas,
                                       file_n, file_truncated, file_selected);
                } else if (idx == file_selected) {
                    /* Zweites Tippen auf dieselbe Datei -> oeffnen */
                    char full_path[1280];
                    size_t plen = strlen(files_path);
                    if (plen > 1)
                        snprintf(full_path, sizeof(full_path), "%s/%s",
                                 files_path, file_names_buf[idx]);
                    else
                        snprintf(full_path, sizeof(full_path), "/%s",
                                 file_names_buf[idx]);
                    load_file_content(full_path);
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_FILE_VIEWER;
                    flux_ui_draw_file_viewer(&fb, viewer_path, viewer_content, viewer_scroll);
                    animate_slide_in(&fb, old);
                    free(old);
                } else {
                    /* Erstes Tippen: Datei markieren */
                    file_selected = idx;
                    flux_ui_draw_files(&fb, files_path, file_names, file_metas,
                                       file_n, file_truncated, file_selected);
                }
            }
            continue;
        }

        if (screen == FLUX_SCREEN_FILE_VIEWER) {
            if (ev.type == FLUX_EV_SWIPE_LEFT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_FILES;
                flux_ui_draw_files(&fb, files_path, file_names, file_metas,
                                   file_n, file_truncated, file_selected);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            if (ev.type != FLUX_EV_TAP && ev.type != FLUX_EV_SWIPE_UP) continue;
            int scroll_delta = 0, back = 0, ai_btn = 0;
            if (ev.type == FLUX_EV_SWIPE_UP) { scroll_delta = 3; }
            else flux_ui_viewer_hit(&fb, ev.x, ev.y, &scroll_delta, &back, &ai_btn);

            if (ai_btn) {
                /* "KI fragen": Dokument-KI-Overlay mit Dateiinhalt als Kontext */
                ai_ovl_active = 1;
                ai_ovl_input[0] = ai_ovl_result[0] = '\0';
                build_ai_overlay_context(FLUX_SCREEN_FILE_VIEWER, last_q, answer_buf);
                flux_ui_draw_ai_overlay(&fb, ai_ovl_label, ai_ovl_input, ai_ovl_result);
                continue;
            }
            if (back) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_FILES;
                flux_ui_draw_files(&fb, files_path, file_names, file_metas,
                                   file_n, file_truncated, file_selected);
                animate_slide_in(&fb, old);
                free(old);
            } else if (scroll_delta) {
                viewer_scroll += scroll_delta;
                if (viewer_scroll < 0) viewer_scroll = 0;
                flux_ui_draw_file_viewer(&fb, viewer_path, viewer_content, viewer_scroll);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_CALENDAR) {
            if (ev.type == FLUX_EV_SWIPE_LEFT || ev.type == FLUX_EV_SWIPE_DOWN) {
                pre_notify_screen = FLUX_SCREEN_CALENDAR;
                if (ev.type == FLUX_EV_SWIPE_DOWN) {
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_NOTIFY;
                    flux_ui_draw_notify(&fb);
                    animate_slide_in(&fb, old);
                    free(old);
                } else {
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_ASSISTANT;
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                    animate_slide_from_left(&fb, old);
                    free(old);
                }
                continue;
            }
            if (ev.type != FLUX_EV_TAP) continue;

            /* Zurueck-Leiste (LIST_BACK_H = 64, definiert in ui.c) */
            if (ev.y >= fb.height - 64) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }

            int hit_cell = 0, prev_m = 0, next_m = 0, today_btn = 0, add_btn = 0;
            if (flux_ui_calendar_hit(&fb, ev.x, ev.y, &hit_cell, &prev_m, &next_m,
                                     &today_btn, &add_btn)) {
                if (prev_m) {
                    if (--cal_month < 1)  { cal_month = 12; cal_year--; }
                    load_cal_events(cal_year, cal_month);
                    cal_selected_day = 0;
                } else if (next_m) {
                    if (++cal_month > 12) { cal_month = 1;  cal_year++; }
                    load_cal_events(cal_year, cal_month);
                    cal_selected_day = 0;
                } else if (today_btn) {
                    /* Zurueck zum aktuellen Monat + heutigen Tag markieren */
                    time_t _t = time(NULL); struct tm _m; localtime_r(&_t, &_m);
                    cal_year  = _m.tm_year + 1900;
                    cal_month = _m.tm_mon + 1;
                    cal_today_day = _m.tm_mday;
                    cal_selected_day = _m.tm_mday;
                    load_cal_events(cal_year, cal_month);
                } else if (add_btn) {
                    /* Ehrlicher Platzhalter: "Termin hinzufuegen" ist noch
                     * nicht implementiert. Kurzes Tipp-Feedback, keine
                     * erfundenen Daten. */
                    animate_ripple(&fb, ev.x, ev.y);
                } else {
                    /* Zellenindex → Tagesnummer */
                    static const int ft[] = {0,3,2,5,0,3,5,1,4,6,2,4};
                    int y2 = cal_year, m2 = cal_month;
                    if (m2 < 3) y2--;
                    int fdow = ((y2 + y2/4 - y2/100 + y2/400 + ft[m2-1] + 1) % 7 + 6) % 7;
                    static const int dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
                    int maxd = dm[cal_month-1];
                    if (cal_month==2 && ((cal_year%4==0&&cal_year%100!=0)||cal_year%400==0)) maxd=29;
                    int actual = hit_cell - fdow + 1;
                    if (actual >= 1 && actual <= maxd) {
                        cal_selected_day = actual;
                        load_cal_events(cal_year, cal_month);
                    }
                }
                flux_ui_draw_calendar(&fb, cal_year, cal_month, cal_today_view(),
                                       cal_selected_day, cal_event_strs, cal_n_events);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_CONTACTS) {
            if (ev.type == FLUX_EV_SWIPE_LEFT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            if (ev.type == FLUX_EV_SWIPE_DOWN) {
                pre_notify_screen = FLUX_SCREEN_CONTACTS;
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_NOTIFY;
                flux_ui_draw_notify(&fb);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (ev.type != FLUX_EV_TAP) continue;
            int idx, back;
            if (!flux_ui_list_hit(&fb, ev.x, ev.y, contact_n, &idx, &back)) continue;
            if (back) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
            } else if (idx < contact_n) {
                contact_selected = (idx == contact_selected) ? -1 : idx;
                flux_ui_draw_contacts(&fb, contact_names_p, contact_details_p,
                                       contact_n, contact_selected);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_GALLERY) {
            if (ev.type == FLUX_EV_SWIPE_LEFT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            /* Vertikales Scrollen des Rasters (Header/Bottom-Leiste fix). */
            if (ev.type == FLUX_EV_SWIPE_UP || ev.type == FLUX_EV_SWIPE_DOWN) {
                int maxs = flux_ui_gallery_max_scroll(&fb, gallery_dates,
                                                      gallery_n, gallery_filter);
                if (ev.type == FLUX_EV_SWIPE_UP) gallery_scroll += 2;
                else gallery_scroll -= 2;
                if (gallery_scroll < 0) gallery_scroll = 0;
                if (gallery_scroll > maxs) gallery_scroll = maxs;
                flux_ui_draw_gallery(&fb, gallery_names, gallery_dates,
                                     gallery_n, gallery_selected, gallery_scroll,
                                     gallery_filter, gallery_select_mode,
                                     gallery_thumb_provider, NULL);
                continue;
            }
            if (ev.type != FLUX_EV_TAP) continue;

            flux_gallery_hit_t gh;
            if (!flux_ui_gallery_hit(&fb, ev.x, ev.y, gallery_dates, gallery_n,
                                     gallery_scroll, gallery_filter, &gh))
                continue;

            if (gh.camera) {
                /* Kamera-Auslöser (kleines Kamera-Icon unten links). */
                animate_ripple(&fb, ev.x, ev.y);
                char photo_path[256];
                time_t _t = time(NULL); struct tm _tm; localtime_r(&_t, &_tm);
                strftime(photo_path, sizeof(photo_path),
                         FLUX_PICTURES_DIR "/IMG_%Y%m%d_%H%M%S.ppm", &_tm);
                flux_camera_capture(photo_path);
                load_gallery();
                gallery_scroll = 0;
                flux_ui_draw_gallery(&fb, gallery_names, gallery_dates,
                                     gallery_n, gallery_selected, gallery_scroll,
                                     gallery_filter, gallery_select_mode,
                                     gallery_thumb_provider, NULL);
            } else if (gh.segment >= 0) {
                /* Segment "Jahre|Monate|Alle" umschalten (Gruppierung real). */
                animate_ripple(&fb, ev.x, ev.y);
                gallery_filter = gh.segment;
                gallery_scroll = 0;
                flux_ui_draw_gallery(&fb, gallery_names, gallery_dates,
                                     gallery_n, gallery_selected, gallery_scroll,
                                     gallery_filter, gallery_select_mode,
                                     gallery_thumb_provider, NULL);
            } else if (gh.select) {
                /* Auswahl-Stub: schaltet den Auswahlmodus sichtbar um. */
                animate_ripple(&fb, ev.x, ev.y);
                gallery_select_mode = !gallery_select_mode;
                if (!gallery_select_mode) gallery_selected = -1;
                flux_ui_draw_gallery(&fb, gallery_names, gallery_dates,
                                     gallery_n, gallery_selected, gallery_scroll,
                                     gallery_filter, gallery_select_mode,
                                     gallery_thumb_provider, NULL);
            } else if (gh.filter) {
                /* Filter-Knopf: ehrlicher Stub -- Tipp-Feedback (Ripple). */
                animate_ripple(&fb, ev.x, ev.y);
            } else if (gh.search) {
                /* Such-Knopf: ehrlicher Stub -- Tipp-Feedback (Ripple). */
                animate_ripple(&fb, ev.x, ev.y);
            } else if (gh.tile >= 0 && gh.tile < gallery_n) {
                if (gallery_select_mode) {
                    /* Auswahl-Stub: Kachel markieren/abwaehlen. */
                    animate_ripple(&fb, ev.x, ev.y);
                    gallery_selected = (gallery_selected == gh.tile) ? -1 : gh.tile;
                    flux_ui_draw_gallery(&fb, gallery_names, gallery_dates,
                                         gallery_n, gallery_selected, gallery_scroll,
                                         gallery_filter, gallery_select_mode,
                                         gallery_thumb_provider, NULL);
                } else {
                    /* Foto oeffnen (bestehender Bild-Betrachter). */
                    animate_ripple(&fb, ev.x, ev.y);
                    open_image(&fb, gallery_names[gh.tile]);
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_IMAGE_VIEWER;
                    flux_ui_draw_image_viewer(&fb, gallery_names[gh.tile],
                        image_pixels, image_w, image_h, "", 0);
                    animate_slide_in(&fb, old);
                    free(old);
                }
            }
            continue;
        }

        if (screen == FLUX_SCREEN_IMAGE_VIEWER) {
            if (ev.type == FLUX_EV_SWIPE_LEFT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_GALLERY;
                flux_ui_draw_gallery(&fb, gallery_names, gallery_dates,
                                     gallery_n, gallery_selected, gallery_scroll,
                                     gallery_filter, gallery_select_mode,
                                     gallery_thumb_provider, NULL);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            if (ev.type != FLUX_EV_TAP) continue;

            int iv_back = 0, iv_analyze = 0, iv_del = 0;
            if (!flux_ui_image_viewer_hit(&fb, ev.x, ev.y,
                                           &iv_back, &iv_analyze, &iv_del)) continue;

            if (iv_back) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_GALLERY;
                flux_ui_draw_gallery(&fb, gallery_names, gallery_dates,
                                     gallery_n, gallery_selected, gallery_scroll,
                                     gallery_filter, gallery_select_mode,
                                     gallery_thumb_provider, NULL);
                animate_slide_from_left(&fb, old);
                free(old);
            } else if (iv_analyze) {
                /* KI-Bildanalyse: zeige Lade-Indikator, frage fluxaid */
                const char *fname = image_path;
                size_t plen = strlen(FLUX_PICTURES_DIR) + 1;
                if (strncmp(fname, FLUX_PICTURES_DIR "/", plen) == 0)
                    fname += plen;
                image_analyzing = 1;
                flux_ui_draw_image_viewer(&fb, fname,
                    image_pixels, image_w, image_h, "", 1);
                /* Analyseauftrag an fluxaid */
                char q[600];
                snprintf(q, sizeof(q), "Analysiere das Foto: %s", image_path);
                char resp[512] = {0};
                flux_ipc_ask(q, resp, sizeof(resp));
                /* Antwort bereinigen: ACTION:-Preamble entfernen falls noetig */
                if (strncmp(resp, "ACTION:", 7) == 0) {
                    snprintf(image_caption, sizeof(image_caption), "%s", image_path);
                } else if (resp[0]) {
                    snprintf(image_caption, sizeof(image_caption), "%s", resp);
                } else {
                    snprintf(image_caption, sizeof(image_caption),
                             "Keine Antwort vom KI-Assistenten.");
                }
                image_analyzing = 0;
                flux_ui_draw_image_viewer(&fb, fname,
                    image_pixels, image_w, image_h, image_caption, 0);
            } else if (iv_del) {
                unlink(image_path);
                free(image_pixels); image_pixels = NULL; image_w = image_h = 0;
                load_gallery();
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_GALLERY;
                flux_ui_draw_gallery(&fb, gallery_names, gallery_dates,
                                     gallery_n, gallery_selected, gallery_scroll,
                                     gallery_filter, gallery_select_mode,
                                     gallery_thumb_provider, NULL);
                animate_slide_from_left(&fb, old);
                free(old);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_MEETING) {
            int elapsed = meeting_recording ? (int)(time(NULL) - meeting_start_t) : 0;
            if (ev.type == FLUX_EV_SWIPE_LEFT || (ev.type == FLUX_EV_TAP &&
                    ({ int r,s,b; flux_ui_meeting_hit(&fb,ev.x,ev.y,&r,&s,&b); b; }))) {
                if (meeting_recording) {
                    meeting_recording = 0;
                    snprintf(meeting_status, sizeof(meeting_status),
                             "Aufnahme beendet (%d:%02d).", elapsed/60, elapsed%60);
                }
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            if (ev.type == FLUX_EV_TAP) {
                int rec, save, back;
                flux_ui_meeting_hit(&fb, ev.x, ev.y, &rec, &save, &back);
                if (rec) {
                    animate_ripple(&fb, ev.x, ev.y);
                    if (!meeting_recording) {
                        meeting_recording = 1;
                        meeting_start_t   = time(NULL);
                        meeting_transcript[0] = '\0';
                        snprintf(meeting_status, sizeof(meeting_status), "Aufnahme laueft...");
                    } else {
                        meeting_recording = 0;
                        elapsed = (int)(time(NULL) - meeting_start_t);
                        snprintf(meeting_status, sizeof(meeting_status),
                                 "Aufnahme beendet (%d:%02d). Whisper.cpp nicht installiert -- "
                                 "Transkription auf anderem Geraet moeglich.", elapsed/60, elapsed%60);
                    }
                } else if (save && (meeting_transcript[0] || elapsed > 0)) {
                    animate_ripple(&fb, ev.x, ev.y);
                    /* Save transcript to /home/user/Meetings/ */
                    mkdir("/home/user/Meetings", 0755);
                    time_t nt = time(NULL); struct tm ntm; localtime_r(&nt, &ntm);
                    char mpath[256];
                    strftime(mpath, sizeof(mpath),
                             "/home/user/Meetings/Meeting_%Y%m%d_%H%M.md", &ntm);
                    FILE *mf = fopen(mpath, "w");
                    if (mf) {
                        char hdr[128];
                        strftime(hdr, sizeof(hdr), "# Meeting %d.%m.%Y %H:%M\n\n", &ntm);
                        fprintf(mf, "%s", hdr);
                        if (meeting_transcript[0])
                            fprintf(mf, "%s\n", meeting_transcript);
                        else
                            fprintf(mf, "*(Keine Transkription verfuegbar -- Whisper.cpp fehlt)*\n");
                        fclose(mf);
                        snprintf(meeting_status, sizeof(meeting_status),
                                 "Gespeichert: %s", mpath);
                    }
                }
            }
            flux_ui_draw_meeting(&fb, meeting_recording, elapsed,
                                  meeting_transcript, meeting_status);
            continue;
        }

        if (screen == FLUX_SCREEN_MEMORY) {
            if (ev.type == FLUX_EV_SWIPE_LEFT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            if (ev.type == FLUX_EV_TAP) {
                int idx, back;
                if (flux_ui_list_hit(&fb, ev.x, ev.y, memory_n, &idx, &back) && back) {
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_ASSISTANT;
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                    animate_slide_from_left(&fb, old);
                    free(old);
                }
            }
            continue;
        }

        /* ---- FLUX_SCREEN_JOURNAL --------------------------------------- */
        if (screen == FLUX_SCREEN_JOURNAL) {
            if (ev.type == FLUX_EV_SWIPE_LEFT || ev.type == FLUX_EV_SWIPE_RIGHT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            if (ev.type == FLUX_EV_SWIPE_UP) {
                journal_scroll++;
                flux_ui_draw_journal(&fb, journal_names_p, journal_n, journal_scroll, -1);
                continue;
            }
            if (ev.type == FLUX_EV_SWIPE_DOWN) {
                if (journal_scroll > 0) journal_scroll--;
                flux_ui_draw_journal(&fb, journal_names_p, journal_n, journal_scroll, -1);
                continue;
            }
            if (ev.type == FLUX_EV_TAP) {
                int back;
                int idx = flux_ui_journal_hit(&fb, ev.x, ev.y, journal_n, &back);
                if (back) {
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_ASSISTANT;
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                    animate_slide_from_left(&fb, old);
                    free(old);
                } else if (idx >= 0 && idx < journal_n) {
                    char path[280];
                    snprintf(path, sizeof(path), "/home/user/Journal/%s.txt",
                             journal_names_buf[idx + journal_scroll]);
                    load_file_content(path);
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_FILE_VIEWER;
                    flux_ui_draw_file_viewer(&fb, viewer_path, viewer_content, viewer_scroll);
                    animate_slide_in(&fb, old);
                    free(old);
                }
            }
            continue;
        }

        /* ---- FLUX_SCREEN_VOICE_ENROLL ---------------------------------- */
        if (screen == FLUX_SCREEN_VOICE_ENROLL) {
            if (ev.type == FLUX_EV_SWIPE_LEFT || ev.type == FLUX_EV_SWIPE_RIGHT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_SETTINGS;
                flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            if (ev.type == FLUX_EV_TAP) {
                int hit = flux_ui_voice_hit(&fb, ev.x, ev.y);
                if (hit == 1) {
                    /* Aufnehmen */
                    voice_enroll_phase = 1;
                    flux_ui_draw_voice_enroll(&fb, voice_enroll_phase, "Aufnahme laeuft...");
                    voice_unlock_result_t r = voice_unlock_enroll(voice_enroll_msg, sizeof(voice_enroll_msg));
                    voice_enroll_phase = (r == VOICE_UNLOCK_OK) ? 2 : 3;
                    flux_ui_draw_voice_enroll(&fb, voice_enroll_phase, voice_enroll_msg);
                } else if (hit == 2) {
                    /* Ueberspringen / Fertig */
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_SETTINGS;
                    flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
                    animate_slide_from_left(&fb, old);
                    free(old);
                }
            }
            continue;
        }

        /* ---- FLUX_SCREEN_VOICE_VERIFY ---------------------------------- */
        if (screen == FLUX_SCREEN_VOICE_VERIFY) {
            if (ev.type == FLUX_EV_TAP) {
                int hit = flux_ui_voice_hit(&fb, ev.x, ev.y);
                if (hit == 1 && voice_verify_phase != 2) {
                    /* Sprechen */
                    voice_verify_phase = 1;
                    flux_ui_draw_voice_verify(&fb, voice_verify_phase, "Hoere zu...");
                    voice_unlock_result_t r = voice_unlock_verify(voice_verify_msg,
                                                                   sizeof(voice_verify_msg));
                    if (r == VOICE_UNLOCK_OK) {
                        voice_verify_phase = 2;
                        flux_ui_draw_voice_verify(&fb, voice_verify_phase, voice_verify_msg);
                    } else if (r == VOICE_UNLOCK_NO_MIC) {
                        /* Kein Mikrofon -- ehrlich weiterlassen (nur zweiter Faktor) */
                        voice_verify_phase = 3;
                        flux_ui_draw_voice_verify(&fb, voice_verify_phase, voice_verify_msg);
                    } else {
                        voice_verify_phase = 3;
                        flux_ui_draw_voice_verify(&fb, voice_verify_phase, voice_verify_msg);
                    }
                } else if (hit == 1 && voice_verify_phase == 2) {
                    /* Weiter nach OK */
                    screen = FLUX_SCREEN_ASSISTANT;
                    input_buf[0] = '\0'; answer_buf[0] = '\0';
                    flux_ui_set_quick_reveal(0, 0);
                    uint32_t *old = capture_frame(&fb);
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                    animate_slide_in(&fb, old); free(old);
                    animate_home_intro(&fb, &in, last_q, input_buf, answer_buf);
                } else if (hit == 2) {
                    /* PIN verwenden -- Stimm-Schutz ueberspringen (immer erlaubt) */
                    screen = FLUX_SCREEN_ASSISTANT;
                    input_buf[0] = '\0'; answer_buf[0] = '\0';
                    flux_ui_set_quick_reveal(0, 0);
                    uint32_t *old = capture_frame(&fb);
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                    animate_slide_in(&fb, old); free(old);
                    animate_home_intro(&fb, &in, last_q, input_buf, answer_buf);
                }
            }
            continue;
        }

        /* ---- FLUX_SCREEN_ALARM ----------------------------------------- */
        if (screen == FLUX_SCREEN_ALARM) {
            /* Jeder Tap oder Tastendruck stoppt den Alarm */
            if (ev.type == FLUX_EV_TAP || ev.type == FLUX_EV_ENTER ||
                ev.type == FLUX_EV_SWIPE_UP || ev.type == FLUX_EV_SWIPE_DOWN) {
                alarm_label[0] = '\0';
                screen = FLUX_SCREEN_LOCK;
                flux_ui_draw_lock(&fb);
            }
            continue;
        }

        /* ---- FLUX_SCREEN_CALL ------------------------------------------ */
        if (screen == FLUX_SCREEN_CALL) {
            if (ev.type == FLUX_EV_TAP) {
                flux_call_hit_t h = flux_ui_call_hit(&fb, ev.x, ev.y, call_connected);
                if (h == FLUX_CALL_ACCEPT && !call_connected) {
                    /* Annehmen: Telefonie-Stub anstossen (ehrliche Meldung
                     * merken) und das Gespraech samt KI-Mitschnitt starten. */
                    char req[FLUX_MAX_LINE];
                    flux_action_build_request(&pending_action, req, sizeof(req));
                    flux_ipc_send_raw(req, call_tel_note, sizeof(call_tel_note));
                    for (char *p = call_tel_note; *p; p++)
                        if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
                    call_connected = 1;
                    call_start_t   = time(NULL);
                    call_transcript[0] = '\0';   /* kein Mikrofon -> Stub bleibt leer */
                    flux_ui_draw_call(&fb, call_name, call_number, call_connected, 0);
                } else if (h == FLUX_CALL_HANGUP) {
                    if (call_connected) {
                        /* Auflegen: Mitschnitt speichern (KI-Transkript-Struktur). */
                        int dur = (int)(time(NULL) - call_start_t);
                        save_call_log(call_name, call_number, call_transcript,
                                      call_tel_note, dur, answer_buf, sizeof(answer_buf));
                    } else {
                        snprintf(answer_buf, sizeof(answer_buf), "Anruf abgelehnt.");
                    }
                    call_connected = 0;
                    screen = FLUX_SCREEN_ASSISTANT;
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                }
            }
            continue;
        }

        /* ---- FLUX_SCREEN_HABITS ---------------------------------------- */
        if (screen == FLUX_SCREEN_HABITS) {
            if (ev.type == FLUX_EV_SWIPE_LEFT || ev.type == FLUX_EV_SWIPE_RIGHT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }
            if (ev.type == FLUX_EV_SWIPE_UP) {
                habits_scroll++;
                if (habits_scroll >= habits_n) habits_scroll = habits_n > 0 ? habits_n - 1 : 0;
                flux_ui_draw_habits(&fb, habits_p, habits_n, habits_scroll);
                continue;
            }
            if (ev.type == FLUX_EV_SWIPE_DOWN) {
                if (habits_scroll > 0) habits_scroll--;
                flux_ui_draw_habits(&fb, habits_p, habits_n, habits_scroll);
                continue;
            }
            if (ev.type == FLUX_EV_TAP) {
                int back;
                if (flux_ui_habits_hit(&fb, ev.x, ev.y, &back) && back) {
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_ASSISTANT;
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                    animate_slide_from_left(&fb, old);
                    free(old);
                }
            }
            continue;
        }

        if (screen == FLUX_SCREEN_SEARCH) {
            if (ev.type == FLUX_EV_SWIPE_LEFT) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                animate_slide_from_left(&fb, old);
                free(old);
                continue;
            }

            int do_search = 0;

            if (ev.type == FLUX_EV_TAP) {
                int srch_back, srch_ridx;
                flux_ui_search_hit(&fb, ev.x, ev.y, &srch_back, &srch_ridx);
                if (srch_back) {
                    uint32_t *old = capture_frame(&fb);
                    screen = FLUX_SCREEN_ASSISTANT;
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                    animate_slide_from_left(&fb, old);
                    free(old);
                    continue;
                }
                /* Tastatur */
                char tap_ch = 0; int tap_bs = 0, tap_enter = 0;
                if (flux_ui_kbd_hit(&fb, ev.x, ev.y, &tap_ch, &tap_bs, &tap_enter)) {
                    if (tap_bs) {
                        size_t ql = strlen(search_query);
                        if (ql > 0) search_query[ql-1] = '\0';
                    } else if (tap_enter) {
                        do_search = search_query[0] != '\0';
                    } else if (tap_ch) {
                        size_t ql = strlen(search_query);
                        if (ql + 1 < sizeof(search_query)) {
                            search_query[ql] = tap_ch;
                            search_query[ql+1] = '\0';
                        }
                    }
                }
            } else if (ev.type == FLUX_EV_CHAR) {
                size_t ql = strlen(search_query);
                if (ql + 1 < sizeof(search_query)) {
                    search_query[ql] = ev.ch;
                    search_query[ql+1] = '\0';
                }
            } else if (ev.type == FLUX_EV_BACKSPACE) {
                size_t ql = strlen(search_query);
                if (ql > 0) search_query[ql-1] = '\0';
            } else if (ev.type == FLUX_EV_ENTER) {
                do_search = search_query[0] != '\0';
            }

            if (do_search) {
                search_searching = 1;
                search_n = 0;
                flux_ui_draw_search(&fb, search_query, search_results_p, 0, 1);

                char sctx[6400] = {0};
                collect_search_context(search_query, sctx, sizeof(sctx));

                char sq[6800];
                snprintf(sq, sizeof(sq),
                         "%s\n\n"
                         "Gib die passendsten Treffer als Liste aus. Format pro Zeile:\n"
                         "Quelle: Inhalt\n"
                         "Quelle ist: Gedaechtnis, Kalender, Kontakte oder Notizen.\n"
                         "Maximal 8 Treffer. Nur die Trefferliste, keine Erklaerungen.",
                         sctx);

                char sanswer[FLUX_MAX_RESPONSE] = {0};
                flux_ipc_ask(sq, sanswer, sizeof(sanswer));
                parse_search_results(sanswer);
                search_searching = 0;
            }

            flux_ui_draw_search(&fb, search_query, search_results_p, search_n, search_searching);
            continue;
        }

        /* FLUX_SCREEN_ASSISTANT -- Voice-Overlay: beliebiger Input stoppt die Aufnahme */
        if (voice_active && (ev.type == FLUX_EV_TAP || ev.type == FLUX_EV_CHAR ||
                              ev.type == FLUX_EV_ENTER || ev.type == FLUX_EV_BACKSPACE)) {
            voice_active = 0;
            flux_ui_draw_assistant(&fb, last_q, input_buf, "Transkribiere...", 1);
            char voice_text[512] = {0};
            if (flux_voice_stop_and_transcribe(voice_text, sizeof(voice_text)) && voice_text[0]) {
                snprintf(input_buf, sizeof(input_buf), "%s", voice_text);
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
            } else {
                snprintf(answer_buf, sizeof(answer_buf),
                         "Keine Spracheingabe erkannt -- bitte deutlicher sprechen.");
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
            }
            continue;
        }

        /* FLUX_SCREEN_ASSISTANT -- Wisch nach unten oeffnet den Notify-Overlay. */
        if (ev.type == FLUX_EV_SWIPE_DOWN) {
            pre_notify_screen = FLUX_SCREEN_ASSISTANT;
            uint32_t *old = capture_frame(&fb);
            screen = FLUX_SCREEN_NOTIFY;
            flux_ui_draw_notify(&fb);
            animate_slide_in(&fb, old);
            free(old);
            continue;
        }

        /* FLUX_SCREEN_ASSISTANT -- Quickrow/Mikrofon-Knopf zuerst pruefen,
         * sonst Taps auf die Bildschirmtastatur wie Hardware-Eingaben
         * behandeln (gemeinsamer Verarbeitungspfad). */
        if (ev.type == FLUX_EV_TAP) {
            int quick = flux_ui_quickrow_hit(&fb, ev.x, ev.y);
            if (quick == 1) {
                load_settings_values();
                animate_ripple(&fb, ev.x, ev.y);
                screen = FLUX_SCREEN_SETTINGS;
                animate_settings_intro(&fb, &in, setting_labels, setting_values,
                                       FLUX_SETTINGS_N);
                continue;
            } else if (quick == 2) {
                strcpy(files_path, "/");
                file_selected = -1;
                load_files(files_path);
                animate_ripple(&fb, ev.x, ev.y);
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_FILES;
                flux_ui_draw_files(&fb, files_path, file_names, file_metas,
                                   file_n, file_truncated, file_selected);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            } else if (quick == 3) {
                load_cal_events(cal_year, cal_month);
                animate_ripple(&fb, ev.x, ev.y);
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_CALENDAR;
                flux_ui_draw_calendar(&fb, cal_year, cal_month, cal_today_view(),
                                       cal_selected_day, cal_event_strs, cal_n_events);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            } else if (quick == 4) {
                load_contacts_list();
                contact_selected = -1;
                animate_ripple(&fb, ev.x, ev.y);
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_CONTACTS;
                flux_ui_draw_contacts(&fb, contact_names_p, contact_details_p,
                                       contact_n, contact_selected);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (flux_ui_mic_hit(&fb, ev.x, ev.y)) {
                if (!flux_voice_can_record()) {
                    snprintf(answer_buf, sizeof(answer_buf),
                             "Kein Mikrofon erkannt -- arecord oder ffmpeg wird benoetigt.");
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                } else if (flux_voice_start()) {
                    voice_active = 1;
                    voice_start_t = time(NULL);
                    voice_frame = 0;
                    flux_ui_draw_voice_overlay(&fb, 0, voice_frame++);
                } else {
                    snprintf(answer_buf, sizeof(answer_buf),
                             "Aufnahme konnte nicht gestartet werden.");
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                }
                continue;
            }
            if (flux_ui_copy_hit(&fb, ev.x, ev.y)) {
                if (input_buf[0])
                    snprintf(clipboard, sizeof(clipboard), "%s", input_buf);
                /* kein Redraw noetig, visuelles Feedback nicht erforderlich */
                continue;
            }
            if (flux_ui_paste_hit(&fb, ev.x, ev.y)) {
                if (clipboard[0]) {
                    size_t clen = strlen(clipboard);
                    size_t ilen = strlen(input_buf);
                    size_t avail = sizeof(input_buf) - ilen - 1;
                    size_t copy = clen < avail ? clen : avail;
                    strncat(input_buf, clipboard, copy);
                    flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                }
                continue;
            }
        }

        flux_event_type_t kind = ev.type;
        char ch = ev.ch;

        if (kind == FLUX_EV_TAP) {
            char tap_ch = 0;
            int tap_backspace = 0, tap_enter = 0;
            if (!flux_ui_kbd_hit(&fb, ev.x, ev.y, &tap_ch, &tap_backspace, &tap_enter))
                continue; /* Tap ausserhalb der Tastatur -- ignorieren */
            if (tap_backspace) kind = FLUX_EV_BACKSPACE;
            else if (tap_enter) kind = FLUX_EV_ENTER;
            else { kind = FLUX_EV_CHAR; ch = tap_ch; }
        } else if (kind == FLUX_EV_SWIPE_UP || kind == FLUX_EV_SWIPE_LEFT) {
            continue; /* auf dem Assistenten-Bildschirm ohne Bedeutung */
        }

        if (kind == FLUX_EV_CHAR) {
            size_t len = strlen(input_buf);
            if (len + 1 < sizeof(input_buf)) {
                input_buf[len] = ch;
                input_buf[len + 1] = '\0';
            }
            flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
        } else if (kind == FLUX_EV_BACKSPACE) {
            size_t len = strlen(input_buf);
            if (len > 0) input_buf[len - 1] = '\0';
            flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
        } else if (kind == FLUX_EV_ENTER) {
            if (input_buf[0] == '\0') continue;

            if (strcasecmp(input_buf, "einstellungen") == 0 || strcasecmp(input_buf, "settings") == 0) {
                input_buf[0] = '\0';
                load_settings_values();
                screen = FLUX_SCREEN_SETTINGS;
                animate_settings_intro(&fb, &in, setting_labels, setting_values,
                                       FLUX_SETTINGS_N);
                continue;
            }
            if (strcasecmp(input_buf, "dateien") == 0 || strcasecmp(input_buf, "files") == 0) {
                input_buf[0] = '\0';
                strcpy(files_path, "/");
                file_selected = -1;
                load_files(files_path);
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_FILES;
                flux_ui_draw_files(&fb, files_path, file_names, file_metas,
                                   file_n, file_truncated, file_selected);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "screenshot") == 0) {
                input_buf[0] = '\0';
                save_screenshot(&fb, answer_buf, sizeof(answer_buf));
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
                continue;
            }
            if (strcasecmp(input_buf, "benachrichtigungen") == 0 || strcasecmp(input_buf, "notify") == 0) {
                input_buf[0] = '\0';
                pre_notify_screen = FLUX_SCREEN_ASSISTANT;
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_NOTIFY;
                flux_ui_draw_notify(&fb);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "kalender") == 0 || strcasecmp(input_buf, "calendar") == 0) {
                input_buf[0] = '\0';
                load_cal_events(cal_year, cal_month);
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_CALENDAR;
                flux_ui_draw_calendar(&fb, cal_year, cal_month, cal_today_view(),
                                       cal_selected_day, cal_event_strs, cal_n_events);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "kontakte") == 0 || strcasecmp(input_buf, "contacts") == 0) {
                input_buf[0] = '\0';
                load_contacts_list();
                contact_selected = -1;
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_CONTACTS;
                flux_ui_draw_contacts(&fb, contact_names_p, contact_details_p,
                                       contact_n, contact_selected);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "fotos") == 0 || strcasecmp(input_buf, "galerie") == 0 ||
                strcasecmp(input_buf, "gallery") == 0 || strcasecmp(input_buf, "bilder") == 0) {
                input_buf[0] = '\0';
                load_gallery();
                gallery_selected = -1;
                gallery_scroll = 0;
                gallery_select_mode = 0;
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_GALLERY;
                flux_ui_draw_gallery(&fb, gallery_names, gallery_dates,
                                     gallery_n, gallery_selected, gallery_scroll,
                                     gallery_filter, gallery_select_mode,
                                     gallery_thumb_provider, NULL);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "notizen") == 0 || strcasecmp(input_buf, "notes") == 0) {
                input_buf[0] = '\0';
                /* Open notes file directly in file viewer */
                const char *npath = "/etc/flux/notes.txt";
                snprintf(viewer_path, sizeof(viewer_path), "%s", npath);
                viewer_content[0] = '\0'; viewer_scroll = 0;
                FILE *nf = fopen(npath, "r");
                if (nf) {
                    size_t nn = fread(viewer_content, 1, VIEWER_CONTENT_MAX - 1, nf);
                    viewer_content[nn] = '\0';
                    fclose(nf);
                } else {
                    snprintf(viewer_content, sizeof(viewer_content), "(Noch keine Notizen vorhanden.)");
                }
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_FILE_VIEWER;
                flux_ui_draw_file_viewer(&fb, viewer_path, viewer_content, viewer_scroll);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "meeting") == 0 || strcasecmp(input_buf, "besprechung") == 0 ||
                strcasecmp(input_buf, "aufnahme") == 0 || strcasecmp(input_buf, "mitschrift") == 0) {
                input_buf[0] = '\0';
                meeting_recording = 0; meeting_start_t = 0;
                meeting_transcript[0] = '\0';
                snprintf(meeting_status, sizeof(meeting_status),
                         "Aufnahme-Knopf druecken um zu beginnen.");
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_MEETING;
                flux_ui_draw_meeting(&fb, 0, 0, "", meeting_status);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "transkript") == 0 || strcasecmp(input_buf, "gespraeche") == 0 ||
                strcasecmp(input_buf, "verlauf") == 0) {
                input_buf[0] = '\0';
                mkdir("/home/user/Journal", 0755);
                snprintf(files_path, sizeof(files_path), "/home/user/Journal");
                load_files(files_path);
                file_selected = -1;
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_FILES;
                flux_ui_draw_files(&fb, files_path, file_names, file_metas,
                                   file_n, file_truncated, file_selected);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "journal") == 0 || strcasecmp(input_buf, "tagebuch") == 0) {
                input_buf[0] = '\0';
                load_journal_list();
                journal_scroll = 0;
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_JOURNAL;
                flux_ui_draw_journal(&fb, journal_names_p, journal_n, journal_scroll, -1);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "gedaechtnis") == 0 || strcasecmp(input_buf, "memory") == 0 ||
                strcasecmp(input_buf, "erinnerungen") == 0 || strcasecmp(input_buf, "ki-speicher") == 0) {
                input_buf[0] = '\0';
                load_memory();
                memory_scroll = 0;
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_MEMORY;
                flux_ui_draw_memory(&fb, memory_entries, memory_n, memory_scroll);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "kamera") == 0 || strcasecmp(input_buf, "camera") == 0 ||
                strcasecmp(input_buf, "foto") == 0) {
                input_buf[0] = '\0';
                char photo_path[256];
                time_t _t2 = time(NULL); struct tm _tm2; localtime_r(&_t2, &_tm2);
                strftime(photo_path, sizeof(photo_path),
                         FLUX_PICTURES_DIR "/IMG_%Y%m%d_%H%M%S.ppm", &_tm2);
                flux_camera_capture(photo_path);
                load_gallery();
                open_image(&fb, gallery_names_buf[0]);
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_IMAGE_VIEWER;
                flux_ui_draw_image_viewer(&fb, gallery_names[0],
                    image_pixels, image_w, image_h, "", 0);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "suche") == 0 || strcasecmp(input_buf, "search") == 0 ||
                strcasecmp(input_buf, "finden") == 0 || strcasecmp(input_buf, "finder") == 0) {
                input_buf[0] = '\0';
                search_query[0] = '\0';
                search_n = 0;
                search_searching = 0;
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_SEARCH;
                flux_ui_draw_search(&fb, search_query, search_results_p,
                                    search_n, search_searching);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }
            if (strcasecmp(input_buf, "gewohnheiten") == 0 ||
                strcasecmp(input_buf, "habits") == 0 ||
                strcasecmp(input_buf, "aktivitaet") == 0 ||
                strcasecmp(input_buf, "statistik") == 0) {
                input_buf[0] = '\0';
                load_habits();
                habits_scroll = 0;
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_HABITS;
                flux_ui_draw_habits(&fb, habits_p, habits_n, habits_scroll);
                animate_slide_in(&fb, old);
                free(old);
                continue;
            }

            snprintf(last_q, sizeof(last_q), "%s", input_buf);
            flux_ui_draw_assistant(&fb, last_q, "", answer_buf, 1);
            flux_ipc_ask(last_q, answer_buf, sizeof(answer_buf));
            input_buf[0] = '\0';

            /* Gespraechs-Transkription: Q&A in tagesaktuelle Datei speichern */
            {
                time_t _tt = time(NULL); struct tm _ttm; localtime_r(&_tt, &_ttm);
                char _tfile[256], _ts[32];
                strftime(_tfile, sizeof(_tfile), "/home/user/Journal/Gespraeche_%Y-%m-%d.md", &_ttm);
                strftime(_ts, sizeof(_ts), "%H:%M", &_ttm);
                mkdir("/home/user/Journal", 0755);
                FILE *_tf = fopen(_tfile, "a");
                if (_tf) {
                    fprintf(_tf, "\n**[%s] Nutzer:** %s\n\n**KI:** %s\n", _ts, last_q, answer_buf);
                    fclose(_tf);
                }
            }

            /* TTS: KI-Antwort vorlesen (wenn aktiviert) */
            tts_speak(answer_buf);

            if (flux_action_parse(answer_buf, &pending_action)) {
                uint32_t *old = capture_frame(&fb);
                screen = FLUX_SCREEN_CONFIRM;
                flux_ui_draw_confirm(&fb, flux_action_type_label(pending_action.type),
                                      pending_action.to, pending_action.subject, pending_action.body);
                animate_slide_in(&fb, old);
                free(old);
            } else {
                flux_ui_draw_assistant(&fb, last_q, input_buf, answer_buf, 0);
            }
        }
    }

    flux_input_close(&in);
    free(image_pixels);
    thumb_cache_clear();
    flux_fb_close(&fb);
    return 0;
}
