/* email.h -- echter SMTP-Mailversand (kein Mock), nur wenn der Nutzer
 * eigene Zugangsdaten per Umgebungsvariable setzt -- exakt dasselbe
 * Prinzip wie FLUX_AI_API_KEY in provider.h: ohne Konfiguration eine
 * ehrliche Fehlermeldung statt eines erfundenen "Mail gesendet".
 */
#ifndef FLUX_EMAIL_H
#define FLUX_EMAIL_H

#include <stddef.h>

/* to/subject/body duerfen kein Newline enthalten (to/subject) bzw.
 * werden 1:1 als Mailkoerper uebernommen (body). out enthaelt immer
 * eine lesbare Antwort. Gibt 1 bei Erfolg, 0 bei Fehler zurueck. */
int flux_email_send(const char *to, const char *subject, const char *body,
                     char *out, size_t out_cap);

#endif
