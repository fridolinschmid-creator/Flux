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

/* Wie flux_provider_ask(), aber mit Hybrid-Router: ist der Config-Key
 * `ai_router` == "on", entscheidet eine simple Heuristik pro Anfrage, ob sie
 * lokal (llama.cpp) oder beim konfigurierten Cloud-Anbieter beantwortet wird.
 * Ist der Router aus (Default), verhaelt sich der Aufruf exakt wie
 * flux_provider_ask() (fest gewaehlter Anbieter). Gedacht fuer den
 * interaktiven Q:-Pfad; Hintergrunddienste nutzen weiter flux_provider_ask(). */
void flux_provider_route(const char *question, char *out, size_t out_cap);

/* 1, wenn der aktuell gewaehlte Anbieter einen nutzbaren API-Key hat. */
int flux_provider_available(void);

/* Ermittelt API-Key und Modell des aktiven Anbieters. Gibt 1 zurueck,
 * wenn ein Key vorliegt. key_out/model_out duerfen NULL sein. */
int flux_provider_active(char *key_out, size_t key_cap,
                         char *model_out, size_t model_cap);

#endif
