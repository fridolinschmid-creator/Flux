/* ipc.h -- Client-Seite: Shell fragt den System-KI-Daemon (fluxaid). */
#ifndef FLUX_IPC_H
#define FLUX_IPC_H

#include <stddef.h>

/* Stellt eine Frage, blockiert bis die Antwort da ist (oder Timeout/
 * Fehler). out enthaelt danach immer einen lesbaren Text -- auch im
 * Fehlerfall, damit die UI nie eine leere Sprechblase zeigt. */
void flux_ipc_ask(const char *question, char *out, size_t out_cap);

#endif
