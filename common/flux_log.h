/* flux_log.h -- Zentrales strukturiertes Logging fuer flux-shell und fluxaid.
 *
 * Alle Ausgaben landen in /var/log/flux/flux.log mit automatischer Rotation
 * bei 5 MB (maximal 3 Backups). Zusaetzlich werden ERROR+ auf stderr gespiegelt.
 *
 * Verwendung:
 *   flux_log_init("mein-modul");
 *   LOGI("gestartet, pid=%d", getpid());
 *   LOGE("Fehler: %s", strerror(errno));
 */
#ifndef FLUX_LOG_H
#define FLUX_LOG_H

#include <stdarg.h>

typedef enum {
    FLUX_LOG_TRACE = 0,
    FLUX_LOG_DEBUG = 1,
    FLUX_LOG_INFO  = 2,
    FLUX_LOG_WARN  = 3,
    FLUX_LOG_ERROR = 4,
    FLUX_LOG_FATAL = 5,
} flux_log_level_t;

/* Initialisiert das Logging: erstellt /var/log/flux/ falls noetig,
 * oeffnet das Logfile. module_name wird in jede Zeile geschrieben.
 * Darf mehrfach aufgerufen werden (idempotent). */
void flux_log_init(const char *module_name);

/* Schliesst das Logfile. Wird normalerweise nicht benoetigt (atexit). */
void flux_log_close(void);

/* Schreibt einen Log-Eintrag. level bestimmt ob er erscheint (>= MIN_LEVEL). */
void flux_log(flux_log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Mindest-Log-Level (Standard: INFO). Aenderbar zur Laufzeit. */
extern flux_log_level_t flux_log_min_level;

/* Optionaler Hook, der bei jedem Eintrag mit Level >= ERROR aufgerufen wird
 * -- nachdem der Eintrag geschrieben wurde. Damit kann die Shell eine
 * Fehler-Benachrichtigung anzeigen und fluxaid den Fehler ans Backend
 * melden, ohne dass das Logging selbst diese Abhaengigkeiten kennt
 * (common bleibt entkoppelt). Reentranz ist abgesichert: ein Log-Aufruf
 * aus dem Hook heraus loest den Hook nicht erneut aus.
 * Setze fn = NULL, um den Hook zu entfernen. */
typedef void (*flux_log_error_hook_t)(flux_log_level_t level,
                                       const char *module,
                                       const char *message);
void flux_log_set_error_hook(flux_log_error_hook_t fn);

/* Klartext-Name eines Levels ("ERROR", "WARN " ...). */
const char *flux_log_level_name(flux_log_level_t level);

/* Bequem-Makros -- schreiben Datei:Zeile in TRACE/DEBUG. */
#define LOGT(fmt, ...) flux_log(FLUX_LOG_TRACE, fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) flux_log(FLUX_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) flux_log(FLUX_LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) flux_log(FLUX_LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) flux_log(FLUX_LOG_ERROR, fmt, ##__VA_ARGS__)
#define LOGF(fmt, ...) flux_log(FLUX_LOG_FATAL, fmt, ##__VA_ARGS__)

#endif /* FLUX_LOG_H */
