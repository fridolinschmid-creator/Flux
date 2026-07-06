/* notification.c -- Hintergrund-Benachrichtigungsdienst fuer Flux OS.
 *
 * Dieser Thread prueft alle 15 Minuten ob eine der folgenden Bedingungen
 * zutrifft und schreibt ggf. einen Hinweis nach FLUX_NOTIF_FILE:
 *
 *  - Kalendertermin in den naechsten 60 Minuten
 *  - Geburtstag morgen (aus Memory)
 *  - Batterie unter 20%
 *  - Proaktive KI-Nachricht vorhanden (/tmp/flux_proactive.txt)
 *
 * Jede Benachrichtigung hat das Format:
 *   [YYYY-MM-DD HH:MM] TEXT
 *
 * Die Datei enthaelt maximal NOTIF_MAX Eintraege; aeltere werden
 * verdraengt. Ein Badge-Counter (erste Zeile "COUNT:N") ermoeglicht
 * der Shell, schnell die Anzahl ungelesener Hinweise zu ermitteln.
 */
#include "notification.h"
#include "../../common/flux_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

#define NOTIF_MAX        10
#define CHECK_INTERVAL   900   /* 15 Minuten in Sekunden */
#define MEMORY_PATH      "/etc/flux/memory.txt"
#define CALENDAR_PATH    "/etc/flux/calendar.txt"
#define BATTERY_SYS      "/sys/class/power_supply/BAT0/capacity"
#define PROACTIVE_FILE   "/tmp/flux_proactive.txt"
#define ALARM_FILE       "/tmp/flux_alarms.txt"
#define ALARM_RING       "/tmp/flux_alarm_ring.txt"

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---- Hilfsfunktionen ------------------------------------------------- */

static void timestamp(char *buf, size_t cap) {
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    strftime(buf, cap, "%Y-%m-%d %H:%M", &tm);
}

static int battery_level(void) {
    FILE *f = fopen(BATTERY_SYS, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

/* Schreibt einen neuen Eintrag vorne in die Datei.
 * Behaelt maximal NOTIF_MAX Zeilen. Thread-sicher. */
static void write_notification(const char *msg) {
    pthread_mutex_lock(&s_mutex);

    /* Bestehende Eintraege lesen */
    char lines[NOTIF_MAX][256];
    int n = 0;
    FILE *f = fopen(FLUX_NOTIF_FILE, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f) && n < NOTIF_MAX) {
            /* Skip COUNT: header line */
            if (strncmp(line, "COUNT:", 6) == 0) continue;
            if (line[0] == '\n') continue;
            size_t l = strlen(line);
            while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
            snprintf(lines[n], sizeof(lines[n]), "%s", line); n++;
        }
        fclose(f);
    }

    /* Neuen Eintrag vorne einsetzen, Rest um 1 nach hinten */
    int keep = (n < NOTIF_MAX - 1) ? n : NOTIF_MAX - 1;
    char ts[32]; timestamp(ts, sizeof(ts));
    char newline[256]; snprintf(newline, sizeof(newline), "[%s] %s", ts, msg);

    f = fopen(FLUX_NOTIF_FILE, "w");
    if (f) {
        fprintf(f, "COUNT:%d\n", keep + 1);
        fprintf(f, "%s\n", newline);
        for (int i = 0; i < keep; i++) fprintf(f, "%s\n", lines[i]);
        fclose(f);
    }

    pthread_mutex_unlock(&s_mutex);
}

/* ---- Pruef-Logik ------------------------------------------------------- */

static void check_battery(void) {
    int lvl = battery_level();
    if (lvl >= 0 && lvl < 20) {
        char msg[64]; snprintf(msg, sizeof(msg), "Batterie bei %d%% -- bitte laden.", lvl);
        write_notification(msg);
    }
}

static void check_proactive(void) {
    FILE *f = fopen(PROACTIVE_FILE, "r");
    if (!f) return;
    char line[256]; line[0] = '\0';
    if (!fgets(line, sizeof(line), f)) line[0] = '\0';
    fclose(f);
    size_t l = strlen(line);
    while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
    if (l > 0) {
        write_notification(line);
        remove(PROACTIVE_FILE);
    }
}

/* Prueft Kalendertermine in den naechsten 60 Minuten */
static void check_calendar(void) {
    FILE *f = fopen(CALENDAR_PATH, "r");
    if (!f) return;
    time_t now = time(NULL);
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        /* Format: YYYY-MM-DD HH:MM Beschreibung */
        struct tm ev = {0};
        char desc[200] = {0};
        if (sscanf(line, "%4d-%2d-%2d %2d:%2d %199[^\n]",
                   &ev.tm_year, &ev.tm_mon, &ev.tm_mday,
                   &ev.tm_hour, &ev.tm_min, desc) >= 5) {
            ev.tm_year -= 1900; ev.tm_mon -= 1; ev.tm_isdst = -1;
            time_t ev_t = mktime(&ev);
            long diff = (long)(ev_t - now);
            if (diff >= 0 && diff <= 3600) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Bald: %s (in %ld Min.)", desc, diff / 60);
                write_notification(msg);
            }
        }
    }
    fclose(f);
}

