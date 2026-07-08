/* proactive.c -- Proactive AI notifications for Flux OS.
 *
 * Runs periodically from fluxaid's main loop (via select() timeout).
 * Prueft mehrere Bedingungen und schreibt ggf. eine Hinweis-Nachricht
 * nach /tmp/flux_proactive.txt, die der Sperrbildschirm bei jedem
 * Redraw liest:
 *   1. Akku < 20% und nicht ladend  -> deterministische Warnung (ohne KI)
 *   2. Termin in den naechsten 90 Minuten -> Hinweis (mit Restzeit)
 *   3. Geburtstag heute/morgen (aus memory.txt)
 *   4. Heutige/morgige Kalendertermine
 *   5. Schlechtes Wetter heute (Regen/Schnee/...) -> kurzer Hinweis
 * Faelle 2-5 werden zu EINER freundlichen KI-Nachricht zusammengefasst.
 */
#include "proactive.h"
#include "provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#define PROACTIVE_OUT   "/tmp/flux_proactive.txt"
#define PROACTIVE_STAMP "/tmp/flux_proactive_last"
#define BATTERY_STAMP   "/tmp/flux_proactive_bat"
#define MEMORY_PATH     "/etc/flux/memory.txt"
#define CALENDAR_PATH   "/etc/flux/calendar.txt"
#define WEATHER_CACHE   "/tmp/flux_weather.txt"

/* Returns 1 if a proactive check should run (not done in last 3600 seconds). */
static int should_run(void) {
    struct stat st;
    if (stat(PROACTIVE_STAMP, &st) != 0) return 1;
    time_t now = time(NULL);
    return (now - st.st_mtime) >= 3600;
}

static void touch_stamp(void) {
    FILE *f = fopen(PROACTIVE_STAMP, "w");
    if (f) { fputc('1', f); fclose(f); }
}

