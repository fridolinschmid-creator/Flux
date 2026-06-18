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
 */
#include "tools.h"
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

#define NOTES_PATH      "/etc/flux/notes.txt"
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

static int tool_file_read(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Pfad angegeben");
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

/* ---- file_create ----------------------------------------------------- */

static int tool_file_create(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: kein Pfad angegeben");
        return 1;
    }
    /* Sicherheit: nur unter /home/user/ und /tmp/ erlaubt */
    if (strncmp(arg, "/home/user/", 11) != 0 &&
        strncmp(arg, "/tmp/", 5) != 0) {
        snprintf(out, cap,
                 "Fehler: Erstellen nur unter /home/user/ und /tmp/ erlaubt");
        return 1;
    }

    /* Format: "pfad|inhalt" -- | als Trennzeichen, \n im Inhalt werden zu echten Newlines */
    const char *sep = strchr(arg, '|');
    if (!sep) {
        /* Nur Pfad, leere Datei erstellen */
        FILE *f = fopen(arg, "w");
        if (!f) {
            snprintf(out, cap, "Fehler: Datei '%s' konnte nicht erstellt werden", arg);
            return 1;
        }
        fclose(f);
        snprintf(out, cap, "Datei '%s' erstellt (leer)", arg);
        return 1;
    }

    char path[512];
    size_t plen = (size_t)(sep - arg);
    if (plen >= sizeof(path)) plen = sizeof(path) - 1;
    memcpy(path, arg, plen);
    path[plen] = '\0';

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
    /* Sicherheit: nur unter /home/user/ erlaubt */
    if (strncmp(arg, "/home/user/", 11) != 0) {
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

/* ---- Dispatch -------------------------------------------------------- */

int flux_tool_exec(const char *name, const char *arg,
                   char *out, size_t out_cap) {
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
        "  alarm_set        -- Alarm setzen. ARG: HH:MM Beschreibung\n"
        "  reminder_set     -- Erinnerung setzen. ARG: Erinnerungstext\n"
        "  contacts_search  -- Kontakt suchen. ARG: Name oder Nummer\n"
        "  brightness_get   -- Bildschirmhelligkeit lesen. ARG: (leer)\n"
        "  brightness_set   -- Bildschirmhelligkeit setzen. ARG: 0-100 (Prozent)\n"
        "  wifi_info        -- WLAN-Signalstaerke und Interface. ARG: (leer)\n"
        "  vibrate          -- Geraet vibrieren lassen. ARG: Dauer in ms (z.B. 300)\n"
        "Verwende Tools NUR wenn Echtzeitdaten benoetigt werden (Wetter, Dateien, Berechnung usw.). "
        "Normale Fragen beantworte ohne Tools.";
}
