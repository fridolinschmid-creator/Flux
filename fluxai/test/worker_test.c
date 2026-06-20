/* worker_test.c -- Funktionstest fuer den Arbeiterthread (Schritt 1).
 *
 * Linkt worker.c gegen leichte Stubs der Abhaengigkeiten (kein curl, kein
 * Netz) und treibt echte Verbindungen durch den Arbeiter, um zu pruefen:
 *   - Q:-Routing trifft zuerst actions, sonst provider
 *   - X:-Routing geht an exec (mehrzeilig erhalten)
 *   - die Antwort ist exakt im Rahmen  A:<text>\nEND\n
 *   - mehrere parallel angenommene Verbindungen werden alle beantwortet
 *   - die Hauptschleife blockiert nicht (submit kehrt sofort zurueck)
 *
 * Bauen/laufen:  make -C fluxai test
 */
#include "../src/worker.h"

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
void flux_provider_ask(const char *q, char *out, size_t cap) {
    snprintf(out, cap, "PROVIDER:%s", q);
}
void flux_exec_action(const char *payload, char *out, size_t cap) {
    /* payload muss die mehrzeilige Struktur unveraendert enthalten */
    snprintf(out, cap, "EXEC[%s]", payload);
}
void flux_habits_log(const char *s, const char *t) { (void)s; (void)t; }
void flux_proactive_check(const char *k, const char *m) { (void)k; (void)m; }
void flux_journal_check(const char *k, const char *m) { (void)k; (void)m; }
void flux_habits_morning_briefing(const char *k, const char *m) { (void)k; (void)m; }
int  flux_config_get(const char *key, char *out, size_t cap) {
    (void)key; if (cap) out[0] = '\0'; return 0;
}

/* ---- Hilfe: einen Request ueber den Arbeiter ausfuehren -------------- */

static void roundtrip(const char *request, char *resp, size_t cap) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    /* sv[1] geht an den Arbeiter (er besitzt und schliesst es),
     * sv[0] bleibt der Testseite zum Senden/Empfangen. */
    flux_worker_submit_client(sv[1]);
    assert(write(sv[0], request, strlen(request)) == (ssize_t)strlen(request));
    ssize_t n = read(sv[0], resp, cap - 1);
    assert(n > 0);
    resp[n] = '\0';
    close(sv[0]);
}

int main(void) {
    flux_worker_start();

    char resp[1024];

    /* 1) Q: trifft lokalen Intent (actions) -> kein provider */
    roundtrip("Q:wie ist die uhrzeit\n", resp, sizeof(resp));
    assert(strcmp(resp, "A:Es ist 12:00 Uhr.\nEND\n") == 0);
    printf("ok  Q: lokaler Intent  -> %s", resp);

    /* 2) Q: ohne lokalen Intent -> provider-Fallback */
    roundtrip("Q:erzaehl mir was\n", resp, sizeof(resp));
    assert(strcmp(resp, "A:PROVIDER:erzaehl mir was\nEND\n") == 0);
    printf("ok  Q: provider        -> %s", resp);

    /* 3) X: mehrzeilig -> exec, Struktur bleibt erhalten (NICHT am 1. \n
     *    abgeschnitten) */
    roundtrip("X:mail\nTO:a@b.de\nSUBJECT:Hi\nBODY:\nText", resp, sizeof(resp));
    assert(strstr(resp, "TO:a@b.de") && strstr(resp, "BODY:\nText"));
    assert(strncmp(resp, "A:EXEC[mail\n", 12) == 0);
    assert(strstr(resp, "\nEND\n"));
    printf("ok  X: mehrzeilig exec\n");

    /* 4) Unbekanntes Protokoll -> ERR */
    roundtrip("FOO:bar\n", resp, sizeof(resp));
    assert(strncmp(resp, "ERR:", 4) == 0 && strstr(resp, "\nEND\n"));
    printf("ok  unbekanntes Protokoll -> ERR\n");

    /* 5) Mehrere Verbindungen gleichzeitig anstossen, dann alle einsammeln:
     *    beweist, dass submit nicht blockiert und der Arbeiter FIFO leert. */
    enum { N = 8 };
    int sv[N][2];
    for (int i = 0; i < N; i++) {
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv[i]) == 0);
        flux_worker_submit_client(sv[i][1]);   /* kehrt sofort zurueck */
    }
    for (int i = 0; i < N; i++) {
        char req[64]; snprintf(req, sizeof(req), "Q:frage %d\n", i);
        assert(write(sv[i][0], req, strlen(req)) > 0);
    }
    for (int i = 0; i < N; i++) {
        char r[256]; ssize_t n = read(sv[i][0], r, sizeof(r) - 1);
        assert(n > 0); r[n] = '\0';
        char want[256]; snprintf(want, sizeof(want), "A:PROVIDER:frage %d\nEND\n", i);
        assert(strcmp(r, want) == 0);
        close(sv[i][0]);
    }
    printf("ok  %d parallele Verbindungen alle beantwortet\n", N);

    /* 6) Proaktiver Job darf eingereiht werden ohne zu blockieren */
    flux_worker_submit_proactive();
    printf("ok  proaktiver Job eingereiht (nicht blockierend)\n");

    /* dem Arbeiter kurz Zeit geben, den proaktiven Job zu ziehen */
    usleep(50 * 1000);

    printf("\nALLE TESTS BESTANDEN\n");
    return 0;
}
