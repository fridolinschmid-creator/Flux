/* protocol_client.c -- minimaler Testclient fuer fluxaid.
 *
 * Verbindet sich mit dem Unix-Socket (FLUX_SOCK_PATH oder Standard),
 * sendet argv[1] woertlich (inkl. abschliessendem \n falls noetig) und
 * schreibt die Antwort nach stdout. Exit 0 bei Antwort, 1 bei Fehler.
 */
#include "../common/flux_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <request>\n", argv[0]); return 2; }

    const char *sock = getenv("FLUX_SOCK_PATH");
    if (!sock || !*sock) sock = FLUX_SOCK_PATH;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(fd); return 1;
    }

    char req[FLUX_MAX_LINE];
    int n = snprintf(req, sizeof(req), "%s", argv[1]);
    if (n < 0) { close(fd); return 1; }
    if (n == 0 || req[n - 1] != '\n') {  /* zeilenbasiertes Protokoll */
        if ((size_t)n + 1 < sizeof(req)) { req[n] = '\n'; req[n + 1] = '\0'; }
    }
    if (write(fd, req, strlen(req)) < 0) { perror("write"); close(fd); return 1; }

    char resp[FLUX_MAX_RESPONSE];
    ssize_t r = read(fd, resp, sizeof(resp) - 1);
    close(fd);
    if (r <= 0) { fprintf(stderr, "no response\n"); return 1; }
    resp[r] = '\0';
    fputs(resp, stdout);
    return 0;
}
