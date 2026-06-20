/* imap.h -- Lesezugriff auf das Postfach via IMAP (libcurl, imaps://).
 *
 * Ergaenzt mail.c (das nur SMTP-Versand kann) um das Abrufen ungelesener
 * Nachrichten und das Lesen einer einzelnen Mail. Damit kann die KI Fragen
 * wie "Fasse meine ungelesenen Mails zusammen" beantworten.
 *
 * Konfiguration (in /etc/flux/flux.conf):
 *   imap_host  -- Server (z.B. imap.gmail.com)  [erforderlich]
 *   imap_port  -- Port (Standard 993, implizites TLS)
 *   imap_user  -- Postfach (Fallback: smtp_user)
 *   imap_pass  -- Passwort/App-Passwort (Fallback: smtp_pass)
 */
#ifndef FLUX_IMAP_H
#define FLUX_IMAP_H

#include <stddef.h>

/* Holt die Kopfzeilen (Von/Betreff/Datum) der letzten ungelesenen Mails
 * (max. ~8) und schreibt sie als lesbare Liste nach out. */
void flux_imap_fetch_unread(char *out, size_t out_cap);

/* Liest den Textkoerper EINER Mail per UID (markiert sie nicht als gelesen). */
void flux_imap_read(const char *uid, char *out, size_t out_cap);

/* --- intern, fuer Unit-Tests exponiert --------------------------------- */

/* Parst eine IMAP-"* SEARCH a b c"-Antwort in ein Array von IDs.
 * Gibt die Anzahl gefundener IDs zurueck. */
int flux_imap_parse_search(const char *resp, unsigned long *ids, int max_ids);

/* Extrahiert From/Subject/Date aus einer FETCH-Header-Antwort und haengt
 * eine kompakte Zeile an out an (mit vorangestellter UID). */
void flux_imap_extract_headers(unsigned long uid, const char *fetch_resp,
                               char *out, size_t out_cap);

#endif
