/* wakeword.c -- Wake-Word "Hey Flux", ehrlicher Stub.
 *
 * Schnittstelle: init / poll / deinit (siehe wakeword.h). Das eigentliche
 * Erkennungs-Backend ist hier gekapselt -- eine Datei, austauschbar.
 *
 * STAND: ehrlicher Stub. QEMU `virt` hat KEIN Mikrofon (kein arecord/ffmpeg
 * mit echtem Audio-Default, kein /dev/snd-Capture), also gibt es keinen
 * Audio-Stream, in dem ein Wake-Word gesucht werden koennte.
 * flux_wakeword_poll() liefert deshalb NIE einen Treffer -- KEIN
 * Fake-Trigger. Das Modul wird nur "aktiv", wenn der Nutzer den Schalter
 * `wakeword=on` setzt UND eine Aufnahme-Quelle vorhanden ist; in QEMU
 * bleibt es ehrlich inaktiv.
 *
 * ANDOCK-STELLE FUER ECHTE WAKE-WORD-ERKENNUNG (openWakeWord / porcupine):
 *   On-Device-Wake-Word, das lokal/offline laeuft (kein Cloud-Wake-Word ->
 *   Privacy, local-first wie der Rest von Flux). Realistischer Aufbau:
 *     1. Kontinuierlicher 16-kHz-Mono-Mikrofon-Stream (arecord/ALSA, schon
 *        in voice.c vorhanden) in einen Ringpuffer.
 *     2. Pro ~80-ms-Fenster Mel-Features bilden und durch ein kleines ONNX-
 *        Modell schicken:
 *          - openWakeWord (https://github.com/dscripka/openWakeWord):
 *            "hey_flux.onnx" via ONNX Runtime (C-API) inferieren,
 *            Schwellwert ~0.5 auf der Aktivierung.
 *          - alternativ Picovoice porcupine (C-SDK, "Hey Flux"-Keyword).
 *     3. Bei Aktivierung s_triggered = 1 setzen -- den Rest macht main.c
 *        (gleicher Pfad wie der Mikrofon-Knopf).
 *   Zum echten Backend wechseln heisst: in dieser Datei den Stream + die
 *   ONNX-Inferenz ergaenzen und s_triggered echt setzen. Schnittstelle,
 *   Settings-Toggle und die Verdrahtung in main.c bleiben unveraendert.
 */
#include "wakeword.h"
#include "voice.h"
#include "../../common/flux_config.h"

#include <string.h>

static int s_active    = 0;   /* 1 = Schalter an UND Audio-Quelle vorhanden */
static int s_triggered = 0;   /* vom Backend gesetzt, sobald "Hey Flux" erkannt */

int flux_wakeword_init(void) {
    s_active = 0;
    s_triggered = 0;

    /* Schalter aus / nicht gesetzt -> inaktiv (Default aus). */
    char on[8] = {0};
    flux_config_get("wakeword", on, sizeof(on));
    if (strcmp(on, "on") != 0) return 0;

    /* EHRLICH: ohne Aufnahme-Quelle (Mikrofon) gibt es keinen Stream zum
     * Lauschen -- wir bleiben dann inaktiv, statt eine Erkennung
     * vorzutaeuschen. flux_voice_can_record() ist die gleiche ehrliche
     * Hardware-Pruefung wie beim Mikrofon-Knopf. In QEMU `virt`: 0. */
    if (!flux_voice_can_record()) return 0;

    /* ANDOCK-STELLE: hier wuerde das echte Backend den kontinuierlichen
     * Audio-Stream oeffnen und das ONNX-Wake-Word-Modell laden
     * (openWakeWord/porcupine). Solange nur der Stub laeuft, starten wir
     * bewusst keinen Stream -- es gaebe nichts zu erkennen. */
    s_active = 1;
    return 1;
}

int flux_wakeword_poll(void) {
    if (!s_active) return 0;

    /* ANDOCK-STELLE: hier wuerde das echte Backend die seit dem letzten
     * Aufruf eingegangenen Audio-Fenster durch das Modell schicken und bei
     * Aktivierung s_triggered = 1 setzen. Im Stub bleibt s_triggered immer
     * 0 -> ehrlich kein Treffer, kein Fake-Trigger. */
    if (s_triggered) {
        s_triggered = 0;   /* Treffer einmalig melden */
        return 1;
    }
    return 0;
}

int flux_wakeword_active(void) {
    return s_active;
}

void flux_wakeword_deinit(void) {
    /* ANDOCK-STELLE: hier wuerde das echte Backend den Audio-Stream
     * schliessen und das Modell freigeben. */
    s_active = 0;
    s_triggered = 0;
}
