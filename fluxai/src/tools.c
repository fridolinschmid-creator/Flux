/* tools.c -- Implementierung der KI-Tools fuer fluxaid.
 *
 * Verfuegbare Tools:
 *   date_time        -- aktuelles Datum und Uhrzeit
 *   weather          -- Wetter via wttr.in (kein API-Key noetig), ARG: Stadtname
 *   file_read        -- Datei lesen, ARG: Pfad
 *   file_list        -- Verzeichnis auflisten, ARG: Pfad
 *   file_create      -- Datei erstellen, ARG: Pfad|Inhalt (\n fuer Zeilenumbruch)
 *   file_delete      -- Datei loeschen (nur /home/user/), ARG: Pfad
 *   calculate        -- Mathematischen Ausdruck auswerten, ARG: Ausdruck
 *   note_save        -- Notiz speichern, ARG: Text
 *   note_list        -- Gespeicherte Notizen anzeigen, ARG: (leer)
 *   note_search      -- Notizen durchsuchen, ARG: Suchbegriff
 *   note_delete      -- Notizen loeschen (alle mit Suchbegriff), ARG: Suchbegriff
 *   sys_info         -- Systeminfo (Speicher, OS), ARG: (leer)
 *   brightness_get   -- Bildschirmhelligkeit lesen, ARG: (leer)
 *   brightness_set   -- Bildschirmhelligkeit setzen, ARG: 0-100 (Prozent)
 *   wifi_info        -- WLAN-Signalstaerke und Interface, ARG: (leer)
 *   wifi_on          -- WLAN einschalten (rfkill unblock wifi), ARG: (leer)
 *   wifi_off         -- WLAN ausschalten (rfkill block wifi), ARG: (leer)
 *   flight_mode_on   -- Flugmodus aktivieren (rfkill block all), ARG: (leer)
 *   flight_mode_off  -- Flugmodus deaktivieren (rfkill unblock all), ARG: (leer)
 *   pin_set          -- Geraete-PIN aendern, ARG: neuer PIN (4-8 Ziffern)
 *   vibrate          -- Geraet vibrieren lassen, ARG: Dauer in ms (z.B. 300)
 *   memory_save      -- Personliche Info dauerhaft merken, ARG: Text
 *   memory_list      -- Alle KI-Erinnerungen anzeigen, ARG: (leer)
 *   memory_search    -- KI-Erinnerungen durchsuchen, ARG: Suchbegriff
 *   memory_delete    -- Erinnerungen loeschen, ARG: Suchbegriff
 *   alarm_list       -- Gesetzte Alarme anzeigen, ARG: (leer)
 *   alarm_delete     -- Alarm loeschen (alle mit Suchbegriff), ARG: Suchbegriff
 *   timer_set        -- Countdown-Timer starten, ARG: Dauer (z.B. "5 Minuten")
 *   timer_list       -- Aktive Timer mit Restzeit anzeigen, ARG: (leer)
 *   timer_delete     -- Timer loeschen (alle mit Suchbegriff), ARG: Suchbegriff
 *   reminder_list    -- Gesetzte Erinnerungen anzeigen, ARG: (leer)
 *   reminder_delete  -- Erinnerung loeschen (alle mit Suchbegriff), ARG: Suchbegriff
 */
#include "tools.h"
#include "vision.h"
#include "specialists.h"
#include "imap.h"
#include "radio.h"
#include "../../common/flux_config.h"
#include "../../common/flux_sha256.h"
#include "../../common/flux_util.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>

#define FLUX_USER_DOCS_DIR "/home/user/Dokumente"  /* Standard-Ablage fuer Nutzer-/KI-Dateien */
#define NOTES_PATH      "/etc/flux/notes.txt"
#define MEMORY_PATH     "/etc/flux/memory.txt"
#define WEATHER_CACHE   "/tmp/flux_weather.txt"
#define FILE_READ_MAX   8192
#define NOTE_TEXT_MAX   512

/* Der HTTP-Antwortpuffer und der libcurl-Write-Callback liegen jetzt in
 * common/flux_util (flux_http_buf / flux_http_write_cb). Die fruehere lokale
 * membuf-Variante war fix-kapazitaet auf Stack-Puffern; die gemeinsame Version
 * waechst per realloc und braucht daher heap-allozierte Puffer. */

/* ---- Sicherheits-/Such-Hilfsfunktionen ------------------------------- */

/* 1, wenn der Pfad eine ".."-Komponente enthaelt (Directory-Traversal).
 * Praefix-Checks allein (z.B. "beginnt mit /home/user/") reichen nicht,
 * weil "/home/user/../../etc/x" den Praefix passiert aber ausbricht. */
static int path_has_traversal(const char *p) {
    if (!p) return 1;
    for (const char *s = p; *s; ) {
        if (s[0] == '.' && s[1] == '.' &&
            (s[2] == '\0' || s[2] == '/') &&
            (s == p || s[-1] == '/'))
            return 1;
        const char *slash = strchr(s, '/');
        if (!slash) break;
        s = slash + 1;
    }
    return 0;
}

/* Gross-/Kleinschreibung-unabhaengige Teilstringsuche (ASCII).
 * Ersetzt vier zuvor kopierte "beide Seiten kleinschreiben, dann strstr"-
 * Schleifen. Gibt 1 zurueck, wenn needle in haystack vorkommt. */
static int str_contains_ci(const char *haystack, const char *needle) {
    if (!*needle) return 1;
    for (const char *h = haystack; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
            if (ca != cb) break;
            a++; b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

/* ---- date_time -------------------------------------------------------- */

static int tool_date_time(const char *arg, char *out, size_t cap) {
    (void)arg;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[128];
    strftime(buf, sizeof(buf), "%A, %d. %B %Y, %H:%M:%S %Z", &tm);
    snprintf(out, cap, "%s", buf);
    return 1;
}

/* ---- weather ---------------------------------------------------------- */

static int tool_weather(const char *arg, char *out, size_t cap) {
    /* Wenn arg leer -> IP-basierte Ortserkennung (wttr.in ohne Ort) */
    const char *city_raw = (arg && *arg) ? arg : "";

    /* Stadtname bereinigen: nur alphanumerische Zeichen, Leerzeichen, Bindestrich
     * und Unterstrich erlaubt -- verhindert URL-Manipulation / SSRF. */
    char safe_city[128] = {0};
    size_t si = 0;
    for (size_t i = 0; city_raw[i] && si + 1 < sizeof(safe_city); i++) {
        unsigned char c = (unsigned char)city_raw[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ')
            safe_city[si++] = (char)c;
    }
    safe_city[si] = '\0';
    /* Leerzeichen → '+' fuer URL-Kompatibilitaet */
    for (size_t i = 0; safe_city[i]; i++)
        if (safe_city[i] == ' ') safe_city[i] = '+';

    /* URL aufbauen: wttr.in/{city}?format=%l:+%C,+%t,+%h+Feuchte,+Wind+%w */
    char url[512];
    snprintf(url, sizeof(url),
             "https://wttr.in/%s?format=%%l:+%%C,+%%t,+%%h+Feuchte,+Wind+%%w",
             safe_city);

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(out, cap, "Fehler: curl nicht verfuegbar");
        return 1;
    }

    /* Wachsender Heap-Antwortpuffer (gemeinsame flux_http_buf-Hilfe).
     * Nicht auf einen Stack-Array zeigen lassen -- der Callback realloc't. */
    flux_http_buf mb;
    if (flux_http_buf_init(&mb, 1024) != 0) {
        curl_easy_cleanup(curl);
        snprintf(out, cap, "Fehler: kein Speicher");
        return 1;
    }

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Accept-Language: de");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, flux_http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.x (flux-os)");

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        flux_http_buf_free(&mb);
        snprintf(out, cap, "Wetterdaten nicht verfuegbar: %s", curl_easy_strerror(res));
        return 1;
    }

    const char *respbuf = mb.data;

    /* Steuerzeichen (Emoji, ANSI) aus Antwort entfernen -- das Terminal-Format
     * von wttr.in enthaelt manchmal Escape-Sequenzen. */
    char clean[512];
    size_t ci = 0;
    for (size_t i = 0; respbuf[i] && ci + 1 < sizeof(clean); i++) {
        unsigned char c = (unsigned char)respbuf[i];
        if (c == '\x1b') {
            /* ANSI Escape: ueberspringen bis 'm' */
            while (respbuf[i] && respbuf[i] != 'm') i++;
            if (!respbuf[i]) break; /* Sequenz ohne 'm' am String-Ende */
        } else if (c < 0x20 && c != '\n') {
            continue;
        } else if (c >= 0x80) {
            /* Multi-Byte UTF-8 (Emoji etc.): ueberspringen */
            if (c >= 0xC0) {
                int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
                while (extra-- > 0 && respbuf[i + 1]) i++;
            }
        } else {
            clean[ci++] = respbuf[i];
        }
    }
    clean[ci] = '\0';
    flux_http_buf_free(&mb);

    /* Newlines durch Leerzeichen ersetzen */
    for (size_t i = 0; clean[i]; i++)
        if (clean[i] == '\n') clean[i] = ' ';

    snprintf(out, cap, "%s", clean);

    /* Ergebnis cachen fuer das UI-Widget */
    FILE *f = fopen(WEATHER_CACHE, "w");
    if (f) { fprintf(f, "%s\n", out); fclose(f); }

    return 1;
}

/* ---- file_read ------------------------------------------------------- */

/* Erlaubt der KI nur das Lesen aus unbedenklichen Verzeichnissen.
 * Verhindert insbesondere Zugriff auf /etc/flux/flux.conf (API-Keys,
 * SMTP-Passwort) und andere Systemdateien. */
static int path_read_allowed(const char *path) {
    /* Pfad-Traversal mit ".."-Komponenten grundsaetzlich ablehnen */
    if (path_has_traversal(path)) return 0;

    /* Kanonischen Pfad aufloesen und ERST DANN gegen die erlaubten
     * Praefixe pruefen. Ohne das laesst sich der Schutz von flux.conf
     * (API-Keys, SMTP-Passwort) ueber die /proc-Magic-Symlinks umgehen,
     * z.B. "/proc/self/root/etc/flux/flux.conf". realpath() folgt allen
     * Symlinks; die Datei muss existieren (file_read oeffnet sie ohnehin). */
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) return 0;

    /* Prozess-Umgebungen nie herausgeben -- /proc/<pid>/environ enthaelt
     * u.a. den per Umgebungsvariable gesetzten API-Key. Nur unter /proc
     * sperren, damit eine echte Nutzerdatei namens "environ" lesbar bleibt. */
    if (strncmp(resolved, "/proc/", 6) == 0) {
        const char *base = strrchr(resolved, '/');
        if (base && strcmp(base, "/environ") == 0) return 0;
    }

    static const char *ok_prefixes[] = {
        "/home/user/", "/tmp/", "/proc/", "/sys/", NULL
    };
    for (int i = 0; ok_prefixes[i]; i++)
        if (strncmp(resolved, ok_prefixes[i], strlen(ok_prefixes[i])) == 0) return 1;
    return 0;
}

static int tool_file_read(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Pfad angegeben");
        return 1;
    }
    if (!path_read_allowed(arg)) {
        snprintf(out, cap,
                 "Fehler: Lesen nur unter /home/user/, /tmp/, /proc/ und /sys/ "
                 "erlaubt (Schutz von Systemdateien und Zugangsdaten).");
        return 1;
    }
    FILE *f = fopen(arg, "r");
    if (!f) {
        snprintf(out, cap, "Fehler: Datei '%s' nicht gefunden oder kein Zugriff", arg);
        return 1;
    }
    size_t max = cap - 1;
    if (max > FILE_READ_MAX) max = FILE_READ_MAX;
    size_t n = fread(out, 1, max, f);
    fclose(f);
    out[n] = '\0';
    if (n == 0) snprintf(out, cap, "(Datei leer)");
    return 1;
}

/* ---- file_list ------------------------------------------------------- */

static int tool_file_list(const char *arg, char *out, size_t cap) {
    const char *path = (arg && *arg) ? arg : "/";
    DIR *d = opendir(path);
    if (!d) {
        snprintf(out, cap, "Fehler: Verzeichnis '%s' nicht zugaenglich", path);
        return 1;
    }
    char tmp[4096];
    size_t pos = 0;
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "Inhalt von %s:\n", path);

    struct dirent *e;
    int count = 0;
    while ((e = readdir(d)) != NULL && count < 50) {
        if (strcmp(e->d_name, ".") == 0) continue;
        char full[1280];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        struct stat st;
        const char *kind = "?";
        if (stat(full, &st) == 0) {
            if (S_ISDIR(st.st_mode)) kind = "[Ordner]";
            else if (st.st_size < 1024) {
                static char sz[32];
                snprintf(sz, sizeof(sz), "%lld B", (long long)st.st_size);
                kind = sz;
            } else {
                static char sz[32];
                snprintf(sz, sizeof(sz), "%.1f KB", st.st_size / 1024.0);
                kind = sz;
            }
        }
        pos += snprintf(tmp + pos, sizeof(tmp) - pos,
                        "  %s  %s\n", e->d_name, kind);
        count++;
    }
    closedir(d);
    if (count == 0) pos += snprintf(tmp + pos, sizeof(tmp) - pos, "  (leer)\n");
    snprintf(out, cap, "%s", tmp);
    return 1;
}

/* ---- Pfad-Sicherheitspruefung ---------------------------------------- */

/* Gibt 1 zurueck wenn der kanonische Pfad unter einem der erlaubten
 * Praefix-Verzeichnisse liegt. Verhindert Path-Traversal (../../etc/passwd). */
