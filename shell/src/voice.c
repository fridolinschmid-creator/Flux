/* voice.c -- Spracheingabe fuer Flux OS.
 *
 * Aufnahme: arecord (ALSA) oder ffmpeg, 16 kHz Mono WAV.
 * Transkription: whisper-cli (whisper.cpp) mit deutschem Sprachmodell.
 *
 * Beide Schritte sind optional -- fehlen die Binaerdateien oder das
 * Modell, gibt flux_voice_can_record() / flux_voice_can_transcribe()
 * 0 zurueck und der Aufrufer zeigt eine freundliche Fehlermeldung.
 *
 * Modell-Pfade (in Reihenfolge probiert):
 *   /usr/share/whisper/ggml-small.bin
 *   /usr/local/share/whisper/ggml-small.bin
 *   /home/user/whisper/ggml-small.bin
 *   /opt/whisper/ggml-small.bin
 *   /tmp/ggml-small.bin
 */
#include "voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define VOICE_WAV  "/tmp/flux_voice.wav"
#define VOICE_TXT  "/tmp/flux_voice.txt"

static pid_t s_rec_pid = 0;

/* ---- Hilfsfunktionen ------------------------------------------------- */

static int file_executable(const char *path) {
    return access(path, X_OK) == 0;
}

static int file_readable(const char *path) {
    return access(path, R_OK) == 0;
}

static const char *find_recorder(void) {
    static const char *recorders[] = {
        "/usr/bin/arecord",
        "/usr/local/bin/arecord",
        "/usr/bin/ffmpeg",
        "/usr/local/bin/ffmpeg",
        NULL
    };
    for (int i = 0; recorders[i]; i++)
        if (file_executable(recorders[i])) return recorders[i];
    return NULL;
}

static const char *find_whisper(void) {
    static const char *bins[] = {
        "/usr/local/bin/whisper-cli",
        "/usr/bin/whisper-cli",
        "/usr/local/bin/whisper",
        "/usr/bin/whisper",
        "/opt/whisper/whisper-cli",
        NULL
    };
    for (int i = 0; bins[i]; i++)
        if (file_executable(bins[i])) return bins[i];
    return NULL;
}

static const char *find_model(void) {
    static const char *models[] = {
        "/usr/share/whisper/ggml-small.bin",
        "/usr/local/share/whisper/ggml-small.bin",
        "/home/user/whisper/ggml-small.bin",
        "/opt/whisper/ggml-small.bin",
        "/tmp/ggml-small.bin",
        NULL
    };
    for (int i = 0; models[i]; i++)
        if (file_readable(models[i])) return models[i];
    return NULL;
}

/* ---- Public API ------------------------------------------------------ */

int flux_voice_can_record(void) {
    return find_recorder() != NULL;
}

int flux_voice_can_transcribe(void) {
    return find_whisper() != NULL && find_model() != NULL;
}

int flux_voice_start(void) {
    if (s_rec_pid > 0) return 1; /* already recording */

    const char *rec = find_recorder();
    if (!rec) return 0;

    unlink(VOICE_WAV);
    unlink(VOICE_TXT);

    pid_t pid = fork();
    if (pid < 0) return 0;

    if (pid == 0) {
        /* Child process */
        /* Redirect stderr to /dev/null to suppress ALSA warnings */
        FILE *devnull = fopen("/dev/null", "w");
        if (devnull) {
            dup2(fileno(devnull), 2);
            fclose(devnull);
        }

        if (strstr(rec, "arecord")) {
            /* arecord: 16 kHz, 16-bit, mono, max 60s */
            char *args[] = { "arecord", "-D", "default",
                             "-f", "S16_LE", "-r", "16000",
                             "-c", "1", "-d", "60",
                             VOICE_WAV, NULL };
            execv(rec, args);
        } else {
            /* ffmpeg fallback */
            char *args[] = { "ffmpeg", "-y",
                             "-f", "alsa", "-i", "default",
                             "-ar", "16000", "-ac", "1",
                             "-t", "60",
                             VOICE_WAV, NULL };
            execv(rec, args);
        }
        _exit(1);
    }

    s_rec_pid = pid;
    return 1;
}

int flux_voice_stop_and_transcribe(char *out, size_t out_cap) {
    out[0] = '\0';

    /* Stop recording */
    if (s_rec_pid > 0) {
        kill(s_rec_pid, SIGTERM);
        waitpid(s_rec_pid, NULL, 0);
        s_rec_pid = 0;
        usleep(300000); /* 300 ms fuer sauberen WAV-Header-Abschluss */
    }

    /* Check WAV exists and is non-trivial (> 44 Bytes Header + Daten) */
    struct stat st;
    if (stat(VOICE_WAV, &st) != 0 || st.st_size < 4096) return 0;

    const char *whisper = find_whisper();
    const char *model   = find_model();
    if (!whisper || !model) return 0;

    /* Whisper aufrufen:
     *   -m <model>   -- Modell-Pfad
     *   -f <file>    -- Eingabe-WAV
     *   -l de        -- Sprache Deutsch (spart Zeit vs. Auto-Detect)
     *   -nt          -- Kein Timestamp-Output
     *   -np          -- Kein Fortschrittsbalken
     *   --output-txt -- Textausgabe in VOICE_WAV + ".txt"
     * whisper-cli schreibt das Ergebnis als <inputfile>.txt */
    /* Kein system()/Shell -- direkt execv() um Shell-Injection zu vermeiden. */
    {
        char *argv[] = {
            (char *)whisper,
            "-m", (char *)model,
            "-f", (char *)VOICE_WAV,
            "-l", "de",
            "-nt", "-np",
            "--output-txt",
            NULL
        };
        pid_t pid = fork();
        if (pid == 0) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
            execv(whisper, argv);
            _exit(127);
        }
        if (pid > 0) waitpid(pid, NULL, 0);
    }

    /* Ergebnis lesen (whisper schreibt "<wav>.txt") */
    char txt_path[256];
    snprintf(txt_path, sizeof(txt_path), "%s.txt", VOICE_WAV);

    FILE *f = fopen(txt_path, "r");
    if (!f) return 0;
    size_t n = fread(out, 1, out_cap - 1, f);
    out[n] = '\0';
    fclose(f);

    /* Whitespace + Zeilenumbrueche am Ende entfernen */
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' ||
                     out[n-1] == ' '  || out[n-1] == '\t'))
        out[--n] = '\0';

    /* Whisper-Timestamp-Marker entfernen: "[00:00.000 --> 00:05.000]  Text" */
    if (out[0] == '[') {
        char clean[2048];
        size_t ci = 0;
        const char *p = out;
        while (*p && ci + 1 < out_cap) {
            if (*p == '[') {
                while (*p && *p != ']') p++;
                if (*p) p++; /* skip ']' */
                while (*p == ' ' || *p == '\n') p++;
                continue;
            }
            clean[ci++] = *p++;
        }
        clean[ci] = '\0';
        snprintf(out, out_cap, "%s", clean);
        n = strlen(out);
        while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' || out[n-1] == ' '))
            out[--n] = '\0';
    }

    return out[0] != '\0';
}

int flux_voice_is_recording(void) {
    return s_rec_pid > 0;
}

void flux_voice_cancel(void) {
    if (s_rec_pid > 0) {
        kill(s_rec_pid, SIGKILL);
        waitpid(s_rec_pid, NULL, WNOHANG);
        s_rec_pid = 0;
    }
    unlink(VOICE_WAV);
    unlink(VOICE_TXT);
}
