/* voice_unlock.c -- Stimm-Entsperrung als zweiter Faktor fuer Flux OS.
 *
 * Stub-Implementierung: zeichnet 3 Sekunden WAV auf und berechnet einen
 * einfachen Energie-Fingerabdruck (RMS pro 20ms-Frame). Das ist kein
 * echtes Speaker-Embedding (kein ECAPA-TDNN), sondern ein sauberer Stub,
 * der das Backend-Interface definiert -- austauschbar ohne UI-Aenderung.
 *
 * Warum kein echtes ML-Modell: ECAPA-TDNN/x-vector braucht ein ONNX- oder
 * GGML-Modell (~20-50 MB), ALSA-Zugriff und libsndfile. Das ist deutlich
 * mehr Infrastruktur als der aktuelle Codebase-Stand -- deshalb ehrlicher
 * Stub mit einem Hinweis. Auf echter Hardware mit Mikrofon wuerde man hier
 * ein kompaktes Embedding-Modell laden.
 */
#include "voice_unlock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define RECORD_WAV   "/tmp/flux_voice_unlock.wav"
#define RECORD_SECS  3

/* ---- Pruefe ob Aufnahme-Binaer vorhanden ----------------------------- */

static const char *find_recorder(void) {
    static const char *cands[] = {
        "/usr/bin/arecord",
        "/usr/local/bin/arecord",
        "/usr/bin/ffmpeg",
        "/usr/local/bin/ffmpeg",
        NULL
    };
    for (int i = 0; cands[i]; i++)
        if (access(cands[i], X_OK) == 0) return cands[i];
    return NULL;
}

static int record_wav(void) {
    const char *rec = find_recorder();
    if (!rec) return 0;

    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        /* Unterdruecke Ausgaben des Recorders */
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) { dup2(null, 1); dup2(null, 2); close(null); }
        if (strstr(rec, "arecord")) {
            execlp(rec, rec, "-d", "3", "-r", "16000", "-c", "1",
                   "-f", "S16_LE", RECORD_WAV, NULL);
        } else {
            /* ffmpeg */
            execlp(rec, rec, "-y", "-f", "alsa", "-i", "default",
                   "-t", "3", "-ar", "16000", "-ac", "1", RECORD_WAV, NULL);
        }
        _exit(1);
    }
    int status; waitpid(pid, &status, 0);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0)
           && access(RECORD_WAV, R_OK) == 0;
}

/* ---- Einfacher RMS-Fingerabdruck (Stub fuer echtes Embedding) -------- */

#define FRAMES     50   /* 50 x 20ms Fenster = 1 Sekunde */
#define FRAME_SAMPLES 320  /* 20ms @ 16kHz */

typedef struct { float rms[FRAMES]; } VoiceFingerprint;

static int compute_fingerprint(VoiceFingerprint *fp) {
    FILE *f = fopen(RECORD_WAV, "rb");
    if (!f) return 0;
    fseek(f, 44, SEEK_SET); /* WAV-Header ueberspringen */
    memset(fp, 0, sizeof(*fp));
    for (int i = 0; i < FRAMES; i++) {
        int16_t buf[FRAME_SAMPLES];
        size_t n = fread(buf, sizeof(int16_t), FRAME_SAMPLES, f);
        if (n == 0) break;
        double sum = 0;
        for (size_t j = 0; j < n; j++) sum += (double)buf[j] * buf[j];
        fp->rms[i] = (float)sqrt(sum / (n > 0 ? n : 1));
    }
    fclose(f);
    return 1;
}

static float cosine_similarity(const VoiceFingerprint *a, const VoiceFingerprint *b) {
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < FRAMES; i++) {
        dot += a->rms[i] * b->rms[i];
        na  += a->rms[i] * a->rms[i];
        nb  += b->rms[i] * b->rms[i];
    }
    if (na < 1e-9 || nb < 1e-9) return 0.0f;
    return (float)(dot / (sqrt(na) * sqrt(nb)));
}

/* Aehnlichkeitsschwelle: bei einem echten ECAPA-TDNN-Modell waere das
 * typischerweise 0.75-0.85. Fuer diesen RMS-Stub grosszuegiger. */
#define SIMILARITY_THRESHOLD 0.70f

