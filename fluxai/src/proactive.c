/* proactive.c -- Proactive AI notifications for Flux OS.
 *
 * Runs periodically from fluxaid's main loop (via select() timeout).
 * Scans memory.txt for birthday mentions and calendar.txt for events
 * happening today or tomorrow, then asks the AI for a short, helpful
 * notification message. The result is written to /tmp/flux_proactive.txt
 * which the shell's lockscreen reads on each redraw.
 */
#include "proactive.h"
#include "provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define PROACTIVE_OUT   "/tmp/flux_proactive.txt"
#define PROACTIVE_STAMP "/tmp/flux_proactive_last"
#define MEMORY_PATH     "/etc/flux/memory.txt"
#define CALENDAR_PATH   "/etc/flux/calendar.txt"

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

void flux_proactive_check(const char *api_key, const char *model) {
    if (!should_run()) return;
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

    /* Collect relevant memory entries (birthday keywords) */
    char memory[2048] = {0};
    read_file_section(MEMORY_PATH, memory, sizeof(memory));

    /* Collect relevant calendar events for today and tomorrow */
    char cal_today[1024] = {0}, cal_tomorrow[1024] = {0};
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
            } else if (strncmp(line, tomorrow, 10) == 0) {
                size_t cl = strlen(cal_tomorrow);
                snprintf(cal_tomorrow + cl, sizeof(cal_tomorrow) - cl, "  %s\n", line);
            }
        }
        fclose(cf);
    }

    /* Also scan memory for birthday pattern matching today/tomorrow */
    char birthday_hint[512] = {0};
    if (memory[0]) {
        /* Extract day/month from today and tomorrow */
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

            /* Check for "geburtstag" and matching day/month */
            char lower[256]; size_t li = 0;
            for (const char *q = line; *q && li < sizeof(lower)-1; q++, li++)
                lower[li] = (*q >= 'A' && *q <= 'Z') ? *q + 32 : *q;
            lower[li] = '\0';
            if (!strstr(lower, "geburtstag") && !strstr(lower, "birthday")) continue;

            /* Try to find DD.MM pattern in the line */
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

    /* Anything to report? */
    if (!cal_today[0] && !cal_tomorrow[0] && !birthday_hint[0]) {
        /* Nothing relevant -- clear old message */
        unlink(PROACTIVE_OUT);
        return;
    }

    /* Build context for AI */
    char ctx[4096];
    size_t pos = 0;
    pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                    "Heutiges Datum: %s (%s)\n", today, day_label);
    if (birthday_hint[0])
        pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                        "Geburtstags-Erinnerungen:\n%s", birthday_hint);
    if (cal_today[0])
        pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                        "Heutige Termine:\n%s", cal_today);
    if (cal_tomorrow[0])
        pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                        "Termine morgen:\n%s", cal_tomorrow);
    if (memory[0])
        pos += snprintf(ctx + pos, sizeof(ctx) - pos,
                        "\nKI-Gedaechtnis (Kurzreferenz):\n%.800s", memory);

    char question[5120];
    snprintf(question, sizeof(question),
             "%s\n\nSchreibe EINE kurze, freundliche Benachrichtigung (max. 2 Saetze) "
             "fuer den Sperrbildschirm. Weise auf den wichtigsten bevorstehenden Termin "
             "oder Geburtstag hin. Fang direkt mit der Nachricht an, keine Einleitung.",
             ctx);

    char answer[512] = {0};
    flux_provider_ask(question, answer, sizeof(answer));

    if (!answer[0] || strncmp(answer, "Kein Cloud", 10) == 0) return;

    /* Write to lockscreen notification file */
    FILE *out = fopen(PROACTIVE_OUT, "w");
    if (!out) return;
    /* Strip ACTION: prefix if AI misread the prompt */
    const char *msg = answer;
    if (strncmp(msg, "ACTION:", 7) == 0) {
        fclose(out);
        unlink(PROACTIVE_OUT);
        return;
    }
    fprintf(out, "%s\n", msg);
    fclose(out);
}
