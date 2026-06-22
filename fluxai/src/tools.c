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
 *   sys_info         -- Systeminfo (Speicher, OS), ARG: (leer)
 *   brightness_get   -- Bildschirmhelligkeit lesen, ARG: (leer)
 *   brightness_set   -- Bildschirmhelligkeit setzen, ARG: 0-100 (Prozent)
 *   wifi_info        -- WLAN-Signalstaerke und Interface, ARG: (leer)
 *   vibrate          -- Geraet vibrieren lassen, ARG: Dauer in ms (z.B. 300)
 *   memory_save      -- Personliche Info dauerhaft merken, ARG: Text
 *   memory_list      -- Alle KI-Erinnerungen anzeigen, ARG: (leer)
 *   memory_search    -- KI-Erinnerungen durchsuchen, ARG: Suchbegriff
 *   memory_delete    -- Erinnerungen loeschen, ARG: Suchbegriff
 */
#include "tools.h"
#include "vision.h"
#include "imap.h"
#include "radio.h"
#include "../../common/flux_config.h"

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

#define NOTES_PATH      "/etc/flux/notes.txt"
#define MEMORY_PATH     "/etc/flux/memory.txt"
#define WEATHER_CACHE   "/tmp/flux_weather.txt"
#define FILE_READ_MAX   8192
#define NOTE_TEXT_MAX   512

/* ---- Curl-Hilfspuffer ------------------------------------------------ */

