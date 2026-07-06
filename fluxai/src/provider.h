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

/* 1, wenn der deterministische Mock-Provider aktiv ist (FLUX_PROVIDER=mock).
 * Der Mock antwortet ohne Netzwerk/API-Key und erlaubt es Tests, den
 * kompletten Tool-Dispatch reproduzierbar zu pruefen. */
int flux_provider_is_mock(void);

/* Beantwortet eine Nutzerfrage und pflegt den Gespraechsverlauf
 * (letzte Runden werden als Kontext mitgesendet und gespeichert). */
void flux_provider_ask(const char *question, char *out, size_t out_cap);

/* 1, wenn der aktuell gewaehlte Anbieter einen nutzbaren API-Key hat. */
int flux_provider_available(void);

/* Ermittelt API-Key und Modell des aktiven Anbieters. Gibt 1 zurueck,
 * wenn ein Key vorliegt. key_out/model_out duerfen NULL sein. */
int flux_provider_active(char *key_out, size_t key_cap,
                         char *model_out, size_t model_cap);

/* Wie flux_provider_ask, aber OHNE den Gespraechsverlauf zu lesen oder zu
 * veraendern. Fuer Hintergrund-Aufgaben (Proactive/Journal/Habits), deren
 * grosse interne Prompts sonst den Verlauf des Nutzers verschmutzen. */
void flux_provider_ask_ephemeral(const char *question, char *out, size_t out_cap);

#endif