/* ---- Public API ------------------------------------------------------ */

int voice_unlock_available(void) {
    return find_recorder() != NULL;
}

int voice_unlock_enrolled(void) {
    return access(VOICE_REF_PATH, R_OK) == 0;
}

voice_unlock_result_t voice_unlock_enroll(char *out_msg, size_t msg_cap) {
    if (!find_recorder()) {
        snprintf(out_msg, msg_cap,
                 "Kein Mikrofon erkannt (QEMU virt hat kein Audiogeraet). "
                 "Stimm-Entsperrung funktioniert nur auf echter Hardware.");
        return VOICE_UNLOCK_NO_MIC;
    }

    snprintf(out_msg, msg_cap, "Aufnahme laeuft (3 Sekunden)...");
    if (!record_wav()) {
        snprintf(out_msg, msg_cap, "Aufnahme fehlgeschlagen. Ist ein Mikrofon angeschlossen?");
        return VOICE_UNLOCK_ERROR;
    }

    VoiceFingerprint fp;
    if (!compute_fingerprint(&fp)) {
        snprintf(out_msg, msg_cap, "Verarbeitung fehlgeschlagen.");
        return VOICE_UNLOCK_ERROR;
    }

    mkdir("/etc/flux", 0755);
    FILE *f = fopen(VOICE_REF_PATH, "wb");
    if (!f) {
        snprintf(out_msg, msg_cap, "Konnte Referenz nicht speichern (Schreibfehler).");
        return VOICE_UNLOCK_ERROR;
    }
    size_t wn = fwrite(&fp, sizeof(fp), 1, f);
    fclose(f);
    if (wn != 1 || chmod(VOICE_REF_PATH, 0600) != 0) {
        snprintf(out_msg, msg_cap, "Konnte Referenz nicht speichern (Schreibfehler).");
        return VOICE_UNLOCK_ERROR;
    }

    snprintf(out_msg, msg_cap,
             "Stimme eingelernt. Bitte noch zweimal wiederholen fuer bessere "
             "Erkennungsgenauigkeit.");
    return VOICE_UNLOCK_OK;
}

voice_unlock_result_t voice_unlock_verify(char *out_msg, size_t msg_cap) {
    if (!find_recorder()) {
        snprintf(out_msg, msg_cap,
                 "Kein Mikrofon erkannt. PIN-Entsperrung verwenden.");
        return VOICE_UNLOCK_NO_MIC;
    }

    FILE *rf = fopen(VOICE_REF_PATH, "rb");
    if (!rf) {
        snprintf(out_msg, msg_cap, "Noch keine Stimme eingelernt.");
        return VOICE_UNLOCK_NO_REF;
    }
    VoiceFingerprint ref;
    size_t n = fread(&ref, sizeof(ref), 1, rf); fclose(rf);
    if (n != 1) {
        snprintf(out_msg, msg_cap, "Referenz-Datei beschaedigt.");
        return VOICE_UNLOCK_ERROR;
    }

    snprintf(out_msg, msg_cap, "Sprechen Sie bitte (3 Sekunden)...");
    if (!record_wav()) {
        snprintf(out_msg, msg_cap, "Aufnahme fehlgeschlagen.");
        return VOICE_UNLOCK_ERROR;
    }

    VoiceFingerprint live;
    if (!compute_fingerprint(&live)) {
        snprintf(out_msg, msg_cap, "Verarbeitung fehlgeschlagen.");
        return VOICE_UNLOCK_ERROR;
    }

    float sim = cosine_similarity(&ref, &live);
    if (sim >= SIMILARITY_THRESHOLD) {
        snprintf(out_msg, msg_cap, "Stimme erkannt (%.0f%% Aehnlichkeit).", sim * 100.0f);
        return VOICE_UNLOCK_OK;
    }

    snprintf(out_msg, msg_cap,
             "Stimme nicht erkannt (%.0f%% < %.0f%% Schwelle). "
             "Bitte PIN verwenden.", sim * 100.0f, SIMILARITY_THRESHOLD * 100.0f);
    return VOICE_UNLOCK_MISMATCH;
}

void voice_unlock_delete_ref(void) {
    remove(VOICE_REF_PATH);
}