struct membuf { char *data; size_t len, cap; };

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    struct membuf *mb = ud;
    size_t add = size * nmemb;
    if (mb->len + add + 1 > mb->cap) return 0;
    memcpy(mb->data + mb->len, ptr, add);
    mb->len += add;
    mb->data[mb->len] = '\0';
    return add;
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
    const char *city = (arg && *arg) ? arg : "";

    /* URL aufbauen: wttr.in/{city}?format=%l:+%C,+%t,+%h+Feuchte,+Wind+%w */
    char url[512];
    snprintf(url, sizeof(url),
             "https://wttr.in/%s?format=%%l:+%%C,+%%t,+%%h+Feuchte,+Wind+%%w",
             city);

    char respbuf[1024];
    respbuf[0] = '\0';
    struct membuf mb = { .data = respbuf, .len = 0, .cap = sizeof(respbuf) };

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(out, cap, "Fehler: curl nicht verfuegbar");
        return 1;
    }
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Accept-Language: de");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.x (flux-os)");

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        snprintf(out, cap, "Wetterdaten nicht verfuegbar: %s", curl_easy_strerror(res));
        return 1;
    }

    /* Steuerzeichen (Emoji, ANSI) aus Antwort entfernen -- das Terminal-Format
     * von wttr.in enthaelt manchmal Escape-Sequenzen. */
    char clean[512];
    size_t ci = 0;
    for (size_t i = 0; respbuf[i] && ci + 1 < sizeof(clean); i++) {
        unsigned char c = (unsigned char)respbuf[i];
        if (c == '\x1b') {
            /* ANSI Escape: ueberspringen bis 'm' */
            while (respbuf[i] && respbuf[i] != 'm') i++;
        } else if (c < 0x20 && c != '\n') {
            continue;
        } else if (c >= 0x80) {
            /* Multi-Byte UTF-8 (Emoji etc.): ueberspringen */
            if (c >= 0xC0) {
                int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
                i += extra;
            }
        } else {
            clean[ci++] = respbuf[i];
        }
    }
    clean[ci] = '\0';

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
    /* Pfad-Traversal mit ".." grundsaetzlich ablehnen */
    if (strstr(path, "..")) return 0;
    static const char *ok_prefixes[] = {
        "/home/user/", "/tmp/", "/proc/", "/sys/", NULL
    };
    for (int i = 0; ok_prefixes[i]; i++)
        if (strncmp(path, ok_prefixes[i], strlen(ok_prefixes[i])) == 0) return 1;
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
    char path[512];
    if (!sep) {
        snprintf(path, sizeof(path), "%s", arg);
    } else {
        size_t plen = (size_t)(sep - arg);
        if (plen >= sizeof(path)) plen = sizeof(path) - 1;
        memcpy(path, arg, plen);
        path[plen] = '\0';
    }

    /* Sicherheit: kanonischen Pfad pruefen (verhindert Path-Traversal) */
    if (!path_is_allowed(path, 1)) {
        snprintf(out, cap,
                 "Fehler: Erstellen nur unter /home/user/ und /tmp/ erlaubt");
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
        /* Gross-Klein-unabhaengige Suche durch manuellen Vergleich */
        char lower_line[256], lower_arg[128];
        for (int i = 0; line[i] && i < 255; i++)
            lower_line[i] = (line[i] >= 'A' && line[i] <= 'Z') ? line[i] + 32 : line[i];
        lower_line[255] = '\0';
        for (int i = 0; arg[i] && i < 127; i++)
            lower_arg[i] = (arg[i] >= 'A' && arg[i] <= 'Z') ? arg[i] + 32 : arg[i];
        lower_arg[127] = '\0';
        if (strstr(lower_line, lower_arg)) {
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
        snprintf(buf, cap, "/sys/class/backlight/%s", e->d_name);
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
    if (f) { fscanf(f, "%ld", &cur); fclose(f); }
    else {
        snprintf(out, cap, "Fehler: brightness-Datei nicht lesbar (%s)", path_cur);
        return 1;
    }
    f = fopen(path_max, "r");
    if (f) { fscanf(f, "%ld", &max); fclose(f); }

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
    if (f) { fscanf(f, "%ld", &max); fclose(f); }
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
/* Flugmodus ueber rfkill schalten bzw. abfragen. Backend: radio.c
 * (austauschbar, ehrliche Meldung wenn keine Funkhardware vorhanden). */
static int tool_flight_mode(const char *arg, char *out, size_t cap) {
    /* Argument normalisieren (fuehrende Leerzeichen, Kleinbuchstaben). */
    char a[32] = {0};
    if (arg) {
        while (*arg == ' ') arg++;
        size_t i = 0;
        for (; arg[i] && i < sizeof(a) - 1; i++)
            a[i] = (char)tolower((unsigned char)arg[i]);
        a[i] = '\0';
    }

    if (!a[0] || strcmp(a, "status") == 0 || strcmp(a, "?") == 0) {
        flux_radio_status(out, cap);
        return 1;
    }

    int on;
    if (strcmp(a, "an") == 0 || strcmp(a, "ein") == 0 || strcmp(a, "on") == 0 ||
        strcmp(a, "1") == 0 || strcmp(a, "true") == 0 || strcmp(a, "ja") == 0 ||
        strcmp(a, "aktivieren") == 0 || strcmp(a, "aktiviere") == 0) {
        on = 1;
    } else if (strcmp(a, "aus") == 0 || strcmp(a, "off") == 0 || strcmp(a, "0") == 0 ||
               strcmp(a, "false") == 0 || strcmp(a, "nein") == 0 ||
               strcmp(a, "deaktivieren") == 0 || strcmp(a, "deaktiviere") == 0) {
        on = 0;
    } else {
        snprintf(out, cap,
            "Flugmodus: bitte 'an' oder 'aus' angeben (oder leer fuer Status). "
            "Angegeben: '%s'.", a);
        return 1;
    }

    flux_radio_set_airplane(on, out, cap);
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
    char lower[512]; size_t li = 0;
    for (const char *p = entry; *p && li < sizeof(lower)-1; p++, li++)
        lower[li] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
    lower[li] = '\0';
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
    char lower_arg[128]; size_t ai = 0;
    for (const char *p = arg; *p && ai < sizeof(lower_arg)-1; p++, ai++)
        lower_arg[ai] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
    lower_arg[ai] = '\0';
    char line[256]; int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char lower_line[256]; size_t li = 0;
        for (const char *p = line; *p && li < sizeof(lower_line)-1; p++, li++)
            lower_line[li] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
        lower_line[li] = '\0';
        if (strstr(lower_line, lower_arg)) {
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
    char lower_arg[128]; size_t ai = 0;
    for (const char *p = arg; *p && ai < sizeof(lower_arg)-1; p++, ai++)
        lower_arg[ai] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
    lower_arg[ai] = '\0';
    f = fopen(MEMORY_PATH, "w");
    if (!f) { snprintf(out, cap, "Fehler: Datei nicht schreibbar"); return 1; }
    int deleted = 0;
    for (int i = 0; i < n; i++) {
        char lower_line[256]; size_t li = 0;
        for (const char *p = lines[i]; *p && li < sizeof(lower_line)-1; p++, li++)
            lower_line[li] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
        lower_line[li] = '\0';
        if (strstr(lower_line, lower_arg)) { deleted++; }
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
    /* If no date prefix given, prepend today */
    if (!(entry[4] == '-' && entry[7] == '-')) {
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
        /* Case-insensitive name match */
        char lower_name[256], lower_pat[128];
        for (int i = 0; e->d_name[i] && i < 255; i++)
            lower_name[i] = (e->d_name[i] >= 'A' && e->d_name[i] <= 'Z')
                           ? e->d_name[i] + 32 : e->d_name[i];
        lower_name[strlen(e->d_name)] = '\0';
        for (int i = 0; pattern[i] && i < 127; i++)
            lower_pat[i] = (pattern[i] >= 'A' && pattern[i] <= 'Z')
                          ? pattern[i] + 32 : pattern[i];
        lower_pat[strlen(pattern)] = '\0';
        if (strstr(lower_name, lower_pat)) {
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
    /* Relativen Pfad in /home/user/Pictures/ auflösen */
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

    char respbuf[16384]; respbuf[0] = '\0';
    struct membuf mb = { .data = respbuf, .len = 0, .cap = sizeof(respbuf) };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "flux-os/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        snprintf(out, cap,
                 "SearXNG nicht erreichbar (%s). Laeuft die Instanz auf dem "
                 "MacBook und ist das Geraet im selben Netz?",
                 curl_easy_strerror(res));
        return 1;
    }
    if (http == 403 || strstr(respbuf, "\"results\"") == NULL) {
        snprintf(out, cap,
                 "Keine Treffer oder JSON-Format nicht aktiviert. Erlaube in der "
                 "SearXNG-settings.yml 'formats: [html, json]' und starte neu.");
        return 1;
    }

    char header[300];
    snprintf(header, sizeof(header), "Web-Suchergebnisse fuer \"%s\":\n", arg);
    snprintf(out, cap, "%s", header);
    int n = parse_searxng(respbuf, out, cap, 5);
    if (n == 0) snprintf(out, cap, "Keine Treffer fuer \"%s\".", arg);
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
    if (strcmp(name, "calculate")        == 0) return tool_calculate(arg, out, out_cap);
    if (strcmp(name, "note_save")        == 0) return tool_note_save(arg, out, out_cap);
    if (strcmp(name, "note_list")        == 0) return tool_note_list(arg, out, out_cap);
    if (strcmp(name, "sys_info")         == 0) return tool_sys_info(arg, out, out_cap);
    if (strcmp(name, "alarm_set")        == 0) return tool_alarm_set(arg, out, out_cap);
    if (strcmp(name, "reminder_set")     == 0) return tool_reminder_set(arg, out, out_cap);
    if (strcmp(name, "contacts_search")  == 0) return tool_contacts_search(arg, out, out_cap);
    if (strcmp(name, "brightness_get")   == 0) return tool_brightness_get(arg, out, out_cap);
    if (strcmp(name, "brightness_set")   == 0) return tool_brightness_set(arg, out, out_cap);
    if (strcmp(name, "wifi_info")        == 0) return tool_wifi_info(arg, out, out_cap);
    if (strcmp(name, "vibrate")          == 0) return tool_vibrate(arg, out, out_cap);
    if (strcmp(name, "flight_mode")      == 0) return tool_flight_mode(arg, out, out_cap);
    if (strcmp(name, "contact_save")   == 0) return tool_contact_save(arg, out, out_cap);
    if (strcmp(name, "contacts_list")  == 0) return tool_contacts_list(arg, out, out_cap);
    if (strcmp(name, "calendar_add")   == 0) return tool_calendar_add(arg, out, out_cap);
    if (strcmp(name, "calendar_list")  == 0) return tool_calendar_list(arg, out, out_cap);
    if (strcmp(name, "search_files")   == 0) return tool_search_files(arg, out, out_cap);
    if (strcmp(name, "prefs_set")      == 0) return tool_prefs_set(arg, out, out_cap);
    if (strcmp(name, "image_list")    == 0) return tool_image_list(arg, out, out_cap);
    if (strcmp(name, "image_analyze") == 0) return tool_image_analyze(arg, out, out_cap);
    if (strcmp(name, "image_take")    == 0) return tool_image_take(arg, out, out_cap);
    if (strcmp(name, "memory_save")   == 0) return tool_memory_save(arg, out, out_cap);
    if (strcmp(name, "memory_list")   == 0) return tool_memory_list(arg, out, out_cap);
    if (strcmp(name, "memory_search") == 0) return tool_memory_search(arg, out, out_cap);
    if (strcmp(name, "memory_delete") == 0) return tool_memory_delete(arg, out, out_cap);
    return 0; /* unbekanntes Tool */
}

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
        "  file_create      -- Datei erstellen. ARG: /pfad/datei.txt|Inhalt (\\n fuer Zeilenumbruch)\n"
        "  file_delete      -- Datei loeschen (nur /home/user/). ARG: Dateipfad\n"
        "  calculate        -- Rechenausdruck. ARG: z.B. '15 * 8 + 3.5'\n"
        "  note_save        -- Notiz speichern. ARG: Notiztext\n"
        "  note_list        -- Alle Notizen anzeigen. ARG: (leer)\n"
        "  sys_info         -- Systeminfos. ARG: (leer)\n"
        "  alarm_set        -- Wecker/Alarm zu einer UHRZEIT. ARG: HH:MM Beschreibung "
        "(z.B. '07:00 Aufstehen'). Nutze dies bei 'Wecker', 'weck mich', 'Alarm um ...'.\n"
        "  reminder_set     -- Erinnerung OHNE feste Uhrzeit. ARG: Erinnerungstext\n"
        "  contacts_search  -- Kontakt suchen. ARG: Name oder Nummer\n"
        "  brightness_get   -- Bildschirmhelligkeit lesen. ARG: (leer)\n"
        "  brightness_set   -- Bildschirmhelligkeit setzen. ARG: 0-100 (Prozent)\n"
        "  wifi_info        -- WLAN-Signalstaerke und Interface. ARG: (leer)\n"
        "  vibrate          -- Geraet vibrieren lassen. ARG: Dauer in ms (z.B. 300)\n"
        "  flight_mode      -- Flugmodus schalten/abfragen (alle Funkmodule via rfkill). "
        "ARG: 'an' | 'aus' | leer fuer Status. Nutze dies bei 'Flugmodus', 'Funk aus', 'aktivier Flugmodus'.\n"
        "  contact_save    -- Kontakt speichern. ARG: Name,Telefon,Email\n"
        "  contacts_list   -- Alle Kontakte anzeigen. ARG: (leer)\n"
        "  calendar_add    -- Termin eintragen. ARG: YYYY-MM-DD HH:MM Beschreibung\n"
        "  calendar_list   -- Bevorstehende Termine. ARG: (leer)\n"
        "  search_files    -- Dateien suchen. ARG: Suchbegriff (in /home/user/)\n"
        "  prefs_set       -- Nutzerpraeferenz merken (fuer spaetere Kontextnutzung). ARG: Praeferenztext\n"
        "  image_list      -- Fotos in /home/user/Pictures/ auflisten. ARG: (leer)\n"
        "  image_analyze   -- Bild per KI analysieren (Was ist drauf? Wo wurde es aufgenommen?). ARG: Dateiname oder Pfad\n"
        "  image_take      -- Neues Foto aufnehmen und speichern. ARG: (leer)\n"
        "  memory_save     -- Persoenliche Info dauerhaft merken (Name, Geburtstag, Praeferenz usw.). ARG: Text\n"
        "  memory_list     -- Alle gespeicherten Infos anzeigen. ARG: (leer)\n"
        "  memory_search   -- Gespeicherte Infos durchsuchen. ARG: Suchbegriff\n"
        "  memory_delete   -- Gespeicherte Info loeschen. ARG: Suchbegriff\n"
        "  mail_unread     -- Ungelesene E-Mails abrufen (Von/Betreff/Datum, fuer Zusammenfassungen). ARG: (leer)\n"
        "  mail_read       -- Text einer E-Mail lesen. ARG: UID (aus mail_unread)\n"
        "  web_search      -- Im Internet suchen (aktuelle Infos/News/Fakten). ARG: Suchbegriff\n"
        "Verwende Tools NUR wenn Echtzeitdaten benoetigt werden (Wetter, Dateien, Berechnung, "
        "aktuelle Infos via web_search usw.). "
        "Wenn der Nutzer dir persoenliche Infos nennt (Name, Geburtstag, Praeferenz), "
        "speichere diese SOFORT mit memory_save -- ohne explizite Aufforderung. "
        "Normale Fragen beantworte ohne Tools.";
}
