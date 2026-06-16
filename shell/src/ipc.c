#include "ipc.h"
#include "../../common/flux_protocol.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

void flux_ipc_ask(const char *question, char *out, size_t out_cap) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(out, out_cap, "fluxaid nicht erreichbar (socket).");
        return;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, FLUX_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        snprintf(out, out_cap, "fluxaid laeuft nicht (kein Socket unter %s).", FLUX_SOCK_PATH);
        close(fd);
        return;
    }

    char req[FLUX_MAX_LINE];
    snprintf(req, sizeof(req), "Q:%s\n", question);
    if (write(fd, req, strlen(req)) < 0) {
        snprintf(out, out_cap, "Fehler beim Senden an fluxaid.");
        close(fd);
        return;
    }

    char resp[8300];
    ssize_t n = read(fd, resp, sizeof(resp) - 1);
    close(fd);
    if (n <= 0) {
        snprintf(out, out_cap, "Keine Antwort von fluxaid erhalten.");
        return;
    }
    resp[n] = '\0';

    const char *body = resp;
    if (strncmp(body, "A:", 2) == 0 || strncmp(body, "ERR:", 4) == 0)
        body += (body[1] == ':') ? 2 : 4;

    char *end = strstr(body, "\nEND");
    if (end) *end = '\0';

    snprintf(out, out_cap, "%s", body);
}