static int path_is_allowed(const char *path, int allow_tmp) {
    char resolved[PATH_MAX];
    /* Datei muss nicht existieren -- realpath erlaubt es fuer Zieldatei-Checks
     * leider nur wenn der Elternpfad existiert. Wir pruefen daher zuerst
     * den Elternpfad, falls die Datei selbst noch nicht existiert. */
    if (!realpath(path, resolved)) {
        /* Elternordner versuchen */
        char parent[PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", path);
        char *slash = strrchr(parent, '/');
        if (slash && slash != parent) {
            *slash = '\0';
            if (!realpath(parent, resolved)) return 0;
            /* Kanonischen Elternpfad + Dateiname rekonstruieren */
            size_t pl = strlen(resolved);
            snprintf(resolved + pl, sizeof(resolved) - pl, "/%s", slash + 1);
        } else {
            return 0;
        }
    }
    if (strncmp(resolved, "/home/user/", 11) == 0) return 1;
    if (allow_tmp && strncmp(resolved, "/tmp/", 5) == 0) return 1;
    return 0;
}

/* ---- file_create ----------------------------------------------------- */

static int tool_file_create(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Pfad angegeben");
        return 1;
    }

    /* Format: "pfad|inhalt" -- | als Trennzeichen */
    const char *sep = strchr(arg, '|');
    char raw[512];
    if (!sep) {
        snprintf(raw, sizeof(raw), "%s", arg);
    } else {
        size_t plen = (size_t)(sep - arg);
        if (plen >= sizeof(raw)) plen = sizeof(raw) - 1;
        memcpy(raw, arg, plen);
        raw[plen] = '\0';
    }
    /* fuehrende Leerzeichen am Pfad entfernen */
    char *rp = raw;
    while (*rp == ' ') rp++;

    /* Relative/blanke Namen landen im Benutzer-Ordner /home/user/Dokumente
     * (dort startet auch der Datei-Browser). Ordner bei Bedarf anlegen. */
    char path[600];
    if (rp[0] == '/') {
        snprintf(path, sizeof(path), "%s", rp);
    } else {
        mkdir("/home/user", 0755);
        mkdir(FLUX_USER_DOCS_DIR, 0755);
        snprintf(path, sizeof(path), "%s/%s", FLUX_USER_DOCS_DIR, rp);
    }

    /* Sicherheit: kanonischen Pfad pruefen (verhindert Path-Traversal) */
    if (!path_is_allowed(path, 1)) {
        snprintf(out, cap,
                 "Fehler: Erstellen nur unter /home/user/ und /tmp/ erlaubt (ohne '..')");
        return 1;
    }

    if (!sep) {
        FILE *f = fopen(path, "w");
        if (!f) {
            snprintf(out, cap, "Fehler: Datei '%s' konnte nicht erstellt werden", path);
            return 1;
        }
        if (fclose(f) != 0)
            snprintf(out, cap, "Warnung: fclose fehlgeschlagen fuer '%s'", path);
        else
            snprintf(out, cap, "Datei '%s' erstellt (leer)", path);
        return 1;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(out, cap, "Fehler: Datei '%s' konnte nicht erstellt werden", path);
        return 1;
    }

    /* Inhalt: \n in echte Newlines umwandeln */
    const char *content = sep + 1;
    while (*content) {
        if (content[0] == '\\' && content[1] == 'n') {
            fputc('\n', f);
            content += 2;
        } else {
            fputc(*content++, f);
        }
    }
    fclose(f);
    snprintf(out, cap, "Datei '%s' erfolgreich erstellt", path);
    return 1;
}

/* ---- file_delete ----------------------------------------------------- */

static int tool_file_delete(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Pfad angegeben");
        return 1;
    }
    /* Sicherheit: kanonischen Pfad pruefen */
    if (!path_is_allowed(arg, 0)) {
        snprintf(out, cap,
                 "Fehler: Loeschen nur unter /home/user/ erlaubt (Systemdateien schuetzen)");
        return 1;
    }
    if (remove(arg) == 0) {
        snprintf(out, cap, "Datei '%s' geloescht", arg);
    } else {
        snprintf(out, cap, "Fehler: '%s' konnte nicht geloescht werden", arg);
    }
    return 1;
}

/* ---- file_rename ----------------------------------------------------- */
/* Datei umbenennen/verschieben. ARG: "altpfad|neupfad".
 * Quelle und Ziel muessen beide unter /home/user/ liegen (wie file_delete).
 * Ein bereits existierendes Ziel wird NICHT ueberschrieben (ehrlich, kein
 * stiller Datenverlust). */
static int tool_file_rename(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Pfad angegeben (Format: altpfad|neupfad)");
        return 1;
    }
    const char *sep = strchr(arg, '|');
    if (!sep || sep == arg || !sep[1]) {
        snprintf(out, cap,
                 "Fehler: Format ist 'altpfad|neupfad' (z.B. /home/user/a.txt|/home/user/b.txt)");
        return 1;
    }

    char src[512], dst[512];
    size_t slen = (size_t)(sep - arg);
    if (slen >= sizeof(src)) slen = sizeof(src) - 1;
    memcpy(src, arg, slen); src[slen] = '\0';
    snprintf(dst, sizeof(dst), "%s", sep + 1);

    /* Sicherheit: beide Pfade kanonisch unter /home/user/ */
    if (!path_is_allowed(src, 0) || !path_is_allowed(dst, 0)) {
        snprintf(out, cap,
                 "Fehler: Umbenennen nur unter /home/user/ erlaubt (Quelle und Ziel)");
        return 1;
    }

    struct stat st;
    if (stat(src, &st) != 0) {
        snprintf(out, cap, "Fehler: Quelle '%s' existiert nicht", src);
        return 1;
    }
    if (stat(dst, &st) == 0) {
        snprintf(out, cap,
                 "Fehler: Ziel '%s' existiert bereits -- wird nicht ueberschrieben", dst);
        return 1;
    }

    if (rename(src, dst) == 0) {
        snprintf(out, cap, "'%s' umbenannt nach '%s'", src, dst);
    } else {
        snprintf(out, cap, "Fehler: '%s' konnte nicht nach '%s' umbenannt werden", src, dst);
    }
    return 1;
}

/* ---- calculate ------------------------------------------------------- */

typedef struct { const char *s; } CalcParser;

static void calc_skip(CalcParser *p) {
    while (*p->s == ' ' || *p->s == '\t') p->s++;
}
static double calc_expr(CalcParser *p);
static double calc_term(CalcParser *p);
static double calc_factor(CalcParser *p);

static double calc_factor(CalcParser *p) {
    calc_skip(p);
    if (*p->s == '(') {
        p->s++;
        double v = calc_expr(p);
        calc_skip(p);
        if (*p->s == ')') p->s++;
        return v;
    }
    if (*p->s == '-') { p->s++; return -calc_factor(p); }
    char *end;
    double v = strtod(p->s, &end);
    if (end == p->s) return 0.0;
    p->s = end;
    return v;
}

static double calc_term(CalcParser *p) {
    double v = calc_factor(p);
    for (;;) {
        calc_skip(p);
        char op = *p->s;
        if (op != '*' && op != '/') break;
        p->s++;
        double r = calc_factor(p);
        v = (op == '*') ? v * r : (r != 0.0 ? v / r : 0.0);
    }
    return v;
}

static double calc_expr(CalcParser *p) {
    double v = calc_term(p);
    for (;;) {
        calc_skip(p);
        char op = *p->s;
        if (op != '+' && op != '-') break;
        p->s++;
        double r = calc_term(p);
        v = (op == '+') ? v + r : v - r;
    }
    return v;
}

static int tool_calculate(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Ausdruck angegeben");
        return 1;
    }
    CalcParser p = { .s = arg };
    double result = calc_expr(&p);
    /* Ganzzahl-Ergebnis ohne Kommastellen anzeigen */
    if (result == floor(result) && fabs(result) < 1e15)
        snprintf(out, cap, "%.0f", result);
    else
        snprintf(out, cap, "%.8g", result);
    return 1;
}

/* ---- note_save ------------------------------------------------------- */

static int tool_note_save(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Notiz-Text angegeben");
        return 1;
    }

    /* Sicherstellen, dass /etc/flux/ existiert */
    mkdir("/etc/flux", 0755);

    FILE *f = fopen(NOTES_PATH, "a");
    if (!f) {
        /* Fallback: /tmp/ */
        f = fopen("/tmp/flux_notes.txt", "a");
    }
    if (!f) {
        snprintf(out, cap, "Fehler: Notiz konnte nicht gespeichert werden");
        return 1;
    }

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &tm);
    fprintf(f, "[%s] %s\n", ts, arg);
    fclose(f);

    snprintf(out, cap, "Notiz gespeichert: \"%.*s\"",
             (int)(NOTE_TEXT_MAX < cap ? NOTE_TEXT_MAX : cap - 32), arg);
    return 1;
}

/* ---- note_list ------------------------------------------------------- */

static int tool_note_list(const char *arg, char *out, size_t cap) {
    (void)arg;
    const char *paths[] = { NOTES_PATH, "/tmp/flux_notes.txt" };
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "r");
        if (!f) continue;
        snprintf(out, cap, "Gespeicherte Notizen:\n");
        size_t pos = strlen(out);
        char line[256];
        while (fgets(line, sizeof(line), f) && pos + 2 < cap) {
            size_t ll = strlen(line);
            if (pos + ll + 1 < cap) {
                memcpy(out + pos, line, ll);
                pos += ll;
                out[pos] = '\0';
            }
        }
        fclose(f);
        if (pos == strlen("Gespeicherte Notizen:\n"))
            snprintf(out, cap, "Keine Notizen vorhanden.");
        return 1;
    }
    snprintf(out, cap, "Keine Notizen vorhanden.");
    return 1;
}

/* ---- note_search ----------------------------------------------------- */

static int tool_note_search(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Suchbegriff angegeben");
        return 1;
    }
    const char *paths[] = { NOTES_PATH, "/tmp/flux_notes.txt" };
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "r");
        if (!f) continue;
        char line[512];
        size_t pos = 0;
        int found = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strcasestr(line, arg) == NULL) continue;
            if (!found) {
                pos += snprintf(out + pos, cap - pos, "Notizen mit \"%s\":\n", arg);
                found++;
            }
            size_t ll = strlen(line);
            if (pos + ll + 1 < cap) {
                memcpy(out + pos, line, ll);
                pos += ll;
                out[pos] = '\0';
            }
        }
        fclose(f);
        if (!found)
            snprintf(out, cap, "Keine Notiz enthaelt \"%s\".", arg);
        return 1;
    }
    snprintf(out, cap, "Keine Notizen vorhanden.");
    return 1;
}

/* ---- note_delete ----------------------------------------------------- */

static int tool_note_delete(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Suchbegriff angegeben (loescht alle Notizen, die den Begriff enthalten)");
        return 1;
    }
    const char *path = NOTES_PATH;
    FILE *f = fopen(path, "r");
    if (!f) f = fopen("/tmp/flux_notes.txt", "r");
    if (!f) {
        snprintf(out, cap, "Keine Notizen vorhanden.");
        return 1;
    }
    char tmppath[256];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);
    FILE *tmp_f = fopen(tmppath, "w");
    if (!tmp_f) {
        fclose(f);
        snprintf(out, cap, "Fehler: temporaere Datei nicht schreibbar.");
        return 1;
    }
    char line[512];
    int deleted = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strcasestr(line, arg)) {
            deleted++;
        } else {
            fputs(line, tmp_f);
        }
    }
    fclose(f);
    fclose(tmp_f);
    if (rename(tmppath, path) != 0) {
        remove(tmppath);
        snprintf(out, cap, "Fehler: Datei konnte nicht aktualisiert werden.");
        return 1;
    }
    if (deleted == 0)
        snprintf(out, cap, "Keine Notiz mit \"%s\" gefunden.", arg);
    else
        snprintf(out, cap, "%d Notiz(en) mit \"%s\" geloescht.", deleted, arg);
    return 1;
}

/* ---- sys_info -------------------------------------------------------- */

