/* test_actions.c -- Unit-Tests fuer die lokalen Intents (actions.c).
 *
 * Bewusst ohne Netzwerk/Curl: actions.c haengt nur von libc ab, laeuft
 * also headless in CI. Gebaut via tests/Makefile (linkt actions.c direkt).
 */
#include "../fluxai/src/actions.h"

#include <stdio.h>
#include <string.h>

static int g_ok, g_bad;

#define CHECK(cond) do { \
    if (cond) { g_ok++; } \
    else { g_bad++; printf("  FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

int main(void) {
    char out[512];

    /* Uhrzeit-Intent: erkannt und enthaelt "Uhr". */
    CHECK(flux_actions_try("wie spaet ist es", out, sizeof(out)) == 1);
    CHECK(strstr(out, "Uhr") != NULL);

    /* Datums-Intent: erkannt, enthaelt einen Punkt (TT.MM.JJJJ). */
    CHECK(flux_actions_try("welches datum haben wir", out, sizeof(out)) == 1);
    CHECK(strchr(out, '.') != NULL);

    /* Akku-Intent: immer erkannt; ohne Sensor ehrliche Meldung. */
    CHECK(flux_actions_try("wie ist der akku", out, sizeof(out)) == 1);
    CHECK(strstr(out, "Akku") != NULL || strstr(out, "akku") != NULL);

    /* Englische Schluesselwoerter werden ebenfalls erkannt. */
    CHECK(flux_actions_try("what time is it", out, sizeof(out)) == 1);

    /* Keine lokale Antwort -> 0 (geht sonst an die Cloud). */
    out[0] = '\0';
    CHECK(flux_actions_try("wer hat die relativitaetstheorie entwickelt", out, sizeof(out)) == 0);

    printf("test_actions: %d passed, %d failed\n", g_ok, g_bad);
    return g_bad ? 1 : 0;
}
