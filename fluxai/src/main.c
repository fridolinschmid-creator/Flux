/* fluxaid -- System-KI-Daemon.
 * Laeuft als eigener Prozess (von init gestartet, siehe init/rcS),
 * nicht als App. Jede Komponente von Flux (Shell, spaeter
 * Benachrichtigungen, Einstellungen, ...) kann ueber denselben
 * Unix-Socket dieselbe Assistenz-Logik nutzen.
 */
#include "actions.h"
#include "provider.h"
#include "../../common/flux_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
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

static void handle_client(int cfd) {
    char line[FLUX_MAX_LINE];
    ssize_t n = read(cfd, line, sizeof(line) - 1);
    if (n <= 0) { close(cfd); return; }
    line[n] = '\0';
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';

    if (strncmp(line, "Q:", 2) != 0) {
        const char *msg = "ERR:Unbekanntes Protokoll, erwarte 'Q:<frage>'\nEND\n";
        if (write(cfd, msg, strlen(msg)) < 0) { /* Client schon weg, egal */ }
        close(cfd);
        return;
    }
    const char *question = line + 2;

    char answer[8192];
    if (!flux_actions_try(question, answer, sizeof(answer)))
        flux_provider_ask(question, answer, sizeof(answer));

    char resp[8300];
    snprintf(resp, sizeof(resp), "A:%s\nEND\n", answer);
    if (write(cfd, resp, strlen(resp)) < 0) { /* Client schon weg, egal */ }
    close(cfd);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN); /* Client kann jederzeit weg sein (Lockscreen-Wechsel) */
    flux_provider_init();

    mkdir("/run/flux", 0755);
    int listen_fd = make_listen_socket(FLUX_SOCK_PATH);

    fprintf(stderr, "fluxaid: lauscht auf %s\n", FLUX_SOCK_PATH);

    for (;;) {
        int cfd = accept(listen_fd, NULL, NULL);
        if (cfd < 0) continue;
        handle_client(cfd); /* ein Request pro Verbindung reicht fuer den Prototyp */
    }
}
