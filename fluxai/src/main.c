/* fluxaid -- System-KI-Daemon.
 * Laeuft als eigener Prozess (von init gestartet, siehe init/rcS),
 * nicht als App. Jede Komponente von Flux (Shell, spaeter
 * Benachrichtigungen, Einstellungen, ...) kann ueber denselben
 * Unix-Socket dieselbe Assistenz-Logik nutzen.
 */
#include "provider.h"
#include "worker.h"
#include "../../common/flux_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <signal.h>

static int make_listen_socket(const char *path) {
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    chmod(path, 0666); /* jede App darf den Assistenten fragen */
    if (listen(fd, 8) < 0) { perror("listen"); exit(1); }
    return fd;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN); /* Client kann jederzeit weg sein (Lockscreen-Wechsel) */
    flux_provider_init();

    mkdir("/run/flux", 0755);
    int listen_fd = make_listen_socket(FLUX_SOCK_PATH);

    fprintf(stderr, "fluxaid: lauscht auf %s\n", FLUX_SOCK_PATH);

    /* Der Arbeiterthread bearbeitet alle KI-Aufrufe (Schritt 1, siehe
     * worker.h / docs/HYBRID_AI_ARCHITECTURE.md). Die Hauptschleife darf
     * dadurch nie mehr auf einem bis zu 20s langen curl-Aufruf blockieren --
     * accept() und der proaktive Timer bleiben jederzeit reaktionsfaehig. */
    flux_worker_start();

    /* Proaktive Pruefung beim Start -- ueber den Arbeiter, damit der gesamte
     * Provider-Zustand (Gespraechskontext) auf einem Thread bleibt. */
    flux_worker_submit_proactive();

    for (;;) {
        /* select() mit 5-Minuten-Timeout fuer die proaktiven Pruefungen. */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        struct timeval tv = { .tv_sec = 300, .tv_usec = 0 };
        int ret = select(listen_fd + 1, &rfds, NULL, NULL, &tv);

        if (ret == 0) {
            /* Timeout: periodische Pruefungen an den Arbeiter uebergeben. */
            flux_worker_submit_proactive();
            continue;
        }
        if (ret < 0) continue;

        int cfd = accept(listen_fd, NULL, NULL);
        if (cfd < 0) continue;
        flux_worker_submit_client(cfd); /* nicht-blockierend: der Arbeiter antwortet */
    }
}
