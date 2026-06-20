/* fluxaid -- System-KI-Daemon.
 * Laeuft als eigener Prozess (von init gestartet, siehe init/rcS),
 * nicht als App. Jede Komponente von Flux (Shell, spaeter
 * Benachrichtigungen, Einstellungen, ...) kann ueber denselben
 * Unix-Socket dieselbe Assistenz-Logik nutzen.
 */
#include "actions.h"
#include "provider.h"
#include "proactive.h"
#include "journal.h"
#include "habits.h"
#include "exec.h"
#include "../../common/flux_protocol.h"
#include "../../common/flux_config.h"

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

static void handle_client(int cfd) {
    char line[FLUX_MAX_LINE];
    ssize_t n = read(cfd, line, sizeof(line) - 1);
    if (n <= 0) { close(cfd); return; }
    line[n] = '\0';

    char answer[FLUX_MAX_LINE];
    if (strncmp(line, "Q:", 2) == 0) {
        /* Eine Frage ist eine einzelne Zeile -- am ersten Newline
         * abschneiden, falls noch einer mitgesendet wurde. */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        const char *question = line + 2;
        if (!flux_actions_try(question, answer, sizeof(answer)))
            flux_provider_ask(question, answer, sizeof(answer));
        /* Nutzungsgewohnheiten loggen (ersten 80 Zeichen der Frage) */
        char topic[84];
        snprintf(topic, sizeof(topic), "%.80s", question);
        flux_habits_log("assistant", topic);
    } else if (strncmp(line, "X:", 2) == 0) {
        /* Eine bestaetigte Aktion ist mehrzeilig (TO:/SUBJECT:/BODY:)
         * -- NICHT am ersten Newline abschneiden. */
        flux_exec_action(line + 2, answer, sizeof(answer));
    } else {
        const char *msg = "ERR:Unbekanntes Protokoll, erwarte 'Q:<frage>' oder 'X:<aktion>'\nEND\n";
        if (write(cfd, msg, strlen(msg)) < 0) { /* Client schon weg, egal */ }
        close(cfd);
        return;
    }

    char resp[FLUX_MAX_RESPONSE];
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

    /* Proactive check on startup */
    {
        char k[512] = {0}, m[200] = {0};
        if (flux_provider_active(k, sizeof(k), m, sizeof(m)))
            flux_proactive_check(k, m);
    }

    for (;;) {
        /* Use select() with 5-minute timeout for proactive checks */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        struct timeval tv = { .tv_sec = 300, .tv_usec = 0 };
        int ret = select(listen_fd + 1, &rfds, NULL, NULL, &tv);

        if (ret == 0) {
            /* Timeout: run periodic checks */
            char k[512] = {0}, m[200] = {0};
            if (flux_provider_active(k, sizeof(k), m, sizeof(m))) {
                flux_proactive_check(k, m);
                flux_journal_check(k, m);
                flux_habits_morning_briefing(k, m);
            }
            continue;
        }
        if (ret < 0) continue;

        int cfd = accept(listen_fd, NULL, NULL);
        if (cfd < 0) continue;
        handle_client(cfd); /* ein Request pro Verbindung reicht fuer den Prototyp */
    }
}
