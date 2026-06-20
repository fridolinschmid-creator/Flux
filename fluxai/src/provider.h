/* provider.h -- Cloud-Fallback fuer Fragen, die actions.c nicht lokal
 * beantworten kann. Nutzt die Anthropic Messages API, wenn der Nutzer
 * einen eigenen API-Key per Umgebungsvariable FLUX_AI_API_KEY setzt.
 * Ohne Key (z.B. im Flugmodus oder beim Erststart) liefert
 * flux_provider_ask() eine ehrliche Offline-Antwort statt eines
 * Absturzes oder einer erfundenen Antwort.
 */
#ifndef FLUX_PROVIDER_H
#define FLUX_PROVIDER_H

#include <stddef.h>
#include "sse.h"   /* flux_delta_cb */

void flux_provider_init(void);

/* Nicht-streamende Variante (unveraendert): blockiert bis zur vollen
 * Antwort. Wird von proaktiven Pruefungen genutzt. */
void flux_provider_ask(const char *question, char *out, size_t out_cap);

/* Streamende Variante: ruft on_delta fuer jedes sichtbare Textstueck auf
 * (fuer progressive P:-Frames) und legt am Ende die volle Antwort in out
 * ab. Strukturierte Direktiven (TOOL:/ACTION:) werden NICHT an on_delta
 * gemeldet, stehen aber vollstaendig in out. on_delta darf NULL sein --
 * dann verhaelt sich der Aufruf wie flux_provider_ask(). */
void flux_provider_ask_stream(const char *question,
                              flux_delta_cb on_delta, void *ud,
                              char *out, size_t out_cap);

#endif