static int tool_sys_info(const char *arg, char *out, size_t cap) {
    (void)arg;
    char tmp[2048];
    size_t pos = 0;
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "Flux OS - Systeminformation\n");

    /* /proc/version */
    FILE *f = fopen("/proc/version", "r");
    if (f) {
        char line[256];
        if (fgets(line, sizeof(line), f)) {
            /* Kuerzen: nur ersten Teil bis '(' zeigen */
            char *paren = strchr(line, '(');
            if (paren) *paren = '\0';
            pos += snprintf(tmp + pos, sizeof(tmp) - pos, "Kernel: %s\n", line);
        }
        fclose(f);
    }

    /* /proc/meminfo: MemTotal und MemAvailable */
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[128];
        long total_kb = 0, avail_kb = 0;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "MemTotal: %ld kB", &total_kb) == 1) {}
            if (sscanf(line, "MemAvailable: %ld kB", &avail_kb) == 1) {}
            if (total_kb && avail_kb) break;
        }
        fclose(f);
        if (total_kb)
            pos += snprintf(tmp + pos, sizeof(tmp) - pos,
                            "RAM: %ld MB gesamt, %ld MB frei\n",
                            total_kb / 1024, avail_kb / 1024);
    }

    /* Uptime */
    f = fopen("/proc/uptime", "r");
    if (f) {
        double uptime_s;
        if (fscanf(f, "%lf", &uptime_s) == 1) {
            long h = (long)(uptime_s / 3600);
            long m = (long)((uptime_s - h * 3600) / 60);
            pos += snprintf(tmp + pos, sizeof(tmp) - pos,
                            "Laufzeit: %ldh %ldm\n", h, m);
        }
        fclose(f);
    }

    /* CPU-Auslastung: zwei /proc/stat-Schnappschuesse, 200 ms Abstand */
    {
        unsigned long long u1=0, n1=0, s1=0, i1=0, w1=0, ir1=0, si1=0;
        unsigned long long u2=0, n2=0, s2=0, i2=0, w2=0, ir2=0, si2=0;
        FILE *sf = fopen("/proc/stat", "r");
        if (sf) {
            int _r1 = fscanf(sf, "cpu %llu %llu %llu %llu %llu %llu %llu",
                             &u1, &n1, &s1, &i1, &w1, &ir1, &si1);
            fclose(sf);
            (void)_r1;
            struct timespec ts = {0, 200000000L}; /* 200 ms */
            nanosleep(&ts, NULL);
            sf = fopen("/proc/stat", "r");
            if (sf) {
                int _r2 = fscanf(sf, "cpu %llu %llu %llu %llu %llu %llu %llu",
                                 &u2, &n2, &s2, &i2, &w2, &ir2, &si2);
                fclose(sf);
                (void)_r2;
                unsigned long long busy  = (u2+n2+s2+w2+ir2+si2) - (u1+n1+s1+w1+ir1+si1);
                unsigned long long total = busy + (i2 - i1);
                if (total > 0)
                    pos += snprintf(tmp + pos, sizeof(tmp) - pos,
                                    "CPU-Last: %llu%%\n", busy * 100 / total);
            }
        }
    }

    /* CPU-Temperatur aus /sys/class/thermal/thermal_zone*/
    {
        char best_path[128] = {0};
        DIR *td = opendir("/sys/class/thermal");
        if (td) {
            struct dirent *te;
            while ((te = readdir(td)) != NULL) {
                if (strncmp(te->d_name, "thermal_zone", 12) != 0) continue;
                char tpath[128];
                snprintf(tpath, sizeof(tpath), "/sys/class/thermal/%.80s/temp", te->d_name);
                if (!best_path[0]) snprintf(best_path, sizeof(best_path), "%s", tpath);
            }
            closedir(td);
        }
        if (best_path[0]) {
            FILE *tf = fopen(best_path, "r");
            if (tf) {
                long milli = 0;
                if (fscanf(tf, "%ld", &milli) == 1)
                    pos += snprintf(tmp + pos, sizeof(tmp) - pos,
                                    "CPU-Temperatur: %.1f degC\n", milli / 1000.0);
                fclose(tf);
            }
        } else {
            pos += snprintf(tmp + pos, sizeof(tmp) - pos,
                            "CPU-Temperatur: nicht lesbar (kein thermal_zone)\n");
        }
    }

    /* Netzwerk-IO: erstes Non-Loopback-Interface aus /proc/net/dev */
    {
        FILE *nf = fopen("/proc/net/dev", "r");
        if (nf) {
            char line[256];
            int lineno = 0;
            while (fgets(line, sizeof(line), nf)) {
                if (++lineno <= 2) continue; /* Header */
                char iface[64]; unsigned long long rx=0, tx=0;
                /* Format: "  eth0: <rx_bytes> ... <tx_bytes> ..." */
                if (sscanf(line, " %63[^:]: %llu %*u %*u %*u %*u %*u %*u %*u %llu",
                           iface, &rx, &tx) == 3) {
                    if (strcmp(iface, "lo") == 0) continue;
                    pos += snprintf(tmp + pos, sizeof(tmp) - pos,
                                    "Netz (%s): RX %llu KB / TX %llu KB\n",
                                    iface, rx / 1024, tx / 1024);
                    break;
                }
            }
            fclose(nf);
        }
    }

    snprintf(out, cap, "%s", tmp);
    return 1;
}

/* ---- alarm_set ------------------------------------------------------- */

static int tool_alarm_set(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: keine Zeit angegeben (Format: HH:MM oder HH:MM Beschreibung)");
        return 1;
    }
    FILE *f = fopen("/tmp/flux_alarms.txt", "a");
    if (!f) {
        snprintf(out, cap, "Fehler: Alarm konnte nicht gesetzt werden");
        return 1;
    }
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d", &tm);
    fprintf(f, "%s %s\n", ts, arg);
    fclose(f);
    snprintf(out, cap, "Alarm gesetzt fuer: %s", arg);
    return 1;
}

/* ---- alarm_list / alarm_delete --------------------------------------- */

static int tool_alarm_list(const char *arg, char *out, size_t cap) {
    (void)arg;
    FILE *f = fopen("/tmp/flux_alarms.txt", "r");
    if (!f) { snprintf(out, cap, "Keine Alarme gesetzt."); return 1; }
    size_t pos = snprintf(out, cap, "Gesetzte Alarme:\n");
    char line[256]; int n = 0;
    while (fgets(line, sizeof(line), f) && pos + 2 < cap) {
        size_t ll = strlen(line);
        if (pos + ll + 1 < cap) { memcpy(out + pos, line, ll); pos += ll; out[pos] = '\0'; }
        n++;
    }
    fclose(f);
    if (!n) snprintf(out, cap, "Keine Alarme gesetzt.");
    return 1;
}

static int tool_alarm_delete(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Suchbegriff angegeben");
        return 1;
    }
    const char *path = "/tmp/flux_alarms.txt";
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(out, cap, "Keine Alarme gesetzt."); return 1; }
    char tmppath[128]; snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);
    FILE *tf = fopen(tmppath, "w");
    if (!tf) { fclose(f); snprintf(out, cap, "Fehler: Datei nicht schreibbar."); return 1; }
    char line[256]; int deleted = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strcasestr(line, arg)) deleted++;
        else fputs(line, tf);
    }
    fclose(f); fclose(tf);
    if (rename(tmppath, path) != 0) { remove(tmppath); snprintf(out, cap, "Fehler beim Speichern."); return 1; }
    if (!deleted) snprintf(out, cap, "Kein Alarm mit \"%s\" gefunden.", arg);
    else          snprintf(out, cap, "%d Alarm(e) mit \"%s\" geloescht.", deleted, arg);
    return 1;
}

/* ---- timer_set / timer_list / timer_delete --------------------------- */

/* Parst eine Zeitdauer aus einem deutschen/englischen Ausdruck.
 * Gibt die Dauer in Sekunden zurueck (0 bei Fehler).
 * Beispiele: "5 Minuten", "30 Sekunden", "1 Stunde 30 Minuten",
 *            "5m", "30s", "1h30m", "90" (= 90 Sekunden). */
static int parse_duration_secs(const char *arg) {
    if (!arg || !*arg) return 0;
    int total = 0;
    const char *p = arg;
    while (*p) {
        /* Weiter bis zur naechsten Ziffer */
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) break;
        int val = 0;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        while (*p == ' ' || *p == '\t') p++;
        /* Einheit bestimmen (laengste Variante zuerst) */
        if      (strncasecmp(p, "stunden", 7) == 0 || strncasecmp(p, "stunde", 6) == 0 ||
                 strncasecmp(p, "hours",   5) == 0 || strncasecmp(p, "hour",   4) == 0) {
            total += val * 3600;
        } else if (strncasecmp(p, "minuten",  7) == 0 || strncasecmp(p, "minute",  6) == 0 ||
                   strncasecmp(p, "minutes",  7) == 0 || strncasecmp(p, "min",     3) == 0) {
            total += val * 60;
        } else if (strncasecmp(p, "sekunden", 8) == 0 || strncasecmp(p, "sekunde", 7) == 0 ||
                   strncasecmp(p, "seconds",  7) == 0 || strncasecmp(p, "second",  6) == 0 ||
                   strncasecmp(p, "sek",      3) == 0 || strncasecmp(p, "sec",     3) == 0) {
            total += val;
        } else if (*p == 'h' || *p == 'H') {
            total += val * 3600;
        } else if (*p == 'm' || *p == 'M') {
            total += val * 60;
        } else if (*p == 's' || *p == 'S') {
            total += val;
        } else {
            total += val; /* keine Einheit -> Sekunden */
        }
        /* Rest des aktuellen Tokens ueberspringen */
        while (*p && (*p < '0' || *p > '9')) p++;
    }
    return total;
}

static int tool_timer_set(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap,
            "Fehler: keine Dauer angegeben. "
            "Beispiele: '5 Minuten', '30 Sekunden', '1 Stunde 30 Minuten'");
        return 1;
    }
    int secs = parse_duration_secs(arg);
    if (secs <= 0) {
        snprintf(out, cap,
            "Fehler: Dauer nicht erkannt. "
            "Beispiele: '5 Minuten', '30s', '1h30m'");
        return 1;
    }
    if (secs > 86400 * 7) {
        snprintf(out, cap, "Fehler: Dauer zu lang (max. 7 Tage).");
        return 1;
    }
    time_t trigger_t = time(NULL) + (time_t)secs;
    FILE *f = fopen("/tmp/flux_timers.txt", "a");
    if (!f) { snprintf(out, cap, "Fehler: Timer konnte nicht gesetzt werden."); return 1; }
    fprintf(f, "%lld %s\n", (long long)trigger_t, arg);
    fclose(f);
    struct tm tm; localtime_r(&trigger_t, &tm);
    char ts[32]; strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
    if      (secs >= 3600)
        snprintf(out, cap, "Timer gesetzt (%dh%02dm) -- loest aus um %s.",
                 secs/3600, (secs%3600)/60, ts);
    else if (secs >= 60)
        snprintf(out, cap, "Timer gesetzt (%d Minuten %ds) -- loest aus um %s.",
                 secs/60, secs%60, ts);
    else
        snprintf(out, cap, "Timer gesetzt (%d Sekunden) -- loest aus um %s.", secs, ts);
    return 1;
}

static int tool_timer_list(const char *arg, char *out, size_t cap) {
    (void)arg;
    FILE *f = fopen("/tmp/flux_timers.txt", "r");
    if (!f) { snprintf(out, cap, "Keine aktiven Timer."); return 1; }
    size_t pos = snprintf(out, cap, "Aktive Timer:\n");
    char line[256]; int n = 0; time_t now = time(NULL);
    while (fgets(line, sizeof(line), f) && pos + 4 < cap) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (!line[0]) continue;
        long long ts = 0; int sc = 0;
        sscanf(line, "%lld%n", &ts, &sc);
        long long rem = (long long)ts - (long long)now;
        const char *desc = (sc > 0 && l > (size_t)sc + 1) ? line + sc + 1 : "Timer";
        char entry[160];
        if (rem <= 0)
            snprintf(entry, sizeof(entry), "  %s (faellig)\n", desc);
        else if (rem >= 3600)
            snprintf(entry, sizeof(entry), "  %s (noch %lluh%02llum)\n",
                     desc, rem/3600, (rem%3600)/60);
        else if (rem >= 60)
            snprintf(entry, sizeof(entry), "  %s (noch %llum%02llus)\n",
                     desc, rem/60, rem%60);
        else
            snprintf(entry, sizeof(entry), "  %s (noch %llus)\n", desc, rem);
        size_t el = strlen(entry);
        if (pos + el + 1 < cap) { memcpy(out + pos, entry, el); pos += el; out[pos] = '\0'; }
        n++;
    }
    fclose(f);
    if (!n) snprintf(out, cap, "Keine aktiven Timer.");
    return 1;
}

static int tool_timer_delete(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) { snprintf(out, cap, "Fehler: kein Suchbegriff angegeben"); return 1; }
    const char *path = "/tmp/flux_timers.txt";
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(out, cap, "Keine aktiven Timer."); return 1; }
    char tmppath[128]; snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);
    FILE *tf = fopen(tmppath, "w");
    if (!tf) { fclose(f); snprintf(out, cap, "Fehler: Datei nicht schreibbar."); return 1; }
    char line[256]; int deleted = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strcasestr(line, arg)) deleted++;
        else fputs(line, tf);
    }
    fclose(f); fclose(tf);
    if (rename(tmppath, path) != 0) { remove(tmppath); snprintf(out, cap, "Fehler beim Speichern."); return 1; }
    if (!deleted) snprintf(out, cap, "Kein Timer mit \"%s\" gefunden.", arg);
    else          snprintf(out, cap, "%d Timer mit \"%s\" geloescht.", deleted, arg);
    return 1;
}

/* ---- reminder_set ---------------------------------------------------- */

static int tool_reminder_set(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Erinnerungstext angegeben");
        return 1;
    }
    FILE *f = fopen("/tmp/flux_reminders.txt", "a");
    if (!f) {
        snprintf(out, cap, "Fehler: Erinnerung konnte nicht gespeichert werden");
        return 1;
    }
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &tm);
    fprintf(f, "[%s] %s\n", ts, arg);
    fclose(f);
    snprintf(out, cap, "Erinnerung gesetzt: \"%s\"", arg);
    return 1;
}

/* ---- reminder_list / reminder_delete --------------------------------- */

static int tool_reminder_list(const char *arg, char *out, size_t cap) {
    (void)arg;
    FILE *f = fopen("/tmp/flux_reminders.txt", "r");
    if (!f) { snprintf(out, cap, "Keine Erinnerungen gesetzt."); return 1; }
    size_t pos = snprintf(out, cap, "Gesetzte Erinnerungen:\n");
    char line[256]; int n = 0;
    while (fgets(line, sizeof(line), f) && pos + 2 < cap) {
        size_t ll = strlen(line);
        if (pos + ll + 1 < cap) { memcpy(out + pos, line, ll); pos += ll; out[pos] = '\0'; }
        n++;
    }
    fclose(f);
    if (!n) snprintf(out, cap, "Keine Erinnerungen gesetzt.");
    return 1;
}

static int tool_reminder_delete(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Suchbegriff angegeben");
        return 1;
    }
    const char *path = "/tmp/flux_reminders.txt";
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(out, cap, "Keine Erinnerungen gesetzt."); return 1; }
    char tmppath[128]; snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);
    FILE *tf = fopen(tmppath, "w");
    if (!tf) { fclose(f); snprintf(out, cap, "Fehler: Datei nicht schreibbar."); return 1; }
    char line[256]; int deleted = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strcasestr(line, arg)) deleted++;
        else fputs(line, tf);
    }
    fclose(f); fclose(tf);
    if (rename(tmppath, path) != 0) { remove(tmppath); snprintf(out, cap, "Fehler beim Speichern."); return 1; }
    if (!deleted) snprintf(out, cap, "Keine Erinnerung mit \"%s\" gefunden.", arg);
    else          snprintf(out, cap, "%d Erinnerung(en) mit \"%s\" geloescht.", deleted, arg);
    return 1;
}

