#include "ipc.h"
#include "../../common/flux_protocol.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

/* Verbindet, schickt req (muss bereits mit '\n' enden), liest die
 * Antwort und entfernt das "A:"/"ERR:"-Praefix sowie das "\nEND"-Suffix.
 * Gibt 1 bei "A:"-Antwort, 0 bei "ERR:"/Verbindungsfehler zurueck --
 * out enthaelt in jedem Fall einen lesbaren Text. */
static int send_request(const char *req, char *out, size_t out_cap) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(out, out_cap, "fluxaid nicht erreichbar (socket).");
        return 0;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, FLUX_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        snprintf(out, out_cap, "fluxaid laeuft nicht (kein Socket unter %s).", FLUX_SOCK_PATH);
        close(fd);
        return 0;
    }

    if (write(fd, req, strlen(req)) < 0) {
        snprintf(out, out_cap, "Fehler beim Senden an fluxaid.");
        close(fd);
        return 0;
    }

    char resp[8300];
    ssize_t n = read(fd, resp, sizeof(resp) - 1);
    close(fd);
    if (n <= 0) {
        snprintf(out, out_cap, "Keine Antwort von fluxaid erhalten.");
        return 0;
    }
    resp[n] = '\0';

    int is_ok = (strncmp(resp, "A:", 2) == 0);
    const char *body = resp + (is_ok ? 2 : (strncmp(resp, "ERR:", 4) == 0 ? 4 : 0));

    char *end = strstr(body, "\nEND");
    if (end) *end = '\0';

    snprintf(out, out_cap, "%s", body);
    return is_ok;
}

void flux_ipc_ask(const char *question, char *out, size_t out_cap) {
    char req[FLUX_MAX_LINE];
    snprintf(req, sizeof(req), "Q:%s\n", question);
    send_request(req, out, out_cap);
}

void flux_ipc_add_contact(const char *name, const char *phone, char *out, size_t out_cap) {
    char req[FLUX_MAX_LINE];
    snprintf(req, sizeof(req), "C:%s\t%s\n", name, phone);
    send_request(req, out, out_cap);
}

int flux_ipc_find_contact(const char *name, char *out, size_t out_cap) {
    char req[FLUX_MAX_LINE];
    snprintf(req, sizeof(req), "F:%s\n", name);
    return send_request(req, out, out_cap);
}

void flux_ipc_send_email(const char *to, const char *subject, const char *body,
                          char *out, size_t out_cap) {
    char req[FLUX_MAX_LINE];
    snprintf(req, sizeof(req), "M:%s\t%s\t%s\n", to, subject, body);
    send_request(req, out, out_cap);
}
