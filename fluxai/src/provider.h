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

void flux_provider_init(void);
void flux_provider_ask(const char *question, char *out, size_t out_cap);

/* 1, wenn der aktuell gewaehlte Anbieter einen nutzbaren API-Key hat. */
int flux_provider_available(void);

/* Ermittelt API-Key und Modell des aktiven Anbieters. Gibt 1 zurueck,
 * wenn ein Key vorliegt. key_out/model_out duerfen NULL sein. */
int flux_provider_active(char *key_out, size_t key_cap,
                         char *model_out, size_t model_cap);

/* Vision/Bildanalyse benoetigt einen Vision-faehigen Anbieter. Aktuell
 * unterstuetzt nur Anthropic (FMT_ANTHROPIC) das von vision.c erzeugte
 * Image-Content-Block-Format. Gibt 1 zurueck, wenn der AKTIVE Anbieter
 * Anthropic ist UND ein nutzbarer API-Key vorliegt; fuellt dann key_out,
 * model_out und label_out. Andernfalls 0 -- label_out wird mit dem Namen
 * des aktiven Anbieters gefuellt, damit der Aufrufer eine klare
 * Fehlermeldung erzeugen kann. Alle *_out duerfen NULL sein. */
int flux_provider_vision(char *key_out, size_t key_cap,
                         char *model_out, size_t model_cap,
                         char *label_out, size_t label_cap);

#endif
