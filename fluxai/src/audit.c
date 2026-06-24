/* audit.c -- Aktions-Audit-Log (Ehrlichkeit/Nachvollziehbarkeit).
 *
 * Append-only Protokoll aller von der KI ausgefuehrten X:-Aktionen.
 * Siehe audit.h. KEINE Geheimnisse loggen.
 */
#include "audit.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define AUDIT_MAX_LINES 500

/* Kuerzt einen Text auf max Zeichen und ersetzt Newlines/CR durch ' ',
 * damit ein Eintrag immer EINE Zeile bleibt. */
static void audit_oneline(const char *in, char *out, size_t cap) {
    if (!in) in = "";
    size_t i = 0;
    for (; in[i] && i < cap - 1; i++) {
        char c = in[i];
        out[i] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    out[i] = '\0';
}

void flux_audit_log(const char *type, const char *desc, const char *result) {
    mkdir("/etc/flux", 0755);
    FILE *f = fopen(FLUX_AUDIT_PATH, "a");   /* append-only */
    if (!f) return;
    /* Restriktive Rechte wie bei anderen /etc/flux-Dateien (best effort). */
    chmod(FLUX_AUDIT_PATH, 0600);

    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &tm);

    char t_clean[32], d_clean[256], r_clean[512];
    audit_oneline(type   && type[0]   ? type   : "Aktion", t_clean, sizeof(t_clean));
    audit_oneline(desc   && desc[0]   ? desc   : "(ohne Angabe)", d_clean, sizeof(d_clean));
    audit_oneline(result && result[0] ? result : "(kein Ergebnis)", r_clean, sizeof(r_clean));

    fprintf(f, "[%s] %s: %s -> %s\n", ts, t_clean, d_clean, r_clean);
    fclose(f);

    /* Datei begrenzen: bei Bedarf auf die letzten AUDIT_MAX_LINES kuerzen
     * (gleiche Tail-Logik wie habits.c). */
    struct stat st;
    if (stat(FLUX_AUDIT_PATH, &st) == 0 && st.st_size > 128 * 1024) {
        FILE *rf = fopen(FLUX_AUDIT_PATH, "r");
        if (!rf) return;
        static char lines[AUDIT_MAX_LINES][800];
        int n = 0;
        char line[800];
        while (fgets(line, sizeof(line), rf)) {
            snprintf(lines[n % AUDIT_MAX_LINES], sizeof(lines[0]), "%s", line);
            n++;
        }
        fclose(rf);
        FILE *wf = fopen(FLUX_AUDIT_PATH, "w");
        if (!wf) return;
        int start = n > AUDIT_MAX_LINES ? n % AUDIT_MAX_LINES : 0;
        int total = n < AUDIT_MAX_LINES ? n : AUDIT_MAX_LINES;
        for (int i = 0; i < total; i++)
            fputs(lines[(start + i) % AUDIT_MAX_LINES], wf);
        fclose(wf);
        chmod(FLUX_AUDIT_PATH, 0600);
    }
}