static void read_file_section(const char *path, char *out, size_t cap) {
    out[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t n = fread(out, 1, cap - 1, f);
    out[n] = '\0';
    fclose(f);
}

/* ---- Akku ------------------------------------------------------------- */

/* Liest Akkustand (0-100) und Ladezustand. Gibt 1 zurueck wenn lesbar. */
int flux_proactive_read_battery(int *pct, int *charging) {
    static const char *cap_paths[] = {
        "/sys/class/power_supply/battery/capacity",
        "/sys/class/power_supply/BAT0/capacity",
        "/sys/class/power_supply/BAT1/capacity",
        NULL
    };
    static const char *stat_paths[] = {
        "/sys/class/power_supply/battery/status",
        "/sys/class/power_supply/BAT0/status",
        "/sys/class/power_supply/BAT1/status",
        NULL
    };
    for (int i = 0; cap_paths[i]; i++) {
        FILE *f = fopen(cap_paths[i], "r");
        if (!f) continue;
        int v = -1;
        int ok = (fscanf(f, "%d", &v) == 1);
        fclose(f);
        if (!ok || v < 0) continue;
        *pct = v;
        *charging = 0;
        FILE *sf = fopen(stat_paths[i], "r");
        if (sf) {
            char s[32] = {0};
            if (fgets(s, sizeof(s), sf)) {
                if (strstr(s, "Charging") || strstr(s, "Full"))
                    *charging = 1;
            }
            fclose(sf);
        }
        return 1;
    }
    return 0;
}

/* Akku-Warnung hoechstens alle 30 Minuten erneuern (nicht spammen). */
static int battery_warn_due(void) {
    struct stat st;
    if (stat(BATTERY_STAMP, &st) != 0) return 1;
    return (time(NULL) - st.st_mtime) >= 1800;
}

/* ---- Wetter-Hinweis --------------------------------------------------- */

/* Erkennt grobe Schlechtwetter-Stichworte im gecachten Wetter-String.
 * Gibt einen Hinweistext zurueck oder NULL. */
const char *flux_proactive_weather_hint(const char *weather) {
    if (!weather || !weather[0]) return NULL;
    char low[256]; size_t i = 0;
    for (const char *p = weather; *p && i < sizeof(low)-1; p++, i++)
        low[i] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
    low[i] = '\0';
    if (strstr(low, "gewitter") || strstr(low, "thunder") || strstr(low, "sturm"))
        return "Heute Gewitter/Sturm moeglich -- plane Wege vorsichtig.";
    if (strstr(low, "schnee") || strstr(low, "snow"))
        return "Heute Schnee -- zieh dich warm an und plane mehr Zeit ein.";
    if (strstr(low, "regen") || strstr(low, "rain") ||
        strstr(low, "schauer") || strstr(low, "drizzle"))
        return "Heute Regen -- denk an einen Schirm.";
    return NULL;
}

/* ---- Geteilte Kontext-Helfer (auch von habits.c/journal.c genutzt) --- */

void flux_calendar_today(const char *today, char *out, size_t out_cap) {
    out[0] = '\0';
    FILE *cf = fopen(CALENDAR_PATH, "r");
    if (!cf) return;
    char line[256];
    while (fgets(line, sizeof(line), cf)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (strncmp(line, today, 10) != 0) continue;
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        size_t cl = strlen(out);
        snprintf(out + cl, out_cap - cl, "- %s\n", line + 11);
    }
    fclose(cf);
}

void flux_weather_read(char *out, size_t out_cap) {
    out[0] = '\0';
    FILE *wf = fopen(WEATHER_CACHE, "r");
    if (!wf) return;
    if (!fgets(out, out_cap, wf)) out[0] = '\0';
    fclose(wf);
    size_t l = strlen(out);
    while (l > 0 && (out[l-1] == '\n' || out[l-1] == '\r')) out[--l] = '\0';
}

void flux_proactive_check(const char *api_key, const char *model) {
    (void)model;

    /* --- 1. Akku-Warnung (deterministisch, hat Vorrang) --------------- */
    int pct = 0, charging = 0;
    if (flux_proactive_read_battery(&pct, &charging) && pct < 20 && !charging) {
        if (battery_warn_due()) {
            FILE *o = fopen(PROACTIVE_OUT, "w");
            if (o) {
                if (pct < 10)
                    fprintf(o, "Akku kritisch bei %d%% -- bitte sofort laden.\n", pct);
                else
                    fprintf(o, "Akku bei %d%% -- lade das Geraet bald auf.\n", pct);
                fclose(o);
            }
            FILE *bs = fopen(BATTERY_STAMP, "w");
            if (bs) { fputc('1', bs); fclose(bs); }
        }
        return; /* niedriger Akku ueberschreibt andere Hinweise */
    }

    if (!should_run()) return;
    if (!api_key || !*api_key) return; /* KI-Nachricht braucht einen Anbieter */
    touch_stamp(); /* Update stamp first to avoid repeated rapid checks */

    /* Get today's and tomorrow's dates */
    time_t now = time(NULL);
    struct tm tmnow; localtime_r(&now, &tmnow);
    char today[12], tomorrow[12], day_label[32];
    strftime(today, sizeof(today), "%Y-%m-%d", &tmnow);
    time_t tom = now + 86400;
    struct tm tmtom; localtime_r(&tom, &tmtom);
    strftime(tomorrow, sizeof(tomorrow), "%Y-%m-%d", &tmtom);
    strftime(day_label, sizeof(day_label), "%A, %d. %B %Y", &tmnow);
    int now_minutes = tmnow.tm_hour * 60 + tmnow.tm_min;

    /* Collect relevant memory entries (birthday keywords) */
    char memory[2048] = {0};
    read_file_section(MEMORY_PATH, memory, sizeof(memory));

    /* Collect calendar events for today/tomorrow + imminent (<= 90 min) */
    char cal_today[1024] = {0}, cal_tomorrow[1024] = {0}, imminent[512] = {0};
    FILE *cf = fopen(CALENDAR_PATH, "r");
    if (cf) {
        char line[256];
        while (fgets(line, sizeof(line), cf)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            size_t l = strlen(line);
            while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
            if (!line[0]) continue;
            if (strncmp(line, today, 10) == 0) {
                size_t cl = strlen(cal_today);
                snprintf(cal_today + cl, sizeof(cal_today) - cl, "  %s\n", line);
                /* Zeit HH:MM direkt hinter "YYYY-MM-DD " parsen */
                int hh = 0, mm = 0;
                if (strlen(line) >= 16 && sscanf(line + 11, "%d:%d", &hh, &mm) == 2) {
                    int ev_min = hh * 60 + mm;
                    int diff = ev_min - now_minutes;
                    if (diff >= 0 && diff <= 90) {
                        const char *desc = (strlen(line) > 17) ? line + 17 : line;
                        size_t il = strlen(imminent);
                        snprintf(imminent + il, sizeof(imminent) - il,
                                 "  in %d Min: %s\n", diff, desc);
                    }
                }
            } else if (strncmp(line, tomorrow, 10) == 0) {
                size_t cl = strlen(cal_tomorrow);
                snprintf(cal_tomorrow + cl, sizeof(cal_tomorrow) - cl, "  %s\n", line);
            }
        }
        fclose(cf);
    }

    /* Scan memory for birthday pattern matching today/tomorrow */
    char birthday_hint[512] = {0};
    if (memory[0]) {
        int td_d = tmnow.tm_mday, td_m = tmnow.tm_mon + 1;
        int tm_d = tmtom.tm_mday, tm_m = tmtom.tm_mon + 1;
        char *p = memory;
        char line[256];
        while (*p) {
            char *nl = strchr(p, '\n');
            size_t ll = nl ? (size_t)(nl - p) : strlen(p);
            if (ll >= sizeof(line)) ll = sizeof(line) - 1;
            memcpy(line, p, ll); line[ll] = '\0';
            p += ll; if (*p == '\n') p++;

            char lower[256]; size_t li = 0;
            for (const char *q = line; *q && li < sizeof(lower)-1; q++, li++)
                lower[li] = (*q >= 'A' && *q <= 'Z') ? *q + 32 : *q;
            lower[li] = '\0';
            if (!strstr(lower, "geburtstag") && !strstr(lower, "birthday")) continue;

            for (int i = 0; line[i+4]; i++) {
                if (line[i+2] == '.' && line[i+5] == '.') {
                    int d = (line[i]-'0')*10 + (line[i+1]-'0');
                    int m = (line[i+3]-'0')*10 + (line[i+4]-'0');
                    if ((d == td_d && m == td_m) || (d == tm_d && m == tm_m)) {
                        size_t bl = strlen(birthday_hint);
                        const char *when = (d == td_d && m == td_m) ? "heute" : "morgen";
                        snprintf(birthday_hint + bl, sizeof(birthday_hint) - bl,
                                 "  Geburtstag %s: %s\n", when, line);
                        break;
                    }
                }
            }
        }
    }

    /* Wetter-Hinweis aus Cache */
    char weather[256] = {0};
    {
        FILE *wf = fopen(WEATHER_CACHE, "r");
        if (wf) { if (!fgets(weather, sizeof(weather), wf)) weather[0] = '\0'; fclose(wf); }
        size_t wl = strlen(weather);
        while (wl > 0 && (weather[wl-1] == '\n' || weather[wl-1] == '\r')) weather[--wl] = '\0';
    }
    const char *weather_hint = flux_proactive_weather_hint(weather);

    /* Anything to report? */
    if (!cal_today[0] && !cal_tomorrow[0] && !birthday_hint[0] &&
        !imminent[0] && !weather_hint) {
        unlink(PROACTIVE_OUT);
        return;
    }

    /* Build context for AI */
    char ctx[4096];
    size_t pos = 0;
    pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                    "Heutiges Datum: %s (%s)\n", today, day_label);
    if (imminent[0])
        pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                        "BALD anstehende Termine (wichtig):\n%s", imminent);
    if (birthday_hint[0])
        pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                        "Geburtstags-Erinnerungen:\n%s", birthday_hint);
    if (cal_today[0])
        pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                        "Heutige Termine:\n%s", cal_today);
    if (cal_tomorrow[0])
        pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                        "Termine morgen:\n%s", cal_tomorrow);
    if (weather_hint)
        pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                        "Wetter-Hinweis: %s\n", weather_hint);
    if (memory[0])
        pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                        "\nKI-Gedaechtnis (Kurzreferenz):\n%.800s", memory);

    char question[5120];
    snprintf(question, sizeof(question),
             "%s\n\nSchreibe EINE kurze, freundliche Benachrichtigung (max. 2 Saetze) "
             "fuer den Sperrbildschirm. Weise auf das Wichtigste hin (bald anstehende "
             "Termine haben Vorrang, dann Geburtstage, dann Wetter). Fang direkt mit "
             "der Nachricht an, keine Einleitung.",
             ctx);

    char answer[512] = {0};
    flux_provider_ask_ephemeral(question, answer, sizeof(answer));

    if (!answer[0] || strncmp(answer, "Kein Cloud", 10) == 0) return;

    const char *msg = answer;
    if (strncmp(msg, "ACTION:", 7) == 0) {
        unlink(PROACTIVE_OUT);
        return;
    }
    FILE *out = fopen(PROACTIVE_OUT, "w");
    if (!out) return;
    fprintf(out, "%s\n", msg);
    fclose(out);
}
