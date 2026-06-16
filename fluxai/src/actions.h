/* actions.h -- Geraete-Intents, die der Daemon SOFORT und LOKAL
 * beantwortet, ohne die Cloud zu fragen (Geschwindigkeit + Privacy:
 * "wie spaet ist es" verlaesst das Geraet nie).
 *
 * Gibt 1 zurueck und schreibt die Antwort nach out, wenn die Frage
 * von einem lokalen Intent erkannt wurde -- sonst 0 (-> provider.c
 * wird gefragt).
 */
#ifndef FLUX_ACTIONS_H
#define FLUX_ACTIONS_H

#include <stddef.h>

int flux_actions_try(const char *question, char *out, size_t out_cap);

#endif
