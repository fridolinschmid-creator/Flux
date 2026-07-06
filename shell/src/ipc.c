#include "ipc.h"
#include "../../common/flux_protocol.h"
#include "../../common/flux_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

void flux_ipc_send_raw(const char *request, char *out, size_t out_cap) {
    /* Socket-Pfad per FLUX_SOCK_PATH ueberschreibbar (muss zum Daemon
     * passen), sonst der Standard aus flux_protocol.h. */
    const char *sock = getenv("FLUX_SOCK_PATH");
    if (!sock || !*sock) sock = FLUX_SOCK_PATH;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(out, out_cap, "fluxaid nicht erreichbar (socket).");
        return;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock, sizeof(addr.sun_path) - 1);

    /* 5-Sekunden Timeout: Shell friert nicht ein wenn fluxaid haengt */
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        snprintf(out, out_cap, "fluxaid laeuft nicht (kein Socket unter %s).", sock);
        LOGE("ipc: connect(%s) fehlgeschlagen: %s", sock, strerror(errno));
        close(fd);
        return;
    }

    if (write(fd, request, strlen(request)) < 0) {
        snprintf(out, out_cap, "Fehler beim Senden an fluxaid.");
        LOGE("ipc: write() an fluxaid fehlgeschlagen: %s", strerror(errno));
        close(fd);
        return;
    }

    /* Schleife bis vollstaendige Antwort (endet mit "\nEND\n") vorliegt.
     * Ein einzelnes read() reicht bei langen KI-Antworten nicht. */
    char resp[FLUX_MAX_RESPONSE];
    size_t total = 0;
    ssize_t n;
    while (total < sizeof(resp) - 1) {
        n = read(fd, resp + total, sizeof(resp) - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
        resp[total] = '\0';
        if (strstr(resp, "\nEND\n")) break;
    }
    close(fd);
    if (total == 0) {
        snprintf(out, out_cap, "Keine Antwort von fluxaid erhalten.");
        LOGE("ipc: keine Antwort von fluxaid (read=%zd): %s", n, strerror(errno));
        return;
    }
    resp[total] = '\0';

    const char *body = resp;
    if (strncmp(body, "A:", 2) == 0 || strncmp(body, "ERR:", 4) == 0)
        body += (body[1] == ':') ? 2 : 4;

    char *end = strstr(body, "\nEND");
    if (end) *end = '\0';

    snprintf(out, out_cap, "%s", body);
}

void flux_ipc_ask(const char *question, char *out, size_t out_cap) {
    char req[FLUX_MAX_LINE];
    snprintf(req, sizeof(req), "Q:%s\n", question);
    flux_ipc_send_raw(req, out, out_cap);
}
