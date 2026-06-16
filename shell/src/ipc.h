/* ipc.h -- Client-Seite: Shell fragt den System-KI-Daemon (fluxaid). */
#ifndef FLUX_IPC_H
#define FLUX_IPC_H

#include <stddef.h>

/* Stellt eine Frage, blockiert bis die Antwort da ist (oder Timeout/
 * Fehler). out enthaelt danach immer einen lesbaren Text -- auch im
 * Fehlerfall, damit die UI nie eine leere Sprechblase zeigt. */
void flux_ipc_ask(const char *question, char *out, size_t out_cap);

/* Legt einen Kontakt an. out enthaelt immer eine lesbare Antwort. */
void flux_ipc_add_contact(const char *name, const char *phone, char *out, size_t out_cap);

/* Sucht einen Kontakt per (Teil-)Name. Gibt 1 bei Treffer (out = "name\ttelefon"),
 * 0 sonst (out = lesbare Fehlermeldung). */
int flux_ipc_find_contact(const char *name, char *out, size_t out_cap);

/* Verschickt eine E-Mail. out enthaelt immer eine lesbare Antwort. */
void flux_ipc_send_email(const char *to, const char *subject, const char *body,
                          char *out, size_t out_cap);

#endif
