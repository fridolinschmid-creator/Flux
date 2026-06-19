/* habits.c -- Passives Kontext-Lernen fuer Flux OS.
 *
 * Flux speichert im Hintergrund, welche Screens der Nutzer oeffnet und
 * welche Themen er der KI stellt. Daraus entsteht ein Nutzungsprofil
 * das zu einem personalisierten Morgen-Briefing fuehrt.
 *
 * Format /etc/flux/habits.txt (eine Zeile pro Eintrag):
 *   [YYYY-MM-DD HH:MM] SCREEN | TOPIC
 *
 * Das Morgen-Briefing (07:00-09:00, einmal taeglich) liest die letzten
 * 14 Tage und generiert eine 2-3-saetzige "Guten Morgen"-Nachricht.
 */
#include "habits.h"
#include "provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define HABITS_PATH  "/etc/flux/habits.txt"
#define BRIEF_STAMP  "/tmp/flux_morning_done"
#define PROACTIVE_OUT "/tmp/flux_proactive.txt"
#define HABITS_MAX_LINES 500

/* ---- Logging --------------------------------------------------------- */

void flux_habits_log(const char *screen_name, const char *topic) {
    if (!screen_name || !screen_name[0]) return;

    mkdir("/etc/flux", 0755);
    FILE *f = fopen(HABITS_PATH, "a");
    if (!f) return;

    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &tm);

    if (topic && topic[0]) {
        /* Thema auf 80 Zeichen kuerzen */
        char short_topic[84];
        snprintf(short_topic, sizeof(short_topic), "%.80s", topic);
        /* Newlines entfernen */
        for (char *p = short_topic; *p; p++) if (*p == '\n') *p = ' ';
        fprintf(f, "[%s] %s | %s\n", ts, screen_name, short_topic);
    } else {
        fprintf(f, "[%s] %s\n", ts, screen_name);
    }
    fclose(f);

    /* Datei auf max HABITS_MAX_LINES Zeilen begrenzen (tail -n) */
    struct stat st;
    if (stat(HABITS_PATH, &st) == 0 && st.st_size > 64 * 1024) {
        /* Lade alle Zeilen, behalte letzte HABITS_MAX_LINES */
        FILE *rf = fopen(HABITS_PATH, "r");
        if (!rf) return;
        char lines[HABITS_MAX_LINES][128];
        int n = 0;
        char line[128];
        while (fgets(line, sizeof(line), rf)) {
            memcpy(lines[n % HABITS_MAX_LINES], line, sizeof(lines[0])-1);
            lines[n % HABITS_MAX_LINES][sizeof(lines[0])-1] = '\0';
            n++;
        }
        fclose(rf);
        FILE *wf = fopen(HABITS_PATH, "w");
        if (!wf) return;
        int start = n > HABITS_MAX_LINES ? n % HABITS_MAX_LINES : 0;
        int total = n < HABITS_MAX_LINES ? n : HABITS_MAX_LINES;
        for (int i = 0; i < total; i++)
            fputs(lines[(start + i) % HABITS_MAX_LINES], wf);
        fclose(wf);
    }
}

/* ---- Morning Briefing ------------------------------------------------ */

int flux_habits_should_brief(void) {
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    if (tm.tm_hour < 7 || tm.tm_hour >= 10) return 0;

    /* Einmal pro Tag */
    struct stat st;
    if (stat(BRIEF_STAMP, &st) == 0) {
        /* Stamp existiert -- pruefen ob von heute */
        struct tm stm; localtime_r(&st.st_mtime, &stm);
        if (stm.tm_yday == tm.tm_yday && stm.tm_year == tm.tm_year) return 0;
    }
    return 1;
}

