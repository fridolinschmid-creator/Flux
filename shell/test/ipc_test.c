/* ipc_test.c -- Test der Client-Seite (src/ipc.c, Schritt 2).
 *
 * Startet einen Mini-Server am echten FLUX_SOCK_PATH, der ein vorgegebenes
 * Antwort-Skript sendet, und prueft den Stream-Konsumenten:
 *   - P:-Frames werden progressiv (kumulativ) an den Callback gemeldet
 *   - escapte Zeilenumbrueche (\\n) werden entschluesselt
 *   - die finale Antwort steht in out (A: ist massgeblich, ersetzt P:)
 *   - rueckwaertskompatibel: nur A:.../END (in mehreren Writes) funktioniert
 *   - ERR: wird als finaler Text geliefert
 *
 * Bauen/laufen:  make -C shell test
 */
#include "../src/ipc.h"
#include "../../common/flux_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

/* ---- Mini-Server, der ein Skript in Teil-Writes sendet --------------- */

typedef struct {
    const char *chunks[8]; /* NULL-terminiert; je ein write() mit kurzer Pause */
} script_t;

static void *server_thread(void *arg) {
    script_t *sc = arg;
    unlink(FLUX_SOCK_PATH);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = {0}; a.sun_family = AF_UNIX;
    strncpy(a.sun_path, FLUX_SOCK_PATH, sizeof(a.sun_path) - 1);
    assert(bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0);
    assert(listen(fd, 1) == 0);
    int c = accept(fd, NULL, NULL);
    char req[256]; (void)!read(c, req, sizeof(req)); /* Anfrage verwerfen */
    for (int i = 0; sc->chunks[i]; i++) {
        (void)!write(c, sc->chunks[i], strlen(sc->chunks[i]));
        usleep(5 * 1000); /* getrennte Frames erzwingen */
    }
    close(c); close(fd);
    return NULL;
}

/* ---- Callback sammelt die Zwischenstaende ---------------------------- */

static char  g_last[1024];   /* letzter gemeldeter Live-Stand */
static int   g_calls;
static char  g_first[1024];  /* erster gemeldeter Live-Stand   */
static void on_partial(const char *live, void *ud) {
    (void)ud;
    if (g_calls == 0) snprintf(g_first, sizeof(g_first), "%s", live);
    snprintf(g_last, sizeof(g_last), "%s", live);
    g_calls++;
}

static void reset(void) { g_last[0] = g_first[0] = '\0'; g_calls = 0; }

static void run(script_t *sc, char *out, size_t cap) {
    reset();
    pthread_t t;
    pthread_create(&t, NULL, server_thread, sc);
    usleep(20 * 1000); /* Server kommt zum bind/listen */
    flux_ipc_ask_stream("frage", on_partial, NULL, out, cap);
    pthread_join(t, NULL);
}

int main(void) {
    char out[1024];

    /* 1) Streaming: zwei P:-Deltas, dann finales A: */
    { script_t sc = { { "P:Hal\n", "P:lo Welt\n", "A:Hallo Welt\nEND\n", NULL } };
      run(&sc, out, sizeof(out));
      assert(strcmp(out, "Hallo Welt") == 0);
      assert(g_calls == 2);
      assert(strcmp(g_first, "Hal") == 0);          /* kumulativ */
      assert(strcmp(g_last,  "Hallo Welt") == 0);
      printf("ok  Streaming: P: progressiv -> finales A:\n"); }

    /* 2) Escapter Zeilenumbruch im P:-Frame */
    { script_t sc = { { "P:Zeile1\\nZeile2\n", "A:Zeile1\\nZeile2\nEND\n", NULL } };
      run(&sc, out, sizeof(out));
      assert(strcmp(g_last, "Zeile1\nZeile2") == 0); /* P: entschluesselt */
      printf("ok  P: escaptes \\n entschluesselt\n"); }

    /* 3) Rueckwaertskompatibel: nur A:.../END, in zwei Writes zerteilt,
     *    Antworttext enthaelt selbst Zeilenumbrueche */
    { script_t sc = { { "A:erste Zeile\nzwei", "te Zeile\nEND\n", NULL } };
      run(&sc, out, sizeof(out));
      assert(strcmp(out, "erste Zeile\nzweite Zeile") == 0);
      assert(g_calls == 0); /* keine P:-Frames -> keine Teilmeldungen */
      printf("ok  rueckwaertskompatibel (nur A:, mehrteilig)\n"); }

    /* 4) ERR: wird als finaler Text geliefert */
    { script_t sc = { { "ERR:kaputt\nEND\n", NULL } };
      run(&sc, out, sizeof(out));
      assert(strcmp(out, "kaputt") == 0);
      printf("ok  ERR: als finaler Text\n"); }

    printf("\nALLE TESTS BESTANDEN\n");
    return 0;
}
