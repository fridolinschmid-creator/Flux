#ifndef FLUX_TTS_H
#define FLUX_TTS_H

/* tts.h -- Lokale Sprachausgabe (Text-to-Speech) fuer Flux OS.
 *
 * Bewusst als EIN austauschbares Backend geschnitten: die Shell ruft nur
 * flux_tts_speak() auf, das eigentliche TTS-Backend lebt komplett in tts.c.
 * Damit kann das Stub-Backend spaeter ohne UI-/Aufruf-Aenderung gegen ein
 * echtes lokales neuronales TTS (piper) getauscht werden.
 *
 * EHRLICH: QEMU `virt` hat KEIN Audiogeraet -- es gibt nichts, worueber
 * abgespielt werden koennte. flux_tts_available() meldet das wahrheits-
 * gemaess; flux_tts_speak() gibt dann lautlos auf statt eine Wiedergabe
 * vorzutaeuschen. Local-first/Privacy: kein Cloud-TTS.
 */

/* 1, wenn lokales TTS prinzipiell moeglich ist (TTS-Binary + Audiogeraet
 * vorhanden), sonst 0. In QEMU ohne Audiogeraet immer 0. */
int flux_tts_available(void);

/* Liest den letzten Grund, warum TTS nicht verfuegbar ist (deutsche
 * Klartextmeldung, z.B. "kein Audiogeraet fuer Sprachausgabe"). Nie NULL. */
const char *flux_tts_unavailable_reason(void);

/* Liest den Text laut vor -- aber NUR, wenn der Einstellungs-Schalter
 * `tts=1` aktiv ist UND echte Wiedergabe-Hardware/-Backend vorhanden ist.
 * Fehlt beides, passiert ehrlich nichts (kein Fake). Non-blocking
 * (Wiedergabe laeuft im Kindprozess). */
void flux_tts_speak(const char *text);

#endif
