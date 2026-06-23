/* voice_unlock.h -- Stimm-Entsperrung als zweiter Faktor.
 *
 * Architektur-Ehrlichkeit:
 *  - QEMU virt hat kein Audiogeraet. Alle Funktionen pruefenden das
 *    zuerst und geben VOICE_UNLOCK_NO_MIC zurueck, falls kein Aufnahme-
 *    binaer (arecord/ffmpeg) gefunden wird.
 *  - Das Referenz-Embedding wird als einfacher MFCC-Fingerabdruck
 *    gespeichert (/etc/flux/voice_ref.dat). Das ist kein vollstaendiges
 *    ECAPA-TDNN/x-vector-Modell -- der Stub ist als austauschbares
 *    Backend geschnitten, sodass ein echtes Modell nur diese Datei
 *    ersetzt.
 *  - Die Stimm-Entsperrung ist NIE alleiniger Faktor: sie wird nur nach
 *    korrektem PIN angeboten und kann jederzeit per PIN uebersprungen werden.
 */
#ifndef FLUX_VOICE_UNLOCK_H
#define FLUX_VOICE_UNLOCK_H

#include <stddef.h>

typedef enum {
    VOICE_UNLOCK_OK        = 0,   /* Stimme verifiziert */
    VOICE_UNLOCK_NO_MIC    = 1,   /* kein Audiogeraet verfuegbar */
    VOICE_UNLOCK_NO_REF    = 2,   /* noch kein Referenz-Embedding eingelernt */
    VOICE_UNLOCK_MISMATCH  = 3,   /* Stimme stimmt nicht ueberein */
    VOICE_UNLOCK_ERROR     = 4,   /* sonstiger Fehler */
} voice_unlock_result_t;

/* Gibt 1 zurueck wenn Hardware + Software vorhanden (arecord + Audiogeraet). */
int voice_unlock_available(void);

/* Gibt 1 zurueck wenn ein Referenz-Embedding vorhanden ist. */
int voice_unlock_enrolled(void);

/* Nimmt 3 Sekunden Sprache auf und speichert das Fingerabdruck-Referenz-
 * Embedding. out_msg wird mit einer benutzerfreundlichen Meldung befuellt.
 * Gibt VOICE_UNLOCK_OK bei Erfolg. */
voice_unlock_result_t voice_unlock_enroll(char *out_msg, size_t msg_cap);

/* Nimmt 3 Sekunden Sprache auf und vergleicht mit dem Referenz-Embedding.
 * out_msg wird mit einer benutzerfreundlichen Meldung befuellt. */
voice_unlock_result_t voice_unlock_verify(char *out_msg, size_t msg_cap);

/* Loescht das gespeicherte Referenz-Embedding. */
void voice_unlock_delete_ref(void);

#define VOICE_REF_PATH "/etc/flux/voice_ref.dat"

#endif /* FLUX_VOICE_UNLOCK_H */
