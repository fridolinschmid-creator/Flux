/* alarm.c -- Alarm- und Timer-Ueberwachung fuer fluxaid.
 *
 * flux_alarm_check() wird vom Daemon-Hauptloop aufgerufen (alle 30 s).
 *
 * /tmp/flux_alarms.txt  Format: "YYYY-MM-DD HH:MM Beschreibung\n"
 * /tmp/flux_timers.txt  Format: "UNIX_TIMESTAMP Beschreibung\n"
 *
 * Faellige Eintraege werden an /tmp/flux_alarm_trigger.txt angehaengt
 * (flux-shell liest diese Datei und zeigt den Alert); danach aus der
 * Quelldatei entfernt (rename-atomare Technik).
 */
#include "alarm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ALARMS_FILE   "/tmp/flux_alarms.txt"
#define TIMERS_FILE   "/tmp/flux_timers.txt"
#define TRIGGER_FILE  "/tmp/flux_alarm_trigger.txt"

static void append_trigger(const char *msg) {
    FILE *f = fopen(TRIGGER_FILE, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

static int check_alarms(time_t now) {
    FILE *f = fopen(ALARMS_FILE, "r");
    if (!f) return 0;

    char tmppath[128];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", ALARMS_FILE);
    FILE *tf = fopen(tmppath, "w");
    if (!tf) { fclose(f); return 0; }

    char line[256];
    int triggered = 0;

    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (!line[0]) continue;

        /* Format: YYYY-MM-DD HH:MM description */
        int yr = 0, mo = 0, dy = 0, hh = 0, mm = 0;
        if (sscanf(line, "%d-%d-%d %d:%d", &yr, &mo, &dy, &hh, &mm) == 5) {
            struct tm ta = {0};
            ta.tm_year  = yr - 1900;
            ta.tm_mon   = mo - 1;
            ta.tm_mday  = dy;
            ta.tm_hour  = hh;
            ta.tm_min   = mm;
            ta.tm_sec   = 0;
            ta.tm_isdst = -1;
            time_t alarm_t = mktime(&ta);
            if (alarm_t != (time_t)-1 && alarm_t <= now) {
                /* Beschreibung hinter "YYYY-MM-DD HH:MM " (17 Zeichen) */
                const char *desc = (l > 17) ? line + 17 : "Wecker";
                char msg[280];
                snprintf(msg, sizeof(msg), "Wecker: %s (%02d:%02d)", desc, hh, mm);
                append_trigger(msg);
                triggered++;
                continue; /* nicht zurueckschreiben */
            }
        }
        fprintf(tf, "%s\n", line);
    }
    fclose(f);
    fclose(tf);
    if (triggered) rename(tmppath, ALARMS_FILE);
    else           remove(tmppath);
    return triggered;
}

static int check_timers(time_t now) {
    FILE *f = fopen(TIMERS_FILE, "r");
    if (!f) return 0;

    char tmppath[128];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", TIMERS_FILE);
    FILE *tf = fopen(tmppath, "w");
    if (!tf) { fclose(f); return 0; }

    char line[256];
    int triggered = 0;

    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (!line[0]) continue;

        /* Format: UNIX_TIMESTAMP description */
        long long ts = 0;
        int scanned = 0;
        if (sscanf(line, "%lld%n", &ts, &scanned) == 1 && scanned > 0) {
            if ((time_t)ts <= now) {
                const char *desc = (l > (size_t)scanned + 1)
                                   ? line + scanned + 1 : "Timer";
                char msg[280];
                snprintf(msg, sizeof(msg), "Timer abgelaufen: %s", desc);
                append_trigger(msg);
                triggered++;
                continue;
            }
        }
        fprintf(tf, "%s\n", line);
    }
    fclose(f);
    fclose(tf);
    if (triggered) rename(tmppath, TIMERS_FILE);
    else           remove(tmppath);
    return triggered;
}

int flux_alarm_check(void) {
    time_t now = time(NULL);
    int n = check_alarms(now);
    n += check_timers(now);
    return n;
}
