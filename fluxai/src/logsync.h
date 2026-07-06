/* logsync.h -- Logs/Fehler an ein konfigurierbares Backend senden.
 *
 * Privacy by construction: Egress ist strikt opt-in. Es passiert nichts,
 * solange in /etc/flux/flux.conf kein `log_backend_url` gesetzt ist.
 * Das Backend ist anbieter-/dienstunabhaengig (eine HTTP-URL, wie bei
 * `searxng_url`); ein minimaler Referenz-Server liegt unter
 * tools/flux-log-server.py.
 *
 * Endpunkte (relativ zu log_backend_url):
 *   POST <url>/ingest   -- kompletter Logfile-Inhalt (Sammel-Upload)
 *   POST <url>/report   -- ein einzelnes Fehler-Ereignis (Auto-Report)
 * Beide tragen den Header  X-Flux-Device: <hostname>.
 */
#ifndef FLUX_LOGSYNC_H
#define FLUX_LOGSYNC_H

#include <stddef.h>

/* Laedt das aktuelle Logfile (/var/log/flux/flux.log) hoch.
 * Schreibt eine menschenlesbare, EHRLICHE Statusmeldung nach out
 * (Erfolg mit Byte-Zahl, "kein Backend konfiguriert", Netzwerkfehler ...).
 * Gibt 1 bei Erfolg, 0 bei Fehler/Abbruch zurueck. */
int flux_logsync_upload(char *out, size_t out_cap);

/* Meldet ein einzelnes Fehler-Ereignis an <url>/report (fuer Auto-Report
 * direkt bei Auftreten). No-op (Rueckgabe 0), wenn kein Backend gesetzt
 * ist. Schreibt KEINE UI-Meldung; nur fuers Hintergrund-Reporting. */
int flux_logsync_report(const char *level, const char *module, const char *message);

#endif