/* Prueft Geburtstage morgen aus Memory */
static void check_birthdays(void) {
    FILE *f = fopen(MEMORY_PATH, "r");
    if (!f) return;
    time_t tomorrow = time(NULL) + 86400;
    struct tm tmv; localtime_r(&tomorrow, &tmv);
    char month_day[8]; strftime(month_day, sizeof(month_day), "%m-%d", &tmv);
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char lower[256]; size_t li = 0;
        for (const char *p = line; *p && li < sizeof(lower)-1; p++, li++)
            lower[li] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
        lower[li] = '\0';
        if ((strstr(lower, "geburtstag") || strstr(lower, "birthday"))
                && strstr(line, month_day)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
            char msg[280]; snprintf(msg, sizeof(msg), "Morgen Geburtstag: %s", line);
            write_notification(msg);
        }
    }
    fclose(f);
}

/* ---- Alarm-Ausloesedienst --------------------------------------------- */

/* Prueft /tmp/flux_alarms.txt, loest faellige Alarme aus und schreibt
 * sie nach ALARM_RING damit die Shell eine Vollbild-Meldung zeigen kann. */
static void check_alarms(void) {
    FILE *f = fopen(ALARM_FILE, "r");
    if (!f) return;

    time_t now = time(NULL);
    char kept[64][256]; int nkept = 0;
    char ring[8][256];  int nring = 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (!line[0]) continue;

        struct tm at = {0};
        char desc[200] = {0};
        int y = 0, mo = 0, d = 0, h = 0, mi = 0;
        if (sscanf(line, "%4d-%2d-%2d %2d:%2d %199[^\n]",
                   &y, &mo, &d, &h, &mi, desc) >= 5) {
            at.tm_year = y - 1900; at.tm_mon = mo - 1; at.tm_mday = d;
            at.tm_hour = h; at.tm_min = mi; at.tm_isdst = -1;
            time_t at_t = mktime(&at);
            long diff = (long)(now - at_t);
            /* Ausloesen wenn Alarm faellig (bis 10 Minuten Toleranz) */
            if (diff >= 0 && diff < 600) {
                if (nring < 8)
                    snprintf(ring[nring++], 255, "%02d:%02d %s", h, mi, desc);
                continue;
            }
        }
        if (nkept < 64) {
            snprintf(kept[nkept], sizeof(kept[nkept]), "%s", line); nkept++;
        }
    }
    fclose(f);

    if (nring > 0) {
        /* Klingeldatei schreiben -- Shell liest diese und zeigt Vollbild */
        FILE *rf = fopen(ALARM_RING, "w");
        if (rf) {
            for (int i = 0; i < nring; i++) fprintf(rf, "%s\n", ring[i]);
            fclose(rf);
        }
        /* Auch als normale Benachrichtigung pushen */
        for (int i = 0; i < nring; i++) {
            char msg[280];
            snprintf(msg, sizeof(msg), "Wecker: %s", ring[i]);
            write_notification(msg);
        }
        /* Ausgeloeste Alarme aus der Datei entfernen */
        FILE *wf = fopen(ALARM_FILE, "w");
        if (wf) {
            for (int i = 0; i < nkept; i++) fprintf(wf, "%s\n", kept[i]);
            fclose(wf);
        }
    }
}

/* ---- Thread ---------------------------------------------------------- */

static void *notification_thread(void *arg) {
    (void)arg;
    /* Erste Pruefung nach 60 Sekunden (Boot-Delay) */
    sleep(60);
    int round = 0;
    for (;;) {
        check_alarms(); /* jede Minute */
        if (round % 15 == 0) {
            check_calendar();
            check_birthdays();
            check_battery();
            check_proactive();
        }
        round++;
        sleep(60);
    }
    return NULL;
}

void flux_notification_start(void) {
    pthread_t tid;
    pthread_create(&tid, NULL, notification_thread, NULL);
    pthread_detach(tid);
}

void flux_notification_push(const char *msg) {
    write_notification(msg);
}

int flux_notification_count(void) {
    FILE *f = fopen(FLUX_NOTIF_FILE, "r");
    if (!f) return 0;
    char line[32]; int n = 0;
    if (fgets(line, sizeof(line), f) && strncmp(line, "COUNT:", 6) == 0)
        n = atoi(line + 6);
    fclose(f);
    return n;
}