/* ---- contacts_search ------------------------------------------------- */

static int tool_contacts_search(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Suchbegriff angegeben");
        return 1;
    }
    const char *contacts_path = "/etc/flux/contacts.txt";
    FILE *f = fopen(contacts_path, "r");
    if (!f) {
        snprintf(out, cap,
                 "Keine Kontaktdatei gefunden (%s). "
                 "Anlegen mit: Name,Telefon,Email (eine Zeile pro Kontakt).",
                 contacts_path);
        return 1;
    }
    char tmp[2048];
    size_t pos = snprintf(tmp, sizeof(tmp), "Suchergebnisse fuer \"%s\":\n", arg);
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f) && pos + 2 < sizeof(tmp)) {
        if (str_contains_ci(line, arg)) {
            size_t ll = strlen(line);
            if (pos + ll + 1 < sizeof(tmp)) {
                memcpy(tmp + pos, line, ll);
                pos += ll;
                tmp[pos] = '\0';
            }
            found++;
        }
    }
    fclose(f);
    if (!found)
        snprintf(out, cap, "Kein Kontakt fuer \"%s\" gefunden.", arg);
    else
        snprintf(out, cap, "%s", tmp);
    return 1;
}

/* ---- brightness_get -------------------------------------------------- */

/*
 * Hilfsfunktion: erstes Backlight-Verzeichnis unter /sys/class/backlight/
 * suchen und den Pfad ohne trailing '/' in buf schreiben.
 * Gibt 1 zurueck wenn gefunden, 0 sonst.
 */
static int find_backlight_dir(char *buf, size_t cap) {
    /* Bevorzugte Namen in Reihenfolge probieren */
    static const char *known[] = {
        "/sys/class/backlight/backlight",
        "/sys/class/backlight/lcd-backlight",
        "/sys/class/backlight/intel_backlight",
        NULL
    };
    for (int i = 0; known[i]; i++) {
        char probe[256];
        snprintf(probe, sizeof(probe), "%s/brightness", known[i]);
        FILE *f = fopen(probe, "r");
        if (f) {
            fclose(f);
            snprintf(buf, cap, "%s", known[i]);
            return 1;
        }
    }
    /* Fallback: ersten Eintrag aus opendir nehmen */
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(buf, cap, "/sys/class/backlight/%.200s", e->d_name);
        closedir(d);
        return 1;
    }
    closedir(d);
    return 0;
}

static int tool_brightness_get(const char *arg, char *out, size_t cap) {
    (void)arg;
    char dir[256];
    if (!find_backlight_dir(dir, sizeof(dir))) {
        snprintf(out, cap, "Helligkeitssteuerung nicht verfuegbar auf diesem Geraet");
        return 1;
    }
    char path_cur[280], path_max[280];
    snprintf(path_cur, sizeof(path_cur), "%s/brightness",     dir);
    snprintf(path_max, sizeof(path_max), "%s/max_brightness", dir);

    long cur = 0, max = 0;
    FILE *f = fopen(path_cur, "r");
    if (f) { if (fscanf(f, "%ld", &cur) != 1) cur = 0; fclose(f); }
    else {
        snprintf(out, cap, "Fehler: brightness-Datei nicht lesbar (%s)", path_cur);
        return 1;
    }
    f = fopen(path_max, "r");
    if (f) { if (fscanf(f, "%ld", &max) != 1) max = 0; fclose(f); }

    if (max <= 0) {
        snprintf(out, cap, "Aktuelle Helligkeit: %ld (max unbekannt)", cur);
        return 1;
    }
    int pct = (int)(cur * 100 / max);
    snprintf(out, cap, "Helligkeit: %d%% (Rohwert %ld von %ld, Quelle: %s)",
             pct, cur, max, dir);
    return 1;
}

/* ---- brightness_set -------------------------------------------------- */

static int tool_brightness_set(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Wert angegeben (0-100)");
        return 1;
    }
    char *end;
    long pct = strtol(arg, &end, 10);
    if (end == arg || pct < 0 || pct > 100) {
        snprintf(out, cap, "Fehler: Wert muss zwischen 0 und 100 liegen (angegeben: '%s')", arg);
        return 1;
    }
    char dir[256];
    if (!find_backlight_dir(dir, sizeof(dir))) {
        snprintf(out, cap, "Helligkeitssteuerung nicht verfuegbar auf diesem Geraet");
        return 1;
    }
    char path_cur[280], path_max[280];
    snprintf(path_cur, sizeof(path_cur), "%s/brightness",     dir);
    snprintf(path_max, sizeof(path_max), "%s/max_brightness", dir);

    long max = 0;
    FILE *f = fopen(path_max, "r");
    if (f) { if (fscanf(f, "%ld", &max) != 1) max = 0; fclose(f); }
    if (max <= 0) {
        snprintf(out, cap, "Fehler: max_brightness nicht lesbar (%s)", path_max);
        return 1;
    }
    long raw = (long)(pct * max / 100);
    f = fopen(path_cur, "w");
    if (!f) {
        snprintf(out, cap, "Fehler: brightness-Datei nicht schreibbar (%s) -- Root-Rechte noetig?", path_cur);
        return 1;
    }
    fprintf(f, "%ld\n", raw);
    fclose(f);
    snprintf(out, cap, "Helligkeit auf %ld%% gesetzt (Rohwert %ld von %ld)", pct, raw, max);
    return 1;
}

/* ---- wifi_info ------------------------------------------------------- */

static int tool_wifi_info(const char *arg, char *out, size_t cap) {
    (void)arg;
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) {
        snprintf(out, cap, "Kein WLAN verfuegbar (oder /proc/net/wireless nicht lesbar)");
        return 1;
    }
    /* Die ersten zwei Zeilen sind Header, danach kommt eine Zeile pro Interface:
     *   wlan0: 0000   65.  -45.  -95.   0.      0      0    0     0     0
     * Felder: status, link, level, noise, ... */
    char line[256];
    int lineno = 0;
    char tmp[1024];
    size_t pos = 0;
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        if (lineno <= 2) continue; /* Header ueberspringen */
        /* Interface-Name endet mit ':' */
        char iface[64] = {0};
        int status = 0;
        int link = 0;
        int level = 0, noise = 0;
        /* Format: "  wlan0: 0000   65.  -45.  -95." */
        if (sscanf(line, " %63[^:]: %d %d. %d. %d.",
                   iface, &status, &link, &level, &noise) >= 3) {
            pos += snprintf(tmp + pos, sizeof(tmp) - pos,
                            "Interface: %s  Verbindungsqualitaet: %d%%  "
                            "Signalpegel: %d dBm  Rauschen: %d dBm\n",
                            iface, link, level, noise);
            found++;
        }
    }
    fclose(f);
    if (!found) {
        snprintf(out, cap, "Kein WLAN-Interface aktiv");
    } else {
        tmp[pos > 0 ? pos - 1 : 0] = '\0'; /* letztes \n entfernen */
        snprintf(out, cap, "%s", tmp);
    }
    return 1;
}

/* ---- wifi_on / wifi_off ---------------------------------------------- */

static const char *find_rfkill(void) {
    static const char *paths[] = {
        "/usr/sbin/rfkill", "/sbin/rfkill",
        "/usr/bin/rfkill",  "/usr/local/sbin/rfkill",
        NULL
    };
    for (int i = 0; paths[i]; i++)
        if (access(paths[i], X_OK) == 0) return paths[i];
    return NULL;
}

/* Fuehrt "rfkill <rfkill_arg>" aus und formatiert das Ergebnis einheitlich.
 * Gemeinsame Basis fuer wifi_on/off und flight_mode_on/off -- die vier
 * Tools unterschieden sich zuvor nur in rfkill-Argument und Meldungstexten. */
static int rfkill_run(const char *rfkill_arg, const char *unavailable_msg,
                       const char *ok_msg, const char *err_fmt,
                       char *out, size_t cap) {
    const char *rfkill = find_rfkill();
    if (!rfkill) {
        snprintf(out, cap, "%s", unavailable_msg);
        return 1;
    }
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s %s 2>/dev/null", rfkill, rfkill_arg);
    int rc = system(cmd);
    if (rc == 0)
        snprintf(out, cap, "%s", ok_msg);
    else
        snprintf(out, cap, err_fmt, rc);
    return 1;
}

static int tool_wifi_on(const char *arg, char *out, size_t cap) {
    (void)arg;
    return rfkill_run("unblock wifi",
        "WLAN-Steuerung nicht verfuegbar: 'rfkill' nicht gefunden "
        "(auf QEMU ohne WLAN-Hardware kein rfkill vorhanden).",
        "WLAN eingeschaltet.",
        "Fehler beim Einschalten des WLANs (rfkill Rueckgabewert %d). "
        "Kein WLAN-Hardware vorhanden?", out, cap);
}

static int tool_wifi_off(const char *arg, char *out, size_t cap) {
    (void)arg;
    return rfkill_run("block wifi",
        "WLAN-Steuerung nicht verfuegbar: 'rfkill' nicht gefunden "
        "(auf QEMU ohne WLAN-Hardware kein rfkill vorhanden).",
        "WLAN ausgeschaltet.",
        "Fehler beim Ausschalten des WLANs (rfkill Rueckgabewert %d). "
        "Kein WLAN-Hardware vorhanden?", out, cap);
}

/* ---- flight_mode_on / flight_mode_off -------------------------------- */

static int tool_flight_mode_on(const char *arg, char *out, size_t cap) {
    (void)arg;
    return rfkill_run("block all",
        "Flugmodus nicht verfuegbar: 'rfkill' nicht gefunden "
        "(auf QEMU ohne Funk-Hardware kein rfkill vorhanden).",
        "Flugmodus aktiviert (alle Funkschnittstellen gesperrt: WLAN, Bluetooth, Mobilfunk).",
        "Fehler beim Aktivieren des Flugmodus (rfkill Rueckgabewert %d).", out, cap);
}

static int tool_flight_mode_off(const char *arg, char *out, size_t cap) {
    (void)arg;
    return rfkill_run("unblock all",
        "Flugmodus nicht verfuegbar: 'rfkill' nicht gefunden "
        "(auf QEMU ohne Funk-Hardware kein rfkill vorhanden).",
        "Flugmodus deaktiviert (alle Funkschnittstellen freigegeben).",
        "Fehler beim Deaktivieren des Flugmodus (rfkill Rueckgabewert %d).", out, cap);
}

/* ---- pin_set --------------------------------------------------------- */

static int tool_pin_set(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: Kein PIN angegeben. Bitte 4-8 Ziffern angeben.");
        return 1;
    }
    /* Nur Ziffern, Laenge 4-8 */
    size_t len = strlen(arg);
    if (len < 4 || len > 8) {
        snprintf(out, cap,
            "Fehler: PIN muss 4 bis 8 Ziffern lang sein (angegeben: %zu Zeichen).", len);
        return 1;
    }
    for (size_t i = 0; i < len; i++) {
        if (arg[i] < '0' || arg[i] > '9') {
            snprintf(out, cap, "Fehler: PIN darf nur Ziffern enthalten.");
            return 1;
        }
    }
    char hash[65];
    flux_sha256_hex(arg, hash);
    if (flux_config_set("pin_hash", hash) != 0) {
        snprintf(out, cap,
            "Fehler: PIN konnte nicht gespeichert werden (/etc/flux/flux.conf nicht schreibbar).");
        return 1;
    }
    snprintf(out, cap, "PIN wurde geaendert. Der neue PIN ist sofort aktiv.");
    return 1;
}

/* ---- vibrate --------------------------------------------------------- */

static int tool_vibrate(const char *arg, char *out, size_t cap) {
    long ms = 300; /* Standardwert */
    if (arg && *arg) {
        char *end;
        long v = strtol(arg, &end, 10);
        if (end != arg && v > 0) ms = v;
    }

    /* Sysfs-Knoten in bevorzugter Reihenfolge probieren */
    struct { const char *path; const char *value_fmt; } nodes[] = {
        /* Android-Kernel: Wert = Dauer in ms */
        { "/sys/class/timed_output/vibrator/enable",  "%ld"  },
        /* Neuere Kernels: "1" schreiben zum Aktivieren */
        { "/sys/class/leds/vibrator/activate",        "1"    },
        { NULL, NULL }
    };

    for (int i = 0; nodes[i].path; i++) {
        FILE *f = fopen(nodes[i].path, "w");
        if (!f) continue;
        if (strcmp(nodes[i].value_fmt, "%ld") == 0)
            fprintf(f, "%ld\n", ms);
        else
            fprintf(f, "%s\n", nodes[i].value_fmt);
        fclose(f);
        snprintf(out, cap, "Vibration ausgeloest fuer %ld ms (Knoten: %s)",
                 ms, nodes[i].path);
        return 1;
    }

    snprintf(out, cap, "Vibration nicht verfuegbar auf diesem Geraet");
    return 1;
}

/* ---- flight_mode ----------------------------------------------------- */
/* NUR Status abfragen (read-only). Das tatsaechliche Schalten laeuft ueber
 * den Bestaetigungs-Dialog (ACTION:flight -> exec.c -> radio.c), weil das
 * Kappen der Konnektivitaet Aussenwirkung hat und nie ohne Bestaetigung
 * passieren darf. Backend: radio.c (austauschbar). */
static int tool_flight_mode(const char *arg, char *out, size_t cap) {
    (void)arg;
    flux_radio_status(out, cap);
    return 1;
}

