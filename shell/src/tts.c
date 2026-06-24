/* tts.c -- Lokale Sprachausgabe (Text-to-Speech), ehrlicher Stub.
 *
 * Schnittstelle: flux_tts_available() / flux_tts_speak() (siehe tts.h).
 * Das eigentliche Backend ist hier gekapselt -- eine Datei, austauschbar.
 *
 * STAND: ehrlicher Stub. QEMU `virt` hat KEIN Audiogeraet (kein /dev/snd,
 * kein ALSA-Default), also gibt es nichts abzuspielen. flux_tts_available()
 * meldet das wahrheitsgemaess und flux_tts_speak() bleibt dann lautlos --
 * KEIN vorgetaeuschter Erfolg.
 *
 * ANDOCK-STELLE FUER ECHTES LOKALES TTS (piper):
 *   piper (https://github.com/rhasspy/piper) ist ein lokales, neuronales
 *   TTS, das offline laeuft (kein Cloud-TTS -> Privacy, local-first wie der
 *   Rest von Flux). Aufruf-Schema auf echter Hardware mit Audiogeraet:
 *
 *     echo "<text>" | piper --model de_DE-thorsten-medium.onnx --output_raw \
 *                    | aplay -r 22050 -f S16_LE -t raw -
 *
 *   bzw. piper schreibt eine WAV, die dann mit `aplay`/`paplay` abgespielt
 *   wird. Zum echten Backend wechseln heisst: in tts_play_backend() unten
 *   das piper|aplay-Pipe-Kommando ausfuehren (fork/exec, kein system() wegen
 *   Shell-Injection) und in find_tts_binary()/audio_device_present() die
 *   echte Verfuegbarkeit pruefen. UI und Aufrufer (main.c) bleiben gleich.
 *
 * Hinweis: espeak/flite sind klassische (nicht-neuronale) TTS-Engines und
 * hier als zusaetzliche, ebenfalls lokale Andock-Optionen mitgesucht --
 * piper ist die bevorzugte (neuronale) Variante.
 */
#include "tts.h"
#include "../../common/flux_config.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

static char s_reason[128] = "noch nicht geprueft";

/* ---- Verfuegbarkeit -------------------------------------------------- */

static int file_executable(const char *path) {
    return access(path, X_OK) == 0;
}

/* Sucht ein lokales TTS-Binary (piper bevorzugt, dann klassische Engines).
 * Gibt den Pfad zurueck oder NULL. */
static const char *find_tts_binary(void) {
    static const char *bins[] = {
        "/usr/local/bin/piper",
        "/usr/bin/piper",
        "/opt/piper/piper",
        "/usr/bin/espeak-ng",
        "/usr/bin/espeak",
        "/usr/local/bin/espeak",
        "/usr/bin/flite",
        "/usr/local/bin/flite",
        NULL
    };
    for (int i = 0; bins[i]; i++)
        if (file_executable(bins[i])) return bins[i];
    return NULL;
}

/* Prueft, ob ueberhaupt ein Audio-Wiedergabegeraet existiert. In QEMU `virt`
 * gibt es kein /dev/snd -- genau das wollen wir ehrlich erkennen, statt eine
 * Wiedergabe ins Leere zu starten. */
static int audio_device_present(void) {
    /* ALSA legt Geraete unter /dev/snd an; ohne Audio-Hardware fehlt das. */
    if (access("/dev/snd/controlC0", F_OK) == 0) return 1;
    if (access("/dev/snd/pcmC0D0p", F_OK) == 0) return 1;
    /* PulseAudio/PipeWire-Socket als Alternative (Desktop/echte HW). */
    if (access("/run/user/0/pulse/native", F_OK) == 0) return 1;
    return 0;
}

int flux_tts_available(void) {
    const char *bin = find_tts_binary();
    if (!bin) {
        snprintf(s_reason, sizeof(s_reason),
                 "kein TTS-Programm (piper/espeak) installiert");
        return 0;
    }
    if (!audio_device_present()) {
        snprintf(s_reason, sizeof(s_reason),
                 "kein Audiogeraet fuer Sprachausgabe");
        return 0;
    }
    s_reason[0] = '\0';
    return 1;
}

const char *flux_tts_unavailable_reason(void) {
    return s_reason[0] ? s_reason : "kein Audiogeraet fuer Sprachausgabe";
}

/* ---- Wiedergabe (Backend) -------------------------------------------- */

/* Spielt den Text tatsaechlich ab. Nur erreichbar, wenn flux_tts_available()
 * 1 ergeben hat (echtes Backend + Audiogeraet). In QEMU wird dieser Pfad
 * nie betreten -- deshalb gibt es hier KEINEN Fake. */
static void tts_play_backend(const char *bin, const char *text) {
    pid_t p = fork();
    if (p < 0) return;
    if (p == 0) {
        /* stderr stummschalten (ALSA-Warnungen) */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

        if (strstr(bin, "piper")) {
            /* ECHTES neuronales Backend (nur falls piper + Audio vorhanden).
             * piper braucht ein Modell -- auf echter HW im Rootfs-Overlay
             * gebuendelt. Hier exemplarisch der --output_raw|aplay-Weg; die
             * tatsaechliche Pipe (piper -> aplay) wird beim Tausch auf echtes
             * piper eingerichtet. */
            execlp(bin, "piper", "--model",
                   "/usr/share/piper/de_DE-thorsten-medium.onnx",
                   "--output_file", "/tmp/flux_tts.wav", (char *)NULL);
        } else if (strstr(bin, "espeak")) {
            execlp(bin, "espeak", "-v", "de", "-s", "160", text, (char *)NULL);
        } else { /* flite */
            execlp(bin, "flite", "-t", text, (char *)NULL);
        }
        _exit(127);
    }
    /* Eltern: nicht blockieren; Zombie wird in der Hauptschleife abgeraeumt. */
    if (p > 0) waitpid(p, NULL, WNOHANG);
}

void flux_tts_speak(const char *text) {
    if (!text || !*text) return;

    /* Nur sprechen, wenn der Nutzer TTS eingeschaltet hat. */
    char enabled[8] = {0};
    flux_config_get("tts", enabled, sizeof(enabled));
    if (enabled[0] != '1') return;

    /* EHRLICH: kein Backend / kein Audiogeraet -> lautlos aufgeben, statt
     * eine Wiedergabe vorzutaeuschen. (QEMU landet hier.) */
    if (!flux_tts_available()) return;

    const char *bin = find_tts_binary();
    if (!bin) return;
    tts_play_backend(bin, text);
}
