/* journal.c -- Daily AI journal generation for Flux OS.
 *
 * Each day between 22:00 and 23:59, generates a Markdown journal entry
 * by asking the AI to reflect on the day based on available context:
 * - Today's calendar events
 * - Any new memory entries added today
 * - Weather (from cache)
 *
 * Output: /home/user/Journal/YYYY-MM-DD.md
 * Guard: /tmp/flux_journal_YYYY-MM-DD (one entry per day)
 */
#include "journal.h"
#include "provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define JOURNAL_DIR "/home/user/Journal"

static int should_write_journal(const char *today) {
    char stamp[128];
    snprintf(stamp, sizeof(stamp), "/tmp/flux_journal_%s", today);
    FILE *f = fopen(stamp, "r");
    if (f) { fclose(f); return 0; }
    return 1;
}

static void mark_journal_done(const char *today) {
    char stamp[128];
    snprintf(stamp, sizeof(stamp), "/tmp/flux_journal_%s", today);
    FILE *f = fopen(stamp, "w");
    if (f) { fputc('1', f); fclose(f); }
}

void flux_journal_check(const char *api_key, const char *model) {
    (void)model;
    if (!api_key || !*api_key) return;

    time_t now = time(NULL);
    struct tm tmnow; localtime_r(&now, &tmnow);

    /* Only run between 22:00 and 23:59 */
    if (tmnow.tm_hour < 22) return;

    char today[12];
    strftime(today, sizeof(today), "%Y-%m-%d", &tmnow);

    if (!should_write_journal(today)) return;
    mark_journal_done(today);

    mkdir(JOURNAL_DIR, 0755);

    /* Gather context */
    char date_label[64];
    strftime(date_label, sizeof(date_label), "%A, %d. %B %Y", &tmnow);

    /* Today's calendar events */
    char cal_today[1024] = {0};
    FILE *cf = fopen("/etc/flux/calendar.txt", "r");
    if (cf) {
        char line[256];
        while (fgets(line, sizeof(line), cf)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            if (strncmp(line, today, 10) == 0) {
                size_t l = strlen(line);
                while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
                size_t cl = strlen(cal_today);
                snprintf(cal_today + cl, sizeof(cal_today) - cl, "- %s\n", line + 11);
            }
        }
        fclose(cf);
    }

    /* Memory entries added today */
    char mem_today[1024] = {0};
    FILE *mf = fopen("/etc/flux/memory.txt", "r");
    if (mf) {
        char line[256];
        while (fgets(line, sizeof(line), mf)) {
            /* Lines starting with [YYYY-MM-DD matching today */
            if (line[0] == '[' && strncmp(line + 1, today, 10) == 0) {
                size_t l = strlen(line);
                while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
                size_t cl = strlen(mem_today);
                snprintf(mem_today + cl, sizeof(mem_today) - cl, "- %s\n", line);
            }
        }
        fclose(mf);
    }

    /* Weather */
    char weather[160] = {0};
    FILE *wf = fopen("/tmp/flux_weather.txt", "r");
    if (wf) { if (!fgets(weather, sizeof(weather), wf)) weather[0] = '\0'; fclose(wf); }
    size_t wl = strlen(weather);
    while (wl > 0 && (weather[wl-1] == '\n' || weather[wl-1] == '\r')) weather[--wl] = '\0';

    /* Build AI question */
    char question[4096];
    size_t pos = 0;
    pos += snprintf(question + pos, sizeof(question) - pos,
                    "Schreibe einen kurzen Tagebucheintrag fuer heute (%s) auf Deutsch. "
                    "Stil: freundlich, reflektierend, 3-5 Saetze. "
                    "Beginne mit dem Datum als Markdown-Ueberschrift. "
                    "Baue die folgenden Infos ein, falls vorhanden:\n\n", date_label);
    if (weather[0])
        pos += snprintf(question + pos, sizeof(question) - pos, "Wetter: %s\n\n", weather);
    if (cal_today[0])
        pos += snprintf(question + pos, sizeof(question) - pos,
                        "Heutige Termine:\n%s\n", cal_today);
    if (mem_today[0])
        pos += snprintf(question + pos, sizeof(question) - pos,
                        "Heute Neues gemerkt:\n%s\n", mem_today);
    if (!cal_today[0] && !mem_today[0])
        pos += snprintf(question + pos, sizeof(question) - pos,
                        "(Keine besonderen Ereignisse verzeichnet. "
                        "Schreibe einen ruhigen, allgemeinen Tageseintrag.)\n");
    pos += snprintf(question + pos, sizeof(question) - pos,
                    "\nAntworte NUR mit dem Tagebucheintrag, keine Erklaerungen.");

    char answer[1024] = {0};
    flux_provider_ask(question, answer, sizeof(answer));

    if (!answer[0] || strncmp(answer, "ACTION:", 7) == 0) return;

    /* Write journal entry */
    char out_path[256];
    snprintf(out_path, sizeof(out_path), "%s/%s.md", JOURNAL_DIR, today);
    FILE *out = fopen(out_path, "w");
    if (!out) return;
    fprintf(out, "%s\n", answer);
    fclose(out);

    /* Write notification to lockscreen */
    FILE *nf = fopen("/tmp/flux_proactive.txt", "w");
    if (nf) {
        fprintf(nf, "Dein Tagebucheintrag fuer heute wurde gespeichert: Journal/%s.md\n", today);
        fclose(nf);
    }
}
