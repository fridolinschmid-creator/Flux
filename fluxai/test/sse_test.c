/* sse_test.c -- Unit-Test fuer den SSE-Parser (src/sse.c).
 *
 * Speist aufgezeichnete Anthropic-SSE-Stroeme ein (auch byteweise, um die
 * Zerstueckelung zu pruefen) und kontrolliert:
 *   - text_delta-Stuecke werden korrekt entschluesselt und zusammengesetzt
 *   - der Callback meldet den sichtbaren Text (= voller Text bei Normalfall)
 *   - TOOL:/ACTION:-Direktiven werden NICHT gemeldet, stehen aber im Volltext
 *   - sehr kurze Antworten (< Praefixlaenge) werden am Ende noch gemeldet
 *
 * Bauen/laufen:  make -C fluxai test
 */
#include "../src/sse.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *const SUPPRESS[] = { "TOOL:", "ACTION:", NULL };

static char g_cb[4096];
static size_t g_cblen;
static void collect(const char *t, void *ud) {
    (void)ud;
    size_t l = strlen(t);
    if (g_cblen + l < sizeof(g_cb)) { memcpy(g_cb + g_cblen, t, l); g_cblen += l; g_cb[g_cblen] = '\0'; }
}

/* Parst input; chunk=0 -> alles auf einmal, sonst in chunk-Byte-Stuecken. */
static void run(const char *input, size_t chunk, char *full, size_t full_cap) {
    g_cblen = 0; g_cb[0] = '\0';
    flux_sse_t s;
    flux_sse_init(&s, full, full_cap, collect, NULL, SUPPRESS);
    size_t n = strlen(input);
    if (chunk == 0) {
        flux_sse_feed(&s, input, n);
    } else {
        for (size_t i = 0; i < n; i += chunk)
            flux_sse_feed(&s, input + i, (i + chunk <= n) ? chunk : (n - i));
    }
    flux_sse_finish(&s);
}

#define D(txt) \
    "event: content_block_delta\n" \
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"" txt "\"}}\n\n"

int main(void) {
    char full[4096];

    /* 1) Normalfall: zwei Deltas, ganzer Block auf einmal */
    run(D("Hallo ") D("Welt") "event: message_stop\ndata: {}\n\n", 0, full, sizeof(full));
    assert(strcmp(full, "Hallo Welt") == 0);
    assert(strcmp(g_cb, "Hallo Welt") == 0);
    printf("ok  Normalfall (ganzer Block)\n");

    /* 2) Gleicher Strom byteweise gespeist -> identisches Ergebnis */
    run(D("Hallo ") D("Welt"), 1, full, sizeof(full));
    assert(strcmp(full, "Hallo Welt") == 0);
    assert(strcmp(g_cb, "Hallo Welt") == 0);
    printf("ok  byteweise Zerstueckelung robust\n");

    /* 3) JSON-Escapes im Delta (\n, \") werden entschluesselt */
    run(D("Zeile1\\nZeile2 \\\"x\\\""), 0, full, sizeof(full));
    assert(strcmp(full, "Zeile1\nZeile2 \"x\"") == 0);
    printf("ok  JSON-Escapes entschluesselt\n");

    /* 4) TOOL:-Direktive -> Volltext ja, Callback nichts */
    run(D("TOOL:weather") D("\\nARG:Berlin"), 0, full, sizeof(full));
    assert(strcmp(full, "TOOL:weather\nARG:Berlin") == 0);
    assert(g_cb[0] == '\0');
    printf("ok  TOOL: unterdrueckt (nur im Volltext)\n");

    /* 5) ACTION:-Direktive -> ebenso unterdrueckt */
    run(D("ACTION:mail") D("\\nTO:a@b.de"), 0, full, sizeof(full));
    assert(strcmp(full, "ACTION:mail\nTO:a@b.de") == 0);
    assert(g_cb[0] == '\0');
    printf("ok  ACTION: unterdrueckt\n");

    /* 6) Sehr kurze Antwort (< Praefixlaenge) wird am Ende noch gemeldet */
    run(D("Ja"), 0, full, sizeof(full));
    assert(strcmp(full, "Ja") == 0);
    assert(strcmp(g_cb, "Ja") == 0);
    printf("ok  kurze Antwort am Stromende gemeldet\n");

    /* 7) Wort, das mit 'ACTION' (ohne Doppelpunkt) beginnt, NICHT unterdrueckt */
    run(D("ACTIONFILME sind toll"), 0, full, sizeof(full));
    assert(strcmp(g_cb, "ACTIONFILME sind toll") == 0);
    printf("ok  'ACTION' ohne ':' wird normal gemeldet\n");

    printf("\nALLE TESTS BESTANDEN\n");
    return 0;
}