/* ---- memory_save ----------------------------------------------------- */
static int tool_memory_save(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Text angegeben");
        return 1;
    }
    mkdir("/etc/flux", 0755);
    FILE *f = fopen(MEMORY_PATH, "a");
    if (!f) {
        snprintf(out, cap, "Fehler: Erinnerung konnte nicht gespeichert werden");
        return 1;
    }
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &tm);
    char entry[512]; snprintf(entry, sizeof(entry), "%s", arg);
    size_t l = strlen(entry);
    while (l > 0 && (entry[l-1] == '\n' || entry[l-1] == '\r')) entry[--l] = '\0';
    fprintf(f, "[%s] %s\n", ts, entry);
    fclose(f);
    /* If birthday mentioned: auto-add a yearly calendar reminder */
    char lower[512];
    flux_str_tolower_ascii(lower, sizeof(lower), entry);
    if (strstr(lower, "geburtstag") || strstr(lower, "birthday")) {
        FILE *cf = fopen("/etc/flux/calendar.txt", "a");
        if (cf) {
            /* Store as a placeholder birthday reminder */
            fprintf(cf, "# Geburtstag-Erinnerung: %s\n", entry);
            fclose(cf);
        }
    }
    snprintf(out, cap, "Notiert: \"%s\"", entry);
    return 1;
}

/* ---- memory_list ----------------------------------------------------- */
static int tool_memory_list(const char *arg, char *out, size_t cap) {
    (void)arg;
    FILE *f = fopen(MEMORY_PATH, "r");
    if (!f) {
        snprintf(out, cap, "Noch nichts gespeichert. Sage mir z.B. deinen Namen, "
                           "Geburtstag von Kontakten, oder Praeferenzen.");
        return 1;
    }
    char tmp[4096]; size_t pos = snprintf(tmp, sizeof(tmp), "KI-Gedaechtnis:\n");
    char line[256]; int n = 0;
    while (fgets(line, sizeof(line), f) && pos + 2 < sizeof(tmp)) {
        if (line[0] == '\n') continue;
        size_t ll = strlen(line);
        if (pos + ll + 1 < sizeof(tmp)) { memcpy(tmp + pos, line, ll); pos += ll; tmp[pos] = '\0'; }
        n++;
    }
    fclose(f);
    if (!n) snprintf(out, cap, "KI-Gedaechtnis ist leer.");
    else    snprintf(out, cap, "%s", tmp);
    return 1;
}

/* ---- memory_search --------------------------------------------------- */
static int tool_memory_search(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) { snprintf(out, cap, "Fehler: kein Suchbegriff"); return 1; }
    FILE *f = fopen(MEMORY_PATH, "r");
    if (!f) { snprintf(out, cap, "Keine Erinnerungen vorhanden."); return 1; }
    char tmp[4096]; size_t pos = snprintf(tmp, sizeof(tmp), "Erinnerungen zu \"%s\":\n", arg);
    char line[256]; int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (str_contains_ci(line, arg)) {
            size_t ll = strlen(line);
            if (pos + ll + 1 < sizeof(tmp)) { memcpy(tmp + pos, line, ll); pos += ll; tmp[pos] = '\0'; }
            found++;
        }
    }
    fclose(f);
    if (!found) snprintf(out, cap, "Keine Erinnerungen zu \"%s\" gefunden.", arg);
    else        snprintf(out, cap, "%s", tmp);
    return 1;
}

/* ---- memory_delete --------------------------------------------------- */
static int tool_memory_delete(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) { snprintf(out, cap, "Fehler: kein Suchbegriff angegeben"); return 1; }
    FILE *f = fopen(MEMORY_PATH, "r");
    if (!f) { snprintf(out, cap, "Keine Erinnerungen vorhanden."); return 1; }
    char lines[200][256]; int n = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) && n < 200) {
        memcpy(lines[n++], line, sizeof(lines[0])-1);
        lines[n-1][sizeof(lines[0])-1] = '\0';
    }
    fclose(f);
    f = fopen(MEMORY_PATH, "w");
    if (!f) { snprintf(out, cap, "Fehler: Datei nicht schreibbar"); return 1; }
    int deleted = 0;
    for (int i = 0; i < n; i++) {
        if (str_contains_ci(lines[i], arg)) { deleted++; }
        else { fputs(lines[i], f); }
    }
    fclose(f);
    if (deleted > 0) snprintf(out, cap, "%d Erinnerung(en) zu \"%s\" geloescht.", deleted, arg);
    else             snprintf(out, cap, "Keine passenden Erinnerungen gefunden.");
    return 1;
}

/* ---- contact_save ---------------------------------------------------- */
static int tool_contact_save(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Kontakt angegeben (Format: Name,Telefon,Email[,Geburtstag])");
        return 1;
    }
    mkdir("/etc/flux", 0755);
    FILE *f = fopen("/etc/flux/contacts.txt", "a");
    if (!f) {
        snprintf(out, cap, "Fehler: Kontakt konnte nicht gespeichert werden");
        return 1;
    }
    char entry[256];
    snprintf(entry, sizeof(entry), "%s", arg);
    size_t l = strlen(entry);
    while (l > 0 && (entry[l-1] == '\n' || entry[l-1] == '\r')) entry[--l] = '\0';
    fprintf(f, "%s\n", entry);
    fclose(f);
    /* If birthday field (4th CSV column) present, add annual calendar reminder */
    const char *p1 = strchr(entry, ',');
    const char *p2 = p1 ? strchr(p1+1, ',') : NULL;
    const char *p3 = p2 ? strchr(p2+1, ',') : NULL;
    if (p3 && *(p3+1)) {
        const char *birthday = p3 + 1;
        /* Get name (first field) */
        char name[64]; size_t nl = (size_t)(p1 - entry);
        if (nl >= sizeof(name)) nl = sizeof(name)-1;
        memcpy(name, entry, nl); name[nl] = '\0';
        FILE *cf = fopen("/etc/flux/calendar.txt", "a");
        if (cf) {
            /* birthday format may be DD.MM.YYYY or MM-DD or YYYY-MM-DD */
            fprintf(cf, "# Geburtstag %s: %s\n", name, birthday);
            /* Try to produce an ISO date for this year */
            int dd = 0, mm = 0, yyyy = 0;
            if (sscanf(birthday, "%d.%d.%d", &dd, &mm, &yyyy) >= 2 ||
                sscanf(birthday, "%d-%d-%d", &yyyy, &mm, &dd) == 3) {
                time_t now = time(NULL); struct tm tmnow; localtime_r(&now, &tmnow);
                int cur_year = tmnow.tm_year + 1900;
                if (yyyy < 1900 || yyyy > 9999) yyyy = cur_year;
                char cal_entry[256];
                snprintf(cal_entry, sizeof(cal_entry),
                         "%04d-%02d-%02d 00:00 Geburtstag: %s", yyyy, mm, dd, name);
                fprintf(cf, "%s\n", cal_entry);
            }
            fclose(cf);
        }
        snprintf(out, cap, "Kontakt gespeichert: %s (Geburtstag: %s in Kalender eingetragen)", name, birthday);
    } else {
        snprintf(out, cap, "Kontakt gespeichert: %s", entry);
    }
    return 1;
}

/* ---- contacts_list --------------------------------------------------- */
static int tool_contacts_list(const char *arg, char *out, size_t cap) {
    (void)arg;
    FILE *f = fopen("/etc/flux/contacts.txt", "r");
    if (!f) {
        snprintf(out, cap, "Keine Kontakte gespeichert. "
                 "Speichere mit: Speichere Kontakt Name,Telefon,Email");
        return 1;
    }
    char tmp[4096];
    size_t pos = snprintf(tmp, sizeof(tmp), "Gespeicherte Kontakte:\n");
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), f) && pos + 2 < sizeof(tmp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        size_t ll = strlen(line);
        if (pos + ll + 1 < sizeof(tmp)) {
            memcpy(tmp + pos, line, ll);
            pos += ll;
            tmp[pos] = '\0';
        }
        n++;
    }
    fclose(f);
    if (!n) snprintf(out, cap, "Keine Kontakte vorhanden.");
    else snprintf(out, cap, "%s", tmp);
    return 1;
}

/* ---- calendar_add ---------------------------------------------------- */
static int tool_calendar_add(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Termin angegeben (Format: YYYY-MM-DD HH:MM Beschreibung)");
        return 1;
    }
    mkdir("/etc/flux", 0755);
    FILE *f = fopen("/etc/flux/calendar.txt", "a");
    if (!f) {
        snprintf(out, cap, "Fehler: Termin konnte nicht gespeichert werden");
        return 1;
    }
    char entry[256];
    snprintf(entry, sizeof(entry), "%s", arg);
    size_t l = strlen(entry);
    while (l > 0 && (entry[l-1] == '\n' || entry[l-1] == '\r')) entry[--l] = '\0';
    /* If no date prefix given, prepend today. l>=8 zuerst pruefen --
     * sonst wuerden entry[4]/entry[7] bei kurzen Eintraegen unbelegten
     * Stack-Inhalt hinter dem NUL-Terminator lesen. */
    if (l < 8 || !(entry[4] == '-' && entry[7] == '-')) {
        time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
        char dated[256];
        strftime(dated, sizeof(dated), "%Y-%m-%d ", &tm);
        snprintf(entry, sizeof(entry), "%s%s", dated, arg);
        l = strlen(entry);
        while (l > 0 && (entry[l-1] == '\n' || entry[l-1] == '\r')) entry[--l] = '\0';
    }
    fprintf(f, "%s\n", entry);
    fclose(f);
    snprintf(out, cap, "Termin eingetragen: %s", entry);
    return 1;
}

/* ---- calendar_list --------------------------------------------------- */
static int tool_calendar_list(const char *arg, char *out, size_t cap) {
    (void)arg;
    FILE *f = fopen("/etc/flux/calendar.txt", "r");
    if (!f) {
        snprintf(out, cap, "Keine Termine gespeichert. "
                 "Termin hinzufuegen: YYYY-MM-DD HH:MM Beschreibung");
        return 1;
    }
    time_t now = time(NULL);
    struct tm tm_now; localtime_r(&now, &tm_now);
    char today[12]; strftime(today, sizeof(today), "%Y-%m-%d", &tm_now);

    char tmp[4096];
    size_t pos = snprintf(tmp, sizeof(tmp), "Bevorstehende Termine:\n");
    char line[256]; int n = 0;
    while (fgets(line, sizeof(line), f) && n < 10) {
        if (line[0] == '#' || line[0] == '\n') continue;
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r')) line[--ll] = '\0';
        if (!line[0]) continue;
        /* Only show events from today onward (simple string compare works for ISO dates) */
        if (strncmp(line, today, 10) < 0) continue;
        if (pos + ll + 2 < sizeof(tmp)) {
            tmp[pos++] = ' '; tmp[pos++] = ' ';
            memcpy(tmp + pos, line, ll); pos += ll;
            tmp[pos++] = '\n'; tmp[pos] = '\0';
        }
        n++;
    }
    fclose(f);
    if (!n) { snprintf(out, cap, "Keine bevorstehenden Termine."); }
    else    { snprintf(out, cap, "%s", tmp); }
    return 1;
}

/* ---- calendar_delete ------------------------------------------------- */
static int tool_calendar_delete(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap,
            "Fehler: kein Suchbegriff angegeben (loescht alle Termine, die den Begriff enthalten)");
        return 1;
    }
    const char *path = "/etc/flux/calendar.txt";
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(out, cap, "Keine Termine gespeichert.");
        return 1;
    }
    char tmppath[256];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);
    FILE *tmp_f = fopen(tmppath, "w");
    if (!tmp_f) {
        fclose(f);
        snprintf(out, cap, "Fehler: temporaere Datei nicht schreibbar.");
        return 1;
    }
    char line[512];
    int deleted = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') { fputs(line, tmp_f); continue; } /* Kommentare behalten */
        if (strcasestr(line, arg)) {
            deleted++;
        } else {
            fputs(line, tmp_f);
        }
    }
    fclose(f);
    fclose(tmp_f);
    if (rename(tmppath, path) != 0) {
        remove(tmppath);
        snprintf(out, cap, "Fehler: Kalender-Datei konnte nicht aktualisiert werden.");
        return 1;
    }
    if (deleted == 0)
        snprintf(out, cap, "Kein Termin mit \"%s\" gefunden.", arg);
    else
        snprintf(out, cap, "%d Termin(e) mit \"%s\" geloescht.", deleted, arg);
    return 1;
}

/* ---- search_files ---------------------------------------------------- */
static void search_files_walk(const char *base, const char *pattern,
                               char *out, size_t cap, int *count) {
    if (*count >= 20) return;
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && *count < 20) {
        if (e->d_name[0] == '.') continue;
        char full[1280];
        snprintf(full, sizeof(full), "%s/%s", base, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (str_contains_ci(e->d_name, pattern)) {
            size_t ol = strlen(out);
            snprintf(out + ol, cap - ol, "  %s\n", full);
            (*count)++;
        }
        if (S_ISDIR(st.st_mode))
            search_files_walk(full, pattern, out, cap, count);
    }
    closedir(d);
}

static int tool_search_files(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Suchbegriff angegeben");
        return 1;
    }
    snprintf(out, cap, "Suche nach \"%s\" in /home/user/:\n", arg);
    int count = 0;
    search_files_walk("/home/user", arg, out, cap, &count);
    if (!count) {
        size_t l = strlen(out);
        snprintf(out + l, cap - l, "  (keine Treffer)");
    }
    return 1;
}

/* ---- prefs_set ------------------------------------------------------- */
static int tool_prefs_set(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: keine Praeferenz angegeben");
        return 1;
    }
    mkdir("/etc/flux", 0755);
    FILE *f = fopen("/etc/flux/prefs.txt", "a");
    if (!f) {
        snprintf(out, cap, "Fehler: Praeferenz konnte nicht gespeichert werden");
        return 1;
    }
    char entry[256]; snprintf(entry, sizeof(entry), "%s", arg);
    size_t l = strlen(entry);
    while (l > 0 && (entry[l-1] == '\n' || entry[l-1] == '\r')) entry[--l] = '\0';
    fprintf(f, "%s\n", entry);
    fclose(f);
    snprintf(out, cap, "Praeferenz gespeichert: %s", entry);
    return 1;
}

