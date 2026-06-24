#ifndef FLUX_WAKEWORD_H
#define FLUX_WAKEWORD_H

/* wakeword.h -- Wake-Word-Erkennung "Hey Flux" fuer Flux OS.
 *
 * Das Wake-Word gehoert zur immer-lauschenden SHELL-Seite (nicht zum
 * Daemon): es soll -- wie der Mikrofon-Knopf -- den Assistenten-Eingabe-
 * modus aktivieren. Deshalb lebt es hier in der Shell und wird aus der
 * bestehenden Event-Loop periodisch gepollt.
 *
 * Schnittstelle bewusst minimal und backend-neutral: init / poll / deinit.
 * Das eigentliche Erkennungs-Backend (Audio-Stream + Modell) ist in
 * wakeword.c gekapselt und austauschbar.
 *
 * EHRLICH: QEMU `virt` hat KEIN Mikrofon -- es gibt keinen Audio-Stream,
 * der ein Wake-Word enthalten koennte. flux_wakeword_init() meldet das
 * wahrheitsgemaess und flux_wakeword_poll() liefert dann NIE einen Treffer
 * (kein Fake-Trigger). Local-first/Privacy: kein Cloud-Wake-Word.
 */

/* Initialisiert die Wake-Word-Erkennung. Liest selbst den Config-Schalter
 * `wakeword` (Default aus) -- ist er aus oder fehlt Audio-Hardware, bleibt
 * das Modul inaktiv. Gibt 1 zurueck, wenn aktiv lauschend, sonst 0.
 * Mehrfachaufruf ist erlaubt (z.B. nach Settings-Aenderung). */
int flux_wakeword_init(void);

/* Prueft (nicht-blockierend), ob seit dem letzten Aufruf das Wake-Word
 * "Hey Flux" erkannt wurde. Gibt 1 bei Erkennung zurueck, sonst 0.
 * In QEMU ohne Mikrofon: immer 0. */
int flux_wakeword_poll(void);

/* 1, wenn das Modul aktiv lauscht (Schalter an UND Audio vorhanden). */
int flux_wakeword_active(void);

/* Gibt Ressourcen frei und stoppt das Lauschen. */
void flux_wakeword_deinit(void);

#endif
