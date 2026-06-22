/* flux_log.c -- Zentrales strukturiertes Logging fuer Flux OS.
 *
 * Log-Format: [2026-06-21 14:23:01.456] [INFO ] [module] Nachricht
 * Log-Datei:  /var/log/flux/flux.log
 * Rotation:   >5 MB -> flux.log.1/.2/.3 (Backups behalten)
 */
#include "flux_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdarg.h>
#include <unistd.h>

#define LOG_DIR      "/var/log/flux"
#define LOG_PATH     "/var/log/flux/flux.log"
#define LOG_MAX_BYTES (5 * 1024 * 1024)   /* 5 MB */
#define LOG_BACKUPS  3

flux_log_level_t flux_log_min_level = FLUX_LOG_INFO;

static FILE        *s_logfile   = NULL;
static char         s_module[64] = "flux";
static flux_log_error_hook_t s_error_hook = NULL;
static int          s_in_hook   = 0;   /* Reentranz-Schutz */

void flux_log_set_error_hook(flux_log_error_hook_t fn) { s_error_hook = fn; }

static const char *level_name(flux_log_level_t l) {
    switch (l) {
        case FLUX_LOG_TRACE: return "TRACE";
        case FLUX_LOG_DEBUG: return "DEBUG";
        case FLUX_LOG_INFO:  return "INFO ";
        case FLUX_LOG_WARN:  return "WARN ";
        case FLUX_LOG_ERROR: return "ERROR";
        case FLUX_LOG_FATAL: return "FATAL";
        default:             return "?????";
    }
}

const char *flux_log_level_name(flux_log_level_t l) { return level_name(l); }

static void rotate_logs(void) {
    char old_path[256], new_path[256];
    for (int i = LOG_BACKUPS - 1; i >= 1; i--) {
        snprintf(old_path, sizeof(old_path), "%s.%d", LOG_PATH, i);
        snprintf(new_path, sizeof(new_path), "%s.%d", LOG_PATH, i + 1);
        rename(old_path, new_path);
    }
    snprintf(new_path, sizeof(new_path), "%s.1", LOG_PATH);
    rename(LOG_PATH, new_path);
}

static void ensure_open(void) {
    if (s_logfile) return;
    mkdir(LOG_DIR, 0755);
    s_logfile = fopen(LOG_PATH, "a");
}

void flux_log_init(const char *module_name) {
    if (module_name && module_name[0])
        snprintf(s_module, sizeof(s_module), "%s", module_name);

    mkdir(LOG_DIR, 0755);

    if (!s_logfile)
        s_logfile = fopen(LOG_PATH, "a");

    if (s_logfile) {
        flux_log(FLUX_LOG_INFO, "=== Flux OS gestartet (pid=%d) ===", (int)getpid());
    }
}

void flux_log_close(void) {
    if (s_logfile) {
        fclose(s_logfile);
        s_logfile = NULL;
    }
}

void flux_log(flux_log_level_t level, const char *fmt, ...) {
    if (level < flux_log_min_level) return;

    ensure_open();

    /* Millisekunden-Timestamp */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tmv;
    localtime_r(&tv.tv_sec, &tmv);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* In Logfile schreiben */
    if (s_logfile) {
        fprintf(s_logfile, "[%s.%03d] [%s] [%s] %s\n",
                ts, (int)(tv.tv_usec / 1000), level_name(level), s_module, msg);
        fflush(s_logfile);

        /* Rotation pruefen */
        long pos = ftell(s_logfile);
        if (pos > LOG_MAX_BYTES) {
            fclose(s_logfile);
            s_logfile = NULL;
            rotate_logs();
            s_logfile = fopen(LOG_PATH, "a");
        }
    }

    /* ERROR und FATAL auch auf stderr */
    if (level >= FLUX_LOG_ERROR) {
        fprintf(stderr, "[%s] [%s] %s\n", level_name(level), s_module, msg);
    }

    /* Optionaler Fehler-Hook (Toast in der Shell / Backend-Report im Daemon).
     * Reentranz-geschuetzt, damit ein Log-Aufruf im Hook nicht rekursiv wird. */
    if (level >= FLUX_LOG_ERROR && s_error_hook && !s_in_hook) {
        s_in_hook = 1;
        s_error_hook(level, s_module, msg);
        s_in_hook = 0;
    }
}