/* ---- image_list ------------------------------------------------------- */
static int tool_image_list(const char *arg, char *out, size_t cap) {
    (void)arg;
    const char *dir = "/home/user/Pictures";
    DIR *d = opendir(dir);
    if (!d) {
        snprintf(out, cap, "Keine Fotos gefunden (Verzeichnis existiert nicht).");
        return 1;
    }
    struct dirent *e;
    size_t pos = 0;
    int count = 0;
    pos += (size_t)snprintf(out + pos, cap - pos, "Fotos in %s:\n", dir);
    while ((e = readdir(d)) != NULL && count < 50) {
        size_t nl = strlen(e->d_name);
        if (nl < 4) continue;
        const char *ext = e->d_name + nl - 4;
        if (strcmp(ext, ".ppm") != 0 && strcmp(ext, ".jpg") != 0 &&
            strcmp(ext, ".png") != 0) continue;
        if (pos + nl + 4 >= cap) break;
        pos += (size_t)snprintf(out + pos, cap - pos, "  %s\n", e->d_name);
        count++;
    }
    closedir(d);
    if (count == 0) snprintf(out, cap, "Noch keine Fotos vorhanden.");
    return 1;
}

/* ---- image_analyze ---------------------------------------------------- */
static int tool_image_analyze(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Bildpfad angegeben.");
        return 1;
    }
    /* Relativen Pfad in /home/user/Pictures/ aufloesen */
    char full[512];
    if (arg[0] == '/') {
        snprintf(full, sizeof(full), "%s", arg);
    } else {
        snprintf(full, sizeof(full), "/home/user/Pictures/%s", arg);
    }
    if (access(full, R_OK) != 0) {
        snprintf(out, cap, "Bilddatei nicht gefunden: %s", full);
        return 1;
    }
    return flux_vision_analyze(full, out, cap, NULL);
}

/* ---- plant_identify (Pl@ntNet) --------------------------------------- */
static int tool_plant_identify(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Bildpfad angegeben.");
        return 1;
    }
    /* Relativen Pfad in /home/user/Pictures/ aufloesen (wie image_analyze). */
    char full[512];
    if (arg[0] == '/') snprintf(full, sizeof(full), "%s", arg);
    else               snprintf(full, sizeof(full), "/home/user/Pictures/%s", arg);
    flux_plant_identify(full, out, cap);  /* setzt out immer (auch im Fehlerfall) */
    return 1;
}

/* ---- logo_detect (Google Cloud Vision) ------------------------------- */
static int tool_logo_detect(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Bildpfad angegeben.");
        return 1;
    }
    char full[512];
    if (arg[0] == '/') snprintf(full, sizeof(full), "%s", arg);
    else               snprintf(full, sizeof(full), "/home/user/Pictures/%s", arg);
    flux_logo_detect(full, out, cap);  /* setzt out immer (auch im Fehlerfall) */
    return 1;
}

/* ---- image_take ------------------------------------------------------- */
static int tool_image_take(const char *arg, char *out, size_t cap) {
    (void)arg;
    /* Einfache Testmuster-Aufnahme (kein V4L2 in fluxaid -- das laeuft im Shell) */
    mkdir("/home/user", 0755);
    mkdir("/home/user/Pictures", 0755);
    time_t t = time(NULL);
    struct tm tmv; localtime_r(&t, &tmv);
    char path[256];
    strftime(path, sizeof(path), "/home/user/Pictures/IMG_%Y%m%d_%H%M%S.ppm", &tmv);

    /* Testmuster als PPM erzeugen */
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(out, cap, "Fehler beim Speichern des Fotos."); return 1; }
    int W = 320, H = 320;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int r, g, b;
            if (y < H * 2 / 5) {
                r = 80  + y * 60 / (H * 2 / 5);
                g = 140 + y * 50 / (H * 2 / 5);
                b = 220;
            } else {
                r = 80;  g = 110; b = 40;
                if ((x * 7 + y * 13) % 20 == 0) { r -= 15; g -= 15; }
            }
            fputc((unsigned char)(r < 0 ? 0 : r > 255 ? 255 : r), f);
            fputc((unsigned char)(g < 0 ? 0 : g > 255 ? 255 : g), f);
            fputc((unsigned char)(b < 0 ? 0 : b > 255 ? 255 : b), f);
        }
    }
    fclose(f);
    snprintf(out, cap, "Foto aufgenommen und gespeichert: %s", path);
    return 1;
}

/* ---- mail_unread / mail_read (IMAP) ---------------------------------- */
static int tool_mail_unread(const char *arg, char *out, size_t cap) {
    (void)arg;
    flux_imap_fetch_unread(out, cap);
    return 1;
}
static int tool_mail_read(const char *arg, char *out, size_t cap) {
    flux_imap_read(arg, out, cap);
    return 1;
}

/* ---- web_search (SearXNG) -------------------------------------------- */

/* Liest den String-Wert von "key" innerhalb von [s, end) nach out.
 * Gibt 1 bei Erfolg. Dekodiert die wichtigsten JSON-Escapes. */
static int json_field(const char *s, const char *end, const char *key,
                      char *out, size_t cap) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(s, pat);
    if (!p || p >= end) return 0;
    p += strlen(pat);
    while (p < end && (*p == ' ' || *p == ':')) p++;
    if (p >= end || *p != '"') return 0;
    p++;
    size_t o = 0;
    while (p < end && *p != '"' && o + 1 < cap) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            switch (*p) {
                case 'n': case 't': case 'r': out[o++] = ' '; break;
                case 'u':
                    /* \uXXXX -> UTF-8 (BMP), damit Umlaute/Striche korrekt
                     * bei der KI ankommen */
                    if (p + 4 < end) {
                        char hx[5] = { p[1], p[2], p[3], p[4], 0 };
                        unsigned v = (unsigned)strtol(hx, NULL, 16);
                        if (v < 0x80) {
                            out[o++] = (char)v;
                        } else if (v < 0x800 && o + 2 < cap) {
                            out[o++] = (char)(0xC0 | (v >> 6));
                            out[o++] = (char)(0x80 | (v & 0x3F));
                        } else if (o + 3 < cap) {
                            out[o++] = (char)(0xE0 | (v >> 12));
                            out[o++] = (char)(0x80 | ((v >> 6) & 0x3F));
                            out[o++] = (char)(0x80 | (v & 0x3F));
                        }
                        p += 4;
                    }
                    break;
                default: out[o++] = *p; break;
            }
        } else {
            out[o++] = *p;
        }
        p++;
    }
    out[o] = '\0';
    return 1;
}

/* Parst die SearXNG-JSON-Antwort und schreibt bis zu max_results Treffer
 * (Titel/URL/Auszug) als lesbaren Text nach out. Gibt die Trefferzahl. */
static int parse_searxng(const char *resp, char *out, size_t cap, int max_results) {
    const char *p = strstr(resp, "\"results\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;

    int n = 0;
    while (*p && n < max_results) {
        while (*p && *p != '{' && *p != ']') p++;
        if (*p != '{') break;

        /* zugehoeriges '}' finden -- String-bewusst (Klammern in Werten ignorieren) */
        const char *obj = p, *q = p;
        int depth = 0, instr = 0;
        for (; *q; q++) {
            if (instr) { if (*q == '\\') { if (q[1]) q++; continue; } if (*q == '"') instr = 0; continue; }
            if (*q == '"') { instr = 1; continue; }
            if (*q == '{') depth++;
            else if (*q == '}') { depth--; if (depth == 0) { q++; break; } }
        }
        const char *objend = q;

        char title[200] = {0}, url[400] = {0}, content[300] = {0};
        int ht = json_field(obj, objend, "title", title, sizeof(title));
        int hu = json_field(obj, objend, "url", url, sizeof(url));
        json_field(obj, objend, "content", content, sizeof(content));

        if (ht || hu) {
            size_t ol = strlen(out);
            snprintf(out + ol, cap - ol, "%d. %s\n   %s\n%s%s%s",
                     n + 1, title[0] ? title : "(ohne Titel)",
                     url,
                     content[0] ? "   " : "", content, content[0] ? "\n" : "");
            n++;
        }
        p = objend;
    }
    return n;
}

static int tool_web_search(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Suchbegriff angegeben");
        return 1;
    }
    char base[256] = {0};
    if (!flux_config_get("searxng_url", base, sizeof(base)) || !base[0]) {
        snprintf(out, cap,
                 "Keine SearXNG-Instanz konfiguriert. Trage searxng_url in den "
                 "Einstellungen ein (z.B. http://macbook.local:8888) -- die Instanz "
                 "laeuft auf deinem MacBook und muss das JSON-Format erlauben.");
        return 1;
    }
    size_t bl = strlen(base);
    while (bl > 0 && base[bl-1] == '/') base[--bl] = '\0';

    CURL *curl = curl_easy_init();
    if (!curl) { snprintf(out, cap, "Fehler: curl nicht verfuegbar"); return 1; }

    char *q = curl_easy_escape(curl, arg, 0);
    char url[1024];
    snprintf(url, sizeof(url),
             "%s/search?q=%s&format=json&language=de&safesearch=1",
             base, q ? q : "");
    if (q) curl_free(q);

    /* Wachsender Heap-Antwortpuffer (gemeinsame flux_http_buf-Hilfe).
     * Nicht auf einen Stack-Array zeigen lassen -- der Callback realloc't. */
    flux_http_buf mb;
    if (flux_http_buf_init(&mb, 16384) != 0) {
        curl_easy_cleanup(curl);
        snprintf(out, cap, "Fehler: kein Speicher");
        return 1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, flux_http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "flux-os/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        flux_http_buf_free(&mb);
        snprintf(out, cap,
                 "SearXNG nicht erreichbar (%s). Laeuft die Instanz auf dem "
                 "MacBook und ist das Geraet im selben Netz?",
                 curl_easy_strerror(res));
        return 1;
    }
    if (http == 403 || strstr(mb.data, "\"results\"") == NULL) {
        flux_http_buf_free(&mb);
        snprintf(out, cap,
                 "Keine Treffer oder JSON-Format nicht aktiviert. Erlaube in der "
                 "SearXNG-settings.yml 'formats: [html, json]' und starte neu.");
        return 1;
    }

    char header[300];
    snprintf(header, sizeof(header), "Web-Suchergebnisse fuer \"%s\":\n", arg);
    snprintf(out, cap, "%s", header);
    int n = parse_searxng(mb.data, out, cap, 5);
    flux_http_buf_free(&mb);
    if (n == 0) snprintf(out, cap, "Keine Treffer fuer \"%s\".", arg);
    return 1;
}

/* ---- journal_list ---------------------------------------------------- */
static int tool_journal_list(const char *arg, char *out, size_t cap) {
    (void)arg;
    DIR *d = opendir("/home/user/Journal");
    if (!d) { snprintf(out, cap, "Noch kein Journal vorhanden."); return 1; }
    char entries[64][32]; int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && n < 64) {
        if (de->d_name[0] == '.') continue;
        size_t l = strlen(de->d_name);
        if (l > 4 && strcmp(de->d_name + l - 4, ".txt") == 0) {
            strncpy(entries[n], de->d_name, 31); entries[n][31] = '\0';
            n++;
        }
    }
    closedir(d);
    if (n == 0) { snprintf(out, cap, "Noch keine Journal-Eintraege vorhanden."); return 1; }
    /* sort descending (newest first) */
    for (int i = 0; i < n - 1; i++)
        for (int j = i+1; j < n; j++)
            if (strcmp(entries[i], entries[j]) < 0) {
                char tmp[32]; memcpy(tmp, entries[i], 32);
                memcpy(entries[i], entries[j], 32);
                memcpy(entries[j], tmp, 32);
            }
    char buf[2048]; size_t pos = snprintf(buf, sizeof(buf), "Journal-Eintraege (%d):\n", n);
    for (int i = 0; i < n && pos + 40 < sizeof(buf); i++) {
        /* strip .txt for display */
        char name[28]; strncpy(name, entries[i], 27); name[27] = '\0';
        char *dot = strrchr(name, '.'); if (dot) *dot = '\0';
        pos += snprintf(buf + pos, sizeof(buf) - pos, "  %s\n", name);
    }
    snprintf(out, cap, "%s", buf);
    return 1;
}

/* ---- journal_read ---------------------------------------------------- */
static int tool_journal_read(const char *arg, char *out, size_t cap) {
    char path[256];
    if (!arg || !*arg || strcmp(arg, "heute") == 0) {
        time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
        char date[16]; strftime(date, sizeof(date), "%Y-%m-%d", &tm);
        snprintf(path, sizeof(path), "/home/user/Journal/%s.txt", date);
    } else if (strcmp(arg, "gestern") == 0) {
        time_t t = time(NULL) - 86400; struct tm tm; localtime_r(&t, &tm);
        char date[16]; strftime(date, sizeof(date), "%Y-%m-%d", &tm);
        snprintf(path, sizeof(path), "/home/user/Journal/%s.txt", date);
    } else {
        /* arg is a date like 2026-06-18 */
        snprintf(path, sizeof(path), "/home/user/Journal/%s.txt", arg);
    }
    /* path traversal guard -- realpath schreibt bis zu PATH_MAX Bytes */
    char real[PATH_MAX];
    if (!realpath(path, real) || strncmp(real, "/home/user/Journal/", 19) != 0) {
        snprintf(out, cap, "Fehler: ungueltiger Pfad."); return 1;
    }
    FILE *f = fopen(real, "r");
    if (!f) { snprintf(out, cap, "Kein Journal-Eintrag fuer dieses Datum vorhanden."); return 1; }
    char buf[4096]; size_t n = fread(buf, 1, sizeof(buf)-1, f); fclose(f);
    buf[n] = '\0';
    snprintf(out, cap, "%s", buf);
    return 1;
}

