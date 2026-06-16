/* mail.h -- echter E-Mail-Versand per SMTP (libcurl), Zugangsdaten aus
 * der Konfigurationsdatei (siehe common/flux_config.h, in Flux ueber
 * den Einstellungen-Bildschirm editierbar). Ohne konfiguriertes Konto:
 * ehrliche Fehlermeldung statt eines erfundenen "gesendet".
 */
#ifndef FLUX_MAIL_H
#define FLUX_MAIL_H

#include <stddef.h>

void flux_mail_send(const char *to, const char *subject, const char *body,
                     char *out, size_t out_cap);

#endif
