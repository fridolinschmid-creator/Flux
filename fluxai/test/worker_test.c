/* worker_test.c -- Funktionstest fuer den Arbeiterthread (Schritt 1+2).
 *
 * Linkt worker.c gegen leichte Stubs (kein curl, kein Netz) und treibt
 * echte Verbindungen durch den Arbeiter. Prueft:
 *   - Q:-Routing trifft zuerst actions (kein Streaming), sonst provider
 *   - der Provider-Pfad streamt P:-Teilstuecke vor dem finalen A:
 *   - X:-Routing geht an exec (mehrzeilig erhalten)
 *   - die finale Antwort ist exakt im Rahmen  A:<text>\nEND\n
 *   - mehrere parallel angenommene Verbindungen werden alle beantwortet
 *
 * Bauen/laufen:  make -C fluxai test
 */
#include "../src/worker.h"
#include "../src/provider.h"   /* flux_provider_ask_stream, flux_delta_cb */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/* ---- Stubs der von worker.c benoetigten Symbole ---------------------- */

int flux_actions_try(const char *q, char *out, size_t cap) {
    if (strstr(q, "uhrzeit")) { snprintf(out, cap, "Es ist 12:00 Uhr."); return 1; }
    return 0; /* sonst Fallback auf provider */
}
/* Streamt die Antwort in zwei Stuecken, damit P:-Frames entstehen. */
void flux_provider_ask_stream(const char *q, flux_delta_cb cb, void *ud,
                              char *out, size_t cap) {
    char full[512];
    snprintf(full, sizeof(full), "PROVIDER:%s", q);
    if (cb) {
        size_t half = strlen(full) / 2;
        char a[256];
        snprintf(a, sizeof(a), "%.*s", (int)half, full);
        cb(a, ud);            /* erstes Teilstueck  -> P: */
        cb(full + half, ud);  /* zweites Teilstueck -> P: */
    }
    snprintf(out, cap, "%s", full);
}
void flux_exec_action(const char *payload, char *out, size_t cap) {
    snprintf(out, cap, "EXEC[%s]", payload);
}
void flux_habits_log(const char *s, const char *t) { (void)s; (void)t; }
void flux_proactive_check(const char *k, const char *m) { (void)k; (void)m; }
void flux_journal_check(const char *k, const char *m) { (void)k; (void)m; }
void flux_habits_morning_briefing(const char *k, const char *m) { (void)k; (void)m; }
int  flux_config_get(const char *key, char *out, size_t cap) {
    (void)key; if (cap) out[0] = '\0'; return 0;
}

/* ---- Hilfe: vollstaendige Antwort einsammeln (bis "\nEND\n") --------- */

static void roundtrip(const char *request, char *resp, size_t cap) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    flux_worker_submit_client(sv[1]);   /* Arbeiter besitzt/schliesst sv[1] */
    assert(write(sv[0], request, strlen(request)) == (ssize_t)strlen(request));
    size_t len = 0;
    for (;;) {
        ssize_t n = read(sv[0], resp + len, cap - 1 - len);
        if (n <= 0) break;
        len += (size_t)n; resp[len] = '\0';
        if (strstr(resp, "\nEND\n")) break;
    }
    close(sv[0]);
}

/* Extrahiert den Text des finalen A:-Frames (nach evtl. P:-Frames). */
static const char *final_answer(const char *raw) {
    const char *a = strstr(raw, "\nA:");
    a = a ? a + 3 : (strncmp(raw, "A:", 2) == 0 ? raw + 2 : NULL);
    return a;
}

int main(void) {
    flux_worker_start();
    char resp[2048];

    /* 1) Q: lokaler Intent (actions) -> kein Streaming, nur A: */
    roundtrip("Q:wie ist die uhrzeit\n", resp, sizeof(resp));
    assert(strcmp(resp, "A:Es ist 12:00 Uhr.\nEND\n") == 0);
    assert(strstr(resp, "P:") == NULL);
    printf("ok  Q: lokaler Intent (kein Stream)\n");

    /* 2) Q: provider -> P:-Frames gefolgt von finalem A: */
    roundtrip("Q:erzaehl mir was\n", resp, sizeof(resp));
    assert(strstr(resp, "P:") != NULL);                 /* gestreamt */
    const char *fa = final_answer(resp);
    assert(fa && strncmp(fa, "PROVIDER:erzaehl mir was\nEND\n", 28) == 0);
    /* P:-Stuecke zusammengesetzt ergeben die volle Antwort */
    {
        char joined[512] = {0}; size_t jl = 0;
        const char *p = resp;
        while ((p = strstr(p, "P:")) != NULL) {
            p += 2; const char *nl = strchr(p, '\n'); if (!nl) break;
            size_t l = (size_t)(nl - p);
            memcpy(joined + jl, p, l); jl += l; joined[jl] = '\0';
            p = nl + 1;
            if (strncmp(p, "A:", 2) == 0) break; /* finalen Frame nicht mitnehmen */
        }
        assert(strcmp(joined, "PROVIDER:erzaehl mir was") == 0);
    }
    printf("ok  Q: provider streamt P: -> A:\n");

    /* 3) X: mehrzeilig -> exec, Struktur erhalten */
    roundtrip("X:mail\nTO:a@b.de\nSUBJECT:Hi\nBODY:\nText", resp, sizeof(resp));
    assert(strstr(resp, "TO:a@b.de") && strstr(resp, "BODY:\nText"));
    assert(strncmp(resp, "A:EXEC[mail\n", 12) == 0 && strstr(resp, "\nEND\n"));
    printf("ok  X: mehrzeilig exec\n");

    /* 4) Unbekanntes Protokoll -> ERR */
    roundtrip("FOO:bar\n", resp, sizeof(resp));
    assert(strncmp(resp, "ERR:", 4) == 0 && strstr(resp, "\nEND\n"));
    printf("ok  unbekanntes Protokoll -> ERR\n");

    /* 5) Mehrere Verbindungen gleichzeitig: submit blockiert nicht, alle
     *    werden beantwortet (FIFO durch den einen Arbeiter). */
    enum { N = 8 };
    int sv[N][2];
    for (int i = 0; i < N; i++) {
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv[i]) == 0);
        flux_worker_submit_client(sv[i][1]);
    }
    for (int i = 0; i < N; i++) {
        char req[64]; snprintf(req, sizeof(req), "Q:frage %d\n", i);
        assert(write(sv[i][0], req, strlen(req)) > 0);
    }
    for (int i = 0; i < N; i++) {
        char r[512]; size_t len = 0;
        for (;;) {
            ssize_t n = read(sv[i][0], r + len, sizeof(r) - 1 - len);
            if (n <= 0) break;
            len += (size_t)n; r[len] = '\0';
            if (strstr(r, "\nEND\n")) break;
        }
        char want[128]; snprintf(want, sizeof(want), "PROVIDER:frage %d\nEND\n", i);
        const char *afa = final_answer(r);
        assert(afa && strcmp(afa, want) == 0);
        close(sv[i][0]);
    }
    printf("ok  %d parallele Verbindungen alle beantwortet\n", N);

    flux_worker_submit_proactive();
    usleep(50 * 1000);
    printf("\nALLE TESTS BESTANDEN\n");
    return 0;
}