/* ---- meeting_list ---------------------------------------------------- */
static int tool_meeting_list(const char *arg, char *out, size_t cap) {
    (void)arg;
    DIR *d = opendir("/home/user/Meetings");
    if (!d) { snprintf(out, cap, "Noch keine Meeting-Protokolle vorhanden."); return 1; }
    char entries[64][40]; int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && n < 64) {
        if (de->d_name[0] == '.') continue;
        size_t l = strlen(de->d_name);
        if (l > 4 && strcmp(de->d_name + l - 4, ".txt") == 0) {
            strncpy(entries[n], de->d_name, 39); entries[n][39] = '\0';
            n++;
        }
    }
    closedir(d);
    if (n == 0) { snprintf(out, cap, "Noch keine Meeting-Protokolle vorhanden."); return 1; }
    for (int i = 0; i < n - 1; i++)
        for (int j = i+1; j < n; j++)
            if (strcmp(entries[i], entries[j]) < 0) {
                char tmp[40]; memcpy(tmp, entries[i], 40);
                memcpy(entries[i], entries[j], 40);
                memcpy(entries[j], tmp, 40);
            }
    char buf[2048]; size_t pos = snprintf(buf, sizeof(buf), "Meeting-Protokolle (%d):\n", n);
    for (int i = 0; i < n && pos + 50 < sizeof(buf); i++) {
        char name[36]; strncpy(name, entries[i], 35); name[35] = '\0';
        char *dot = strrchr(name, '.'); if (dot) *dot = '\0';
        pos += snprintf(buf + pos, sizeof(buf) - pos, "  %s\n", name);
    }
    snprintf(out, cap, "%s", buf);
    return 1;
}

/* ---- meeting_read ---------------------------------------------------- */
static int tool_meeting_read(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) { snprintf(out, cap, "Fehler: Datum oder 'letztes' angeben."); return 1; }
    char path[256];
    if (strcmp(arg, "letztes") == 0) {
        DIR *d = opendir("/home/user/Meetings");
        if (!d) { snprintf(out, cap, "Keine Meetings vorhanden."); return 1; }
        char newest[40] = {0};
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            size_t l = strlen(de->d_name);
            if (l > 4 && strcmp(de->d_name + l - 4, ".txt") == 0)
                if (strcmp(de->d_name, newest) > 0)
                    strncpy(newest, de->d_name, 39);
        }
        closedir(d);
        if (!newest[0]) { snprintf(out, cap, "Keine Meetings vorhanden."); return 1; }
        snprintf(path, sizeof(path), "/home/user/Meetings/%s", newest);
    } else {
        snprintf(path, sizeof(path), "/home/user/Meetings/%s.txt", arg);
    }
    char real[PATH_MAX];
    if (!realpath(path, real) || strncmp(real, "/home/user/Meetings/", 20) != 0) {
        snprintf(out, cap, "Fehler: ungueltiger Pfad."); return 1;
    }
    FILE *f = fopen(real, "r");
    if (!f) { snprintf(out, cap, "Meeting-Protokoll nicht gefunden."); return 1; }
    char buf[4096]; size_t n = fread(buf, 1, sizeof(buf)-1, f); fclose(f);
    buf[n] = '\0';
    snprintf(out, cap, "%s", buf);
    return 1;
}

/* ---- doc_analyze ----------------------------------------------------- */
static int tool_doc_analyze(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) { snprintf(out, cap, "Fehler: Dateipfad angeben."); return 1; }
    char real[PATH_MAX];
    if (!realpath(arg, real)) { snprintf(out, cap, "Datei nicht gefunden: %s", arg); return 1; }
    /* Allow access only within /home/user/ and /etc/flux/ */
    if (strncmp(real, "/home/user/", 11) != 0 && strncmp(real, "/etc/flux/", 10) != 0) {
        snprintf(out, cap, "Zugriff verweigert: nur /home/user/ und /etc/flux/ erlaubt.");
        return 1;
    }
    /* flux.conf enthaelt API-Keys und SMTP-Passwort -- wie bei file_read
     * vor Zugriff durch die KI schuetzen. */
    if (strcmp(real, FLUX_CONFIG_PATH) == 0) {
        snprintf(out, cap, "Zugriff verweigert: %s enthaelt Zugangsdaten.", FLUX_CONFIG_PATH);
        return 1;
    }
    FILE *f = fopen(real, "r");
    if (!f) { snprintf(out, cap, "Datei nicht lesbar: %s", real); return 1; }
    char content[8192]; size_t n = fread(content, 1, sizeof(content)-1, f); fclose(f);
    content[n] = '\0';
    int truncated = (n == sizeof(content)-1);
    snprintf(out, cap, "Dateiinhalt von %s%s:\n\n%s",
             real,
             truncated ? " (abgeschnitten nach 8000 Zeichen)" : "",
             content);
    return 1;
}

/* ---- Dispatch -------------------------------------------------------- */

int flux_tool_exec(const char *name, const char *arg,
                   char *out, size_t out_cap) {
    if (strcmp(name, "mail_unread")      == 0) return tool_mail_unread(arg, out, out_cap);
    if (strcmp(name, "mail_read")        == 0) return tool_mail_read(arg, out, out_cap);
    if (strcmp(name, "web_search")       == 0) return tool_web_search(arg, out, out_cap);
    if (strcmp(name, "date_time")        == 0) return tool_date_time(arg, out, out_cap);
    if (strcmp(name, "weather")          == 0) return tool_weather(arg, out, out_cap);
    if (strcmp(name, "file_read")        == 0) return tool_file_read(arg, out, out_cap);
    if (strcmp(name, "file_list")        == 0) return tool_file_list(arg, out, out_cap);
    if (strcmp(name, "file_create")      == 0) return tool_file_create(arg, out, out_cap);
    if (strcmp(name, "file_delete")      == 0) return tool_file_delete(arg, out, out_cap);
    if (strcmp(name, "file_rename")      == 0) return tool_file_rename(arg, out, out_cap);
    if (strcmp(name, "calculate")        == 0) return tool_calculate(arg, out, out_cap);
    if (strcmp(name, "note_save")        == 0) return tool_note_save(arg, out, out_cap);
    if (strcmp(name, "note_list")        == 0) return tool_note_list(arg, out, out_cap);
    if (strcmp(name, "note_search")      == 0) return tool_note_search(arg, out, out_cap);
    if (strcmp(name, "note_delete")      == 0) return tool_note_delete(arg, out, out_cap);
    if (strcmp(name, "sys_info")         == 0) return tool_sys_info(arg, out, out_cap);
    if (strcmp(name, "alarm_set")        == 0) return tool_alarm_set(arg, out, out_cap);
    if (strcmp(name, "alarm_list")       == 0) return tool_alarm_list(arg, out, out_cap);
    if (strcmp(name, "alarm_delete")     == 0) return tool_alarm_delete(arg, out, out_cap);
    if (strcmp(name, "timer_set")        == 0) return tool_timer_set(arg, out, out_cap);
    if (strcmp(name, "timer_list")       == 0) return tool_timer_list(arg, out, out_cap);
    if (strcmp(name, "timer_delete")     == 0) return tool_timer_delete(arg, out, out_cap);
    if (strcmp(name, "reminder_set")     == 0) return tool_reminder_set(arg, out, out_cap);
    if (strcmp(name, "reminder_list")    == 0) return tool_reminder_list(arg, out, out_cap);
    if (strcmp(name, "reminder_delete")  == 0) return tool_reminder_delete(arg, out, out_cap);
    if (strcmp(name, "contacts_search")  == 0) return tool_contacts_search(arg, out, out_cap);
    if (strcmp(name, "brightness_get")   == 0) return tool_brightness_get(arg, out, out_cap);
    if (strcmp(name, "brightness_set")   == 0) return tool_brightness_set(arg, out, out_cap);
    if (strcmp(name, "wifi_info")        == 0) return tool_wifi_info(arg, out, out_cap);
    if (strcmp(name, "wifi_on")          == 0) return tool_wifi_on(arg, out, out_cap);
    if (strcmp(name, "wifi_off")         == 0) return tool_wifi_off(arg, out, out_cap);
    if (strcmp(name, "flight_mode_on")   == 0) return tool_flight_mode_on(arg, out, out_cap);
    if (strcmp(name, "flight_mode_off")  == 0) return tool_flight_mode_off(arg, out, out_cap);
    if (strcmp(name, "pin_set")          == 0) return tool_pin_set(arg, out, out_cap);
    if (strcmp(name, "vibrate")          == 0) return tool_vibrate(arg, out, out_cap);
    if (strcmp(name, "flight_mode")      == 0) return tool_flight_mode(arg, out, out_cap);
    if (strcmp(name, "contact_save")   == 0) return tool_contact_save(arg, out, out_cap);
    if (strcmp(name, "contacts_list")  == 0) return tool_contacts_list(arg, out, out_cap);
    if (strcmp(name, "calendar_add")    == 0) return tool_calendar_add(arg, out, out_cap);
    if (strcmp(name, "calendar_list")   == 0) return tool_calendar_list(arg, out, out_cap);
    if (strcmp(name, "calendar_delete") == 0) return tool_calendar_delete(arg, out, out_cap);
    if (strcmp(name, "search_files")   == 0) return tool_search_files(arg, out, out_cap);
    if (strcmp(name, "prefs_set")      == 0) return tool_prefs_set(arg, out, out_cap);
    if (strcmp(name, "image_list")    == 0) return tool_image_list(arg, out, out_cap);
    if (strcmp(name, "image_analyze") == 0) return tool_image_analyze(arg, out, out_cap);
    if (strcmp(name, "plant_identify")== 0) return tool_plant_identify(arg, out, out_cap);
    if (strcmp(name, "logo_detect")   == 0) return tool_logo_detect(arg, out, out_cap);
    if (strcmp(name, "image_take")    == 0) return tool_image_take(arg, out, out_cap);
    if (strcmp(name, "memory_save")   == 0) return tool_memory_save(arg, out, out_cap);
    if (strcmp(name, "memory_list")   == 0) return tool_memory_list(arg, out, out_cap);
    if (strcmp(name, "memory_search") == 0) return tool_memory_search(arg, out, out_cap);
    if (strcmp(name, "memory_delete") == 0) return tool_memory_delete(arg, out, out_cap);
    if (strcmp(name, "journal_list")  == 0) return tool_journal_list(arg, out, out_cap);
    if (strcmp(name, "journal_read")  == 0) return tool_journal_read(arg, out, out_cap);
    if (strcmp(name, "meeting_list")  == 0) return tool_meeting_list(arg, out, out_cap);
    if (strcmp(name, "meeting_read")  == 0) return tool_meeting_read(arg, out, out_cap);
    if (strcmp(name, "doc_analyze")   == 0) return tool_doc_analyze(arg, out, out_cap);
    return 0; /* unbekanntes Tool */
}

/* ---- Natives Tool-Schema (JSON) -------------------------------------- */

/* Eine Tooldefinition fuer das native Tool-Calling. arg_desc beschreibt das
 * (einzige) String-Argument; arg_required gibt an, ob "arg" zwingend ist. */
typedef struct {
    const char *name;
    const char *desc;       /* JSON-sicher: keine " oder \ noetig hier */
    const char *arg_desc;   /* Beschreibung des "arg"-Strings */
    int         arg_required;
} flux_tool_def_t;

/* Reihenfolge stabil halten -- so bleibt das erzeugte JSON deterministisch
 * und damit Prompt-Cache-faehig (gleiche Bytes bei jeder Anfrage). */