void flux_habits_morning_briefing(const char *api_key, const char *model) {
    (void)model;
    if (!api_key || !*api_key) return;
    if (!flux_habits_should_brief()) return;

    /* Stamp setzen (vor API-Call um race conditions zu vermeiden) */
    FILE *sf = fopen(BRIEF_STAMP, "w");
    if (sf) { fputc('1', sf); fclose(sf); }

    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    char date_label[64];
    strftime(date_label, sizeof(date_label), "%A, %d. %B %Y", &tm);
    char today[12];
    strftime(today, sizeof(today), "%Y-%m-%d", &tm);

    /* Nutzungsgewohnheiten der letzten 14 Tage analysieren */
    char habit_summary[2048] = {0};
    FILE *hf = fopen(HABITS_PATH, "r");
    if (hf) {
        /* Screens zaehlen */
        int cnt_assistant = 0, cnt_calendar = 0, cnt_contacts = 0,
            cnt_files = 0, cnt_gallery = 0, cnt_settings = 0;
        char last_topics[5][80]; int lt_n = 0;
        char line[128];
        while (fgets(line, sizeof(line), hf)) {
            if (strstr(line, "assistant")) cnt_assistant++;
            if (strstr(line, "calendar"))  cnt_calendar++;
            if (strstr(line, "contacts"))  cnt_contacts++;
            if (strstr(line, "files"))     cnt_files++;
            if (strstr(line, "gallery"))   cnt_gallery++;
            if (strstr(line, "settings"))  cnt_settings++;
            /* Letzte Themen sammeln */
            char *pipe = strchr(line, '|');
            if (pipe && lt_n < 5) {
                char *topic = pipe + 2;
                size_t tl = strlen(topic);
                while (tl > 0 && (topic[tl-1]=='\n'||topic[tl-1]=='\r')) topic[--tl]='\0';
                if (topic[0]) {
                    snprintf(last_topics[lt_n], sizeof(last_topics[0]), "%s", topic);
                    lt_n++;
                }
            }
        }
        fclose(hf);
        size_t pos = 0;
        pos += snprintf(habit_summary + pos, sizeof(habit_summary) - pos,
                        "Nutzungsgewohnheiten: ");
        if (cnt_assistant) pos += snprintf(habit_summary + pos, sizeof(habit_summary) - pos,
                                           "Assistent (%dx), ", cnt_assistant);
        if (cnt_calendar)  pos += snprintf(habit_summary + pos, sizeof(habit_summary) - pos,
                                           "Kalender (%dx), ", cnt_calendar);
        if (cnt_contacts)  pos += snprintf(habit_summary + pos, sizeof(habit_summary) - pos,
                                           "Kontakte (%dx), ", cnt_contacts);
        if (cnt_files)     pos += snprintf(habit_summary + pos, sizeof(habit_summary) - pos,
                                           "Dateien (%dx), ", cnt_files);
        if (cnt_gallery)   pos += snprintf(habit_summary + pos, sizeof(habit_summary) - pos,
                                           "Fotos (%dx), ", cnt_gallery);
        if (lt_n > 0) {
            pos += snprintf(habit_summary + pos, sizeof(habit_summary) - pos,
                            "\nLetzte Themen: ");
            for (int i = 0; i < lt_n; i++)
                pos += snprintf(habit_summary + pos, sizeof(habit_summary) - pos,
                                "'%s' ", last_topics[i]);
        }
    }

    /* Heutige Termine */
    char cal_today[1024] = {0};
    FILE *cf = fopen("/etc/flux/calendar.txt", "r");
    if (cf) {
        char line[256];
        while (fgets(line, sizeof(line), cf)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            if (strncmp(line, today, 10) == 0) {
                size_t l = strlen(line);
                while (l > 0 && (line[l-1]=='\n'||line[l-1]=='\r')) line[--l]='\0';
                size_t cl = strlen(cal_today);
                snprintf(cal_today + cl, sizeof(cal_today) - cl, "- %s\n", line + 11);
            }
        }
        fclose(cf);
    }

    /* Gedaechtnis-Auszug (max 600 Zeichen) */
    char memory[640] = {0};
    FILE *mf = fopen("/etc/flux/memory.txt", "r");
    if (mf) { size_t n = fread(memory, 1, sizeof(memory)-1, mf); memory[n]='\0'; fclose(mf); }

    /* Wetter */
    char weather[120] = {0};
    FILE *wf = fopen("/tmp/flux_weather.txt", "r");
    if (wf) { if (!fgets(weather, sizeof(weather), wf)) weather[0]='\0'; fclose(wf); }
    size_t wl = strlen(weather);
    while (wl > 0 && (weather[wl-1]=='\n'||weather[wl-1]=='\r')) weather[--wl]='\0';

    /* KI-Anfrage */
    char question[4096];
    size_t pos = 0;
    pos += snprintf(question + pos, sizeof(question) - pos,
                    "Erstelle eine warme, persoenliche Morgen-Begruessung (2-3 Saetze) "
                    "fuer den Nutzer. Heute ist %s.\n\n", date_label);
    if (weather[0])
        pos += snprintf(question + pos, sizeof(question) - pos,
                        "Wetter: %s\n", weather);
    if (cal_today[0])
        pos += snprintf(question + pos, sizeof(question) - pos,
                        "Heutige Termine:\n%s\n", cal_today);
    if (memory[0])
        pos += snprintf(question + pos, sizeof(question) - pos,
                        "Persoenliches (aus Gedaechtnis):\n%.400s\n\n", memory);
    if (habit_summary[0])
        pos += snprintf(question + pos, sizeof(question) - pos,
                        "%s\n\n", habit_summary);
    pos += snprintf(question + pos, sizeof(question) - pos,
                    "Beginne mit 'Guten Morgen' und weise auf den wichtigsten "
                    "Termin hin. Rede den Nutzer persoenlich an wenn du seinen Namen kennst. "
                    "Nur der Begrüssungstext, keine Erklaerungen.");

    char answer[512] = {0};
    flux_provider_ask_ephemeral(question, answer, sizeof(answer));

    if (!answer[0] || strncmp(answer, "ACTION:", 7) == 0) return;

    FILE *out = fopen(PROACTIVE_OUT, "w");
    if (!out) return;
    fprintf(out, "%s\n", answer);
    fclose(out);
}
