/* exec.h -- fuehrt eine vom Nutzer in flux-shell bestaetigte Aktion aus
 * (Mail/SMS/Anruf). Wird ueber den Request-Typ "X:" im Mini-Protokoll
 * angestossen (siehe common/flux_protocol.h, shell/src/action.h).
 */
#ifndef FLUX_EXEC_H
#define FLUX_EXEC_H

#include <stddef.h>

/* payload ist alles nach dem "X:"-Praefix, also
 * "<typ>\nTO:<empfaenger>\nSUBJECT:<betreff>\nBODY:\n<text...>".
 * Schreibt eine fuer den Nutzer lesbare Ergebnismeldung nach out. */
void flux_exec_action(const char *payload, char *out, size_t out_cap);

#endif