static const flux_tool_def_t TOOL_DEFS[] = {
    { "date_time",       "Aktuelles Datum und Uhrzeit.", "(leer, kein Argument noetig)", 0 },
    { "weather",         "Aktuelles Wetter.", "Stadtname (leer = automatische Ortserkennung)", 0 },
    { "file_read",       "Dateiinhalt lesen (nur /home/user/, /tmp/, /proc/, /sys/).", "Dateipfad", 1 },
    { "file_list",       "Verzeichnis auflisten.", "Verzeichnispfad", 1 },
    { "file_create",     "Datei erstellen.", "Format: /pfad/datei.txt|Inhalt (\\n fuer Zeilenumbruch)", 1 },
    { "file_delete",     "Datei loeschen (nur /home/user/).", "Dateipfad", 1 },
    { "calculate",       "Rechenausdruck auswerten.", "z.B. '15 * 8 + 3.5'", 1 },
    { "note_save",       "Notiz speichern.", "Notiztext", 1 },
    { "note_list",       "Alle Notizen anzeigen.", "(leer)", 0 },
    { "sys_info",        "Systeminformationen (Speicher, Kernel, Laufzeit).", "(leer)", 0 },
    { "alarm_set",       "Wecker/Alarm zu einer Uhrzeit stellen. Nutze dies bei 'Wecker', 'weck mich', 'Alarm um ...'.", "HH:MM Beschreibung (z.B. '07:00 Aufstehen')", 1 },
    { "reminder_set",    "Erinnerung ohne feste Uhrzeit setzen.", "Erinnerungstext", 1 },
    { "contacts_search", "Kontakt suchen.", "Name oder Nummer", 1 },
    { "brightness_get",  "Bildschirmhelligkeit lesen.", "(leer)", 0 },
    { "brightness_set",  "Bildschirmhelligkeit setzen.", "0-100 (Prozent)", 1 },
    { "wifi_info",       "WLAN-Signalstaerke und Interface.", "(leer)", 0 },
    { "vibrate",         "Geraet vibrieren lassen.", "Dauer in ms (z.B. 300)", 0 },
    { "contact_save",    "Kontakt speichern.", "Name,Telefon,Email[,Geburtstag]", 1 },
    { "contacts_list",   "Alle Kontakte anzeigen.", "(leer)", 0 },
    { "calendar_add",    "Termin eintragen.", "YYYY-MM-DD HH:MM Beschreibung", 1 },
    { "calendar_list",   "Bevorstehende Termine anzeigen.", "(leer)", 0 },
    { "search_files",    "Dateien in /home/user/ suchen.", "Suchbegriff", 1 },
    { "prefs_set",       "Nutzerpraeferenz merken (fuer spaetere Kontextnutzung).", "Praeferenztext", 1 },
    { "image_list",      "Fotos in /home/user/Pictures/ auflisten.", "(leer)", 0 },
    { "image_analyze",   "Bild per KI analysieren (Was ist drauf? Wo aufgenommen?).", "Dateiname oder Pfad", 1 },
    { "plant_identify",  "Exakte Pflanzenart eines Fotos bestimmen (Pl@ntNet-Spezialist).", "Dateiname oder Pfad (in /home/user/Pictures)", 1 },
    { "logo_detect",     "Logos/Marken und Text auf einem Foto erkennen (Google-Vision-Spezialist).", "Dateiname oder Pfad (in /home/user/Pictures)", 1 },
    { "image_take",      "Neues Foto aufnehmen und speichern.", "(leer)", 0 },
    { "memory_save",     "Persoenliche Info dauerhaft merken (Name, Geburtstag, Praeferenz usw.).", "Text", 1 },
    { "memory_list",     "Alle gespeicherten Infos anzeigen.", "(leer)", 0 },
    { "memory_search",   "Gespeicherte Infos durchsuchen.", "Suchbegriff", 1 },
    { "memory_delete",   "Gespeicherte Info loeschen.", "Suchbegriff", 1 },
    { "journal_list",    "Alle Tagesjournal-Eintraege auflisten.", "(leer)", 0 },
    { "journal_read",    "Einen Journal-Eintrag lesen.", "YYYY-MM-DD oder 'heute' oder 'gestern'", 0 },
    { "meeting_list",    "Alle Meeting-Protokolle auflisten.", "(leer)", 0 },
    { "meeting_read",    "Ein Meeting-Protokoll lesen.", "YYYY-MM-DD_HHmm oder 'letztes'", 1 },
    { "doc_analyze",     "Dateiinhalt lesen und der KI als Kontext uebergeben.", "Dateipfad", 1 },
    { "mail_unread",     "Ungelesene E-Mails abrufen (Von/Betreff/Datum, fuer Zusammenfassungen).", "(leer)", 0 },
    { "mail_read",       "Volltext einer E-Mail lesen.", "UID (aus mail_unread)", 1 },
    { "web_search",      "Im Internet suchen (aktuelle Infos/News/Fakten).", "Suchbegriff", 1 },
};
static const int TOOL_DEFS_N = (int)(sizeof(TOOL_DEFS) / sizeof(TOOL_DEFS[0]));

/* Haengt s JSON-escaped an buf an (begrenzt durch cap, *pos wird fortgeschrieben).
 * Behandelt " und \ -- die Tool-Texte enthalten sonst nur ASCII. */
static void json_append_escaped(char *buf, size_t cap, size_t *pos, const char *s) {
    for (; *s && *pos + 2 < cap; s++) {
        if (*s == '"' || *s == '\\') buf[(*pos)++] = '\\';
        buf[(*pos)++] = *s;
    }
    buf[*pos] = '\0';
}

/* Haengt das JSON-Schema-Objekt fuer das Argument eines Tools an.
 * Erzeugt: {"type":"object","properties":{"arg":{...}}[,"required":["arg"]]} */
static void append_arg_schema(char *buf, size_t cap, size_t *pos,
                              const flux_tool_def_t *t) {
    *pos += (size_t)snprintf(buf + *pos, cap - *pos,
        "{\"type\":\"object\",\"properties\":"
        "{\"arg\":{\"type\":\"string\",\"description\":\"");
    json_append_escaped(buf, cap, pos, t->arg_desc);
    *pos += (size_t)snprintf(buf + *pos, cap - *pos, "\"}}");
    if (t->arg_required)
        *pos += (size_t)snprintf(buf + *pos, cap - *pos, ",\"required\":[\"arg\"]");
    *pos += (size_t)snprintf(buf + *pos, cap - *pos, "}");
}

/* Gemeinsamer Aufbau. openai=1 -> OpenAI-Function-Format, sonst Anthropic.
 * Das OpenAI-Format ist durch die zusaetzliche function-Verpackung groesser,
 * daher reichlich Puffer. */
#define FLUX_TOOLS_SCHEMA_CAP 12288
static const char *build_tools_schema(int openai) {
    static char anthropic_buf[FLUX_TOOLS_SCHEMA_CAP];
    static char openai_buf[FLUX_TOOLS_SCHEMA_CAP];
    static int  anthropic_built = 0, openai_built = 0;
    char  *buf = openai ? openai_buf : anthropic_buf;
    int   *built = openai ? &openai_built : &anthropic_built;
    const size_t cap = FLUX_TOOLS_SCHEMA_CAP;
    if (*built) return buf;

    size_t pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < TOOL_DEFS_N; i++) {
        const flux_tool_def_t *t = &TOOL_DEFS[i];
        if (i) buf[pos++] = ',';
        if (openai) {
            pos += (size_t)snprintf(buf + pos, cap - pos,
                "{\"type\":\"function\",\"function\":"
                "{\"name\":\"%s\",\"description\":\"", t->name);
            json_append_escaped(buf, cap, &pos, t->desc);
            pos += (size_t)snprintf(buf + pos, cap - pos, "\",\"parameters\":");
            append_arg_schema(buf, cap, &pos, t);
            pos += (size_t)snprintf(buf + pos, cap - pos, "}}");
        } else {
            pos += (size_t)snprintf(buf + pos, cap - pos,
                "{\"name\":\"%s\",\"description\":\"", t->name);
            json_append_escaped(buf, cap, &pos, t->desc);
            pos += (size_t)snprintf(buf + pos, cap - pos, "\",\"input_schema\":");
            append_arg_schema(buf, cap, &pos, t);
            pos += (size_t)snprintf(buf + pos, cap - pos, "}");
        }
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    *built = 1;
    return buf;
}

const char *flux_tools_json_schema(void)   { return build_tools_schema(0); }
const char *flux_tools_openai_schema(void) { return build_tools_schema(1); }

const char *flux_tools_description(void) {
    return
        "Du hast Zugriff auf folgende Tools. Rufe ein Tool auf, indem du "
        "AUSSCHLIESSLICH folgendes Format verwendest (kein Text davor/danach):\n"
        "TOOL:<toolname>\n"
        "ARG:<argument>\n\n"
        "Verfuegbare Tools:\n"
        "  date_time        -- aktuelles Datum/Uhrzeit. ARG: (leer)\n"
        "  weather          -- aktuelles Wetter. ARG: Stadtname (leer = automatisch)\n"
        "  file_read        -- Dateiinhalt lesen. ARG: Dateipfad\n"
        "  file_list        -- Verzeichnis auflisten. ARG: Verzeichnispfad\n"
        "  file_create      -- Datei erstellen. ARG: name.txt|Inhalt (\\n fuer Zeilenumbruch). "
        "Ohne fuehrenden / landet die Datei im Benutzer-Ordner /home/user/Dokumente.\n"
        "  file_delete      -- Datei loeschen (nur /home/user/). ARG: Dateipfad\n"
        "  file_rename      -- Datei umbenennen/verschieben (nur /home/user/). "
        "ARG: altpfad|neupfad (Ziel wird nicht ueberschrieben)\n"
        "  calculate        -- Rechenausdruck. ARG: z.B. '15 * 8 + 3.5'\n"
        "  note_save        -- Notiz speichern. ARG: Notiztext\n"
        "  note_list        -- Alle Notizen anzeigen. ARG: (leer)\n"
        "  note_search      -- Notizen nach Stichwort durchsuchen. ARG: Suchbegriff\n"
        "  note_delete      -- Notizen loeschen (alle die Suchbegriff enthalten). ARG: Suchbegriff\n"
        "  sys_info         -- Systeminfos. ARG: (leer)\n"
        "  alarm_set        -- Wecker/Alarm zu einer UHRZEIT. ARG: HH:MM Beschreibung "
        "(z.B. '07:00 Aufstehen'). Nutze dies bei 'Wecker', 'weck mich', 'Alarm um ...'.\n"
        "  alarm_list       -- Alle gesetzten Alarme anzeigen. ARG: (leer)\n"
        "  alarm_delete     -- Alarm loeschen (alle Zeilen die Suchbegriff enthalten). ARG: Suchbegriff\n"
        "  timer_set        -- Countdown-Timer starten. ARG: Dauer (z.B. '5 Minuten', '30s', '1h30m'). "
        "Nutze dies bei 'Timer', 'stell einen Timer', 'in X Minuten'.\n"
        "  timer_list       -- Aktive Timer mit Restzeit anzeigen. ARG: (leer)\n"
        "  timer_delete     -- Timer loeschen (alle Zeilen die Suchbegriff enthalten). ARG: Suchbegriff\n"
        "  reminder_set     -- Erinnerung OHNE feste Uhrzeit. ARG: Erinnerungstext\n"
        "  reminder_list    -- Alle gesetzten Erinnerungen anzeigen. ARG: (leer)\n"
        "  reminder_delete  -- Erinnerung loeschen (alle Zeilen die Suchbegriff enthalten). ARG: Suchbegriff\n"
        "  contacts_search  -- Kontakt suchen. ARG: Name oder Nummer\n"
        "  brightness_get   -- Bildschirmhelligkeit lesen. ARG: (leer)\n"
        "  brightness_set   -- Bildschirmhelligkeit setzen. ARG: 0-100 (Prozent)\n"
        "  wifi_info        -- WLAN-Signalstaerke und Interface. ARG: (leer)\n"
        "  wifi_on          -- WLAN einschalten. ARG: (leer)\n"
        "  wifi_off         -- WLAN ausschalten. ARG: (leer)\n"
        "  flight_mode_on   -- Flugmodus aktivieren (alle Funkschnittstellen sperren). ARG: (leer)\n"
        "  flight_mode_off  -- Flugmodus deaktivieren (alle Funkschnittstellen freigeben). ARG: (leer)\n"
        "  pin_set          -- Geraete-PIN aendern. ARG: neuer PIN (nur 4-8 Ziffern, sofort aktiv). "
        "Nur aufrufen wenn der Nutzer explizit die PIN aendern moechte.\n"
        "  vibrate          -- Geraet vibrieren lassen. ARG: Dauer in ms (z.B. 300)\n"
        "  flight_mode      -- Flugmodus-STATUS abfragen (read-only, ob Funk an/aus). ARG: (leer). "
        "Zum SCHALTEN nicht dieses Tool nutzen, sondern den ACTION:flight-Block (Bestaetigungs-Dialog).\n"
        "  contact_save    -- Kontakt speichern. ARG: Name,Telefon,Email\n"
        "  contacts_list   -- Alle Kontakte anzeigen. ARG: (leer)\n"
        "  calendar_add    -- Termin eintragen. ARG: YYYY-MM-DD HH:MM Beschreibung\n"
        "  calendar_list   -- Bevorstehende Termine. ARG: (leer)\n"
        "  calendar_delete -- Termin loeschen (alle die Suchbegriff enthalten). ARG: Suchbegriff\n"
        "  search_files    -- Dateien suchen. ARG: Suchbegriff (in /home/user/)\n"
        "  prefs_set       -- Nutzerpraeferenz merken (fuer spaetere Kontextnutzung). ARG: Praeferenztext\n"
        "  image_list      -- Fotos in /home/user/Pictures/ auflisten. ARG: (leer)\n"
        "  image_analyze   -- Bild per KI analysieren (Was ist drauf? Wo wurde es aufgenommen?). ARG: Dateiname oder Pfad\n"
        "  plant_identify  -- Exakte Pflanzenart eines Fotos bestimmen (Pl@ntNet). ARG: Dateiname oder Pfad\n"
        "  logo_detect     -- Logos/Marken und Text auf einem Foto erkennen (Google Vision). ARG: Dateiname oder Pfad\n"
        "  image_take      -- Neues Foto aufnehmen und speichern. ARG: (leer)\n"
        "  memory_save     -- Persoenliche Info dauerhaft merken (Name, Geburtstag, Praeferenz usw.). ARG: Text\n"
        "  memory_list     -- Alle gespeicherten Infos anzeigen. ARG: (leer)\n"
        "  memory_search   -- Gespeicherte Infos durchsuchen. ARG: Suchbegriff\n"
        "  memory_delete   -- Gespeicherte Info loeschen. ARG: Suchbegriff\n"
        "  journal_list    -- Alle Tagesjournal-Eintraege auflisten. ARG: (leer)\n"
        "  journal_read    -- Einen Journal-Eintrag lesen. ARG: YYYY-MM-DD oder 'heute' oder 'gestern'\n"
        "  meeting_list    -- Alle Meeting-Protokolle auflisten. ARG: (leer)\n"
        "  meeting_read    -- Ein Meeting-Protokoll lesen. ARG: YYYY-MM-DD_HHmm oder 'letztes'\n"
        "  doc_analyze     -- Dateiinhalt lesen und der KI als Kontext uebergeben. ARG: Dateipfad\n"
        "  mail_unread     -- Ungelesene E-Mails abrufen (Von/Betreff/Datum, fuer Zusammenfassungen). ARG: (leer)\n"
        "  mail_read       -- Text einer E-Mail lesen. ARG: UID (aus mail_unread)\n"
        "  web_search      -- Im Internet suchen (aktuelle Infos/News/Fakten). ARG: Suchbegriff\n"
        "Verwende Tools NUR wenn Echtzeitdaten benoetigt werden (Wetter, Dateien, Berechnung, "
        "aktuelle Infos via web_search usw.). "
        "Wenn der Nutzer dir persoenliche Infos nennt (Name, Geburtstag, Praeferenz), "
        "speichere diese SOFORT mit memory_save -- ohne explizite Aufforderung. "
        "Normale Fragen beantworte ohne Tools.";
}
