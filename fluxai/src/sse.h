/* sse.h -- inkrementeller Parser fuer den Server-Sent-Events-Strom der
 * Anthropic Messages API (stream:true). Zieht "text_delta"-Stuecke heraus,
 * haengt sie an den vollstaendigen Antworttext an UND meldet jedes neue
 * Stueck ueber einen Callback (fuer progressives Rendern, P:-Frames).
 *
 * Bewusst eigenstaendig und ohne curl/Netz-Abhaengigkeit, damit der Parser
 * mit einem aufgezeichneten Byte-Strom unit-getestet werden kann
 * (fluxai/test/sse_test.c).
 *
 * Praefix-Schutz: solange noch nicht entschieden ist, ob die Antwort ein
 * strukturiertes Direktiv ist (z.B. "TOOL:" oder "ACTION:"), werden die
 * Stuecke zurueckgehalten. Beginnt die Antwort mit einem der Unterdrueck-
 * Praefixe, wird per Callback NICHTS gemeldet (der Nutzer soll die rohe
 * Direktive nie sehen) -- der volle Text enthaelt sie aber weiterhin,
 * damit provider.c den Tool-/Aktions-Aufruf erkennen kann.
 */
#ifndef FLUX_SSE_H
#define FLUX_SSE_H

#include <stddef.h>

/* Wird fuer jedes neue, sichtbare Textstueck aufgerufen. text ist
 * nullterminiert und nur waehrend des Aufrufs gueltig. */
typedef void (*flux_delta_cb)(const char *text, void *ud);

typedef struct {
    char  *full;          /* Zielpuffer fuer den vollstaendigen Antworttext */
    size_t full_cap;
    size_t full_len;
    size_t emitted_len;   /* bis hierhin schon per Callback gemeldet */

    char   line[8192];    /* Puffer fuer die gerade einlaufende SSE-Zeile */
    size_t line_len;

    flux_delta_cb cb;
    void  *ud;

    const char *const *suppress; /* NULL-terminierte Liste von Praefixen */
    int    decided;       /* Praefix-Entscheidung gefallen? */
    int    suppressed;    /* Antwort ist eine zu unterdrueckende Direktive */
    size_t decide_at;     /* ab so vielen Bytes wird entschieden */
} flux_sse_t;

/* full/full_cap: Zielpuffer fuer den Gesamttext (wird nullterminiert).
 * cb/ud:        Callback fuer sichtbare Stuecke (darf NULL sein).
 * suppress:     NULL-terminierte Praefixliste, bei deren Match nichts
 *               gemeldet wird (darf NULL sein). */
void flux_sse_init(flux_sse_t *s, char *full, size_t full_cap,
                   flux_delta_cb cb, void *ud,
                   const char *const *suppress);

/* Naechsten Roh-Byte-Block aus dem Strom einspeisen (beliebig zerstueckelt). */
void flux_sse_feed(flux_sse_t *s, const char *bytes, size_t n);

/* Strom-Ende: ggf. zurueckgehaltenes kurzes Stueck noch melden. */
void flux_sse_finish(flux_sse_t *s);

#endif
