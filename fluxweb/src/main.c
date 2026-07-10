/* fluxweb/src/main.c -- Socket-Server fuer den WPE-Browser-Companion-
 * Prozess. Siehe README.md fuer die Architektur-Begruendung und den
 * Verifikationsstatus (dieser Teil: auf dem Host kompiliert und
 * eigenstaendig getestet -- webview.c/die WPE-Aufrufe: nicht).
 *
 * Ein GLib-Main-Loop statt der select()-Schleife von fluxaid: WPE
 * WebKit braucht GLib fuer seine eigene Event-Verarbeitung (Netzwerk,
 * JS, Rendering-Callbacks), ein zweiter Loop nebenher wuerde nur
 * Race-Bedingungen schaffen.
 */
#include "webview.h"
#include "../../common/flux_webview_protocol.h"

#include <glib.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

static int g_viewport_w = 480, g_viewport_h = 854;

/* Der Client, der gerade auf eine Antwort wartet (ein Request pro
 * Verbindung, wie beim fluxaid-Protokoll) -- gesetzt beim Verbindungs-
 * aufbau, geleert nach der Antwort. -1 = kein wartender Client. */
static int g_pending_fd = -1;

static void send_line(int fd, const char *msg) {
    char buf[FLUX_WEBVIEW_MAX_LINE];
    int n = snprintf(buf, sizeof(buf), "%s\nEND\n", msg);
    if (n > 0) {
        ssize_t w = write(fd, buf, (size_t)n);
        (void)w; /* Client kann jederzeit weg sein, egal */
    }
}

static void finish_pending(const char *msg) {
    if (g_pending_fd < 0) return;
    send_line(g_pending_fd, msg);
    close(g_pending_fd);
    g_pending_fd = -1;
}

/* webview.c ruft das auf, sobald ein neuer Frame fertig ist. */
static void on_frame(const char *path, int w, int h, void *user) {
    (void)user;
    char msg[FLUX_WEBVIEW_MAX_LINE];
    if (w < 0) snprintf(msg, sizeof(msg), "ERR:%s", path);
    else       snprintf(msg, sizeof(msg), "FRAME:%s,%d,%d", path, w, h);
    finish_pending(msg);
}

static void handle_line(int fd, char *line) {
    /* Ein vorheriger Client, der nie eine Antwort bekam (z.B. Timeout),
     * wird verworfen -- nur ein Request gleichzeitig, wie das gesamte
     * Protokoll es vorsieht (siehe flux_webview_protocol.h). */
    if (g_pending_fd >= 0 && g_pending_fd != fd) {
        close(g_pending_fd);
    }
    g_pending_fd = fd;

    if (strncmp(line, "LOAD:", 5) == 0) {
        webview_load(line + 5);
    } else if (strncmp(line, "CLICK:", 6) == 0) {
        int x = 0, y = 0;
        if (sscanf(line + 6, "%d,%d", &x, &y) == 2) webview_click(x, y);
        else finish_pending("ERR:ungueltiges CLICK-Format");
    } else if (strncmp(line, "SCROLL:", 7) == 0) {
        int dy = 0;
        if (sscanf(line + 7, "%d", &dy) == 1) webview_scroll(dy);
        else finish_pending("ERR:ungueltiges SCROLL-Format");
    } else if (strncmp(line, "SIZE:", 5) == 0) {
        int w = 0, h = 0;
        if (sscanf(line + 5, "%d,%d", &w, &h) == 2) {
            g_viewport_w = w; g_viewport_h = h;
            webview_set_size(w, h);
            finish_pending("FRAME:,0,0"); /* Groesse gesetzt, kein Frame noetig */
        } else {
            finish_pending("ERR:ungueltiges SIZE-Format");
        }
    } else {
        finish_pending("ERR:unbekannter Befehl");
    }
}

static gboolean on_client_readable(gint fd, GIOCondition cond, gpointer user) {
    (void)user;
    if (cond & (G_IO_ERR | G_IO_HUP)) { close(fd); if (fd == g_pending_fd) g_pending_fd = -1; return G_SOURCE_REMOVE; }

    char line[FLUX_WEBVIEW_MAX_LINE];
    ssize_t n = read(fd, line, sizeof(line) - 1);
    if (n <= 0) { close(fd); if (fd == g_pending_fd) g_pending_fd = -1; return G_SOURCE_REMOVE; }
    line[n] = '\0';
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';

    handle_line(fd, line);
    /* Antwort kommt asynchron ueber on_frame()/finish_pending() -- der
     * fd bleibt bis dahin offen, dieser Watch wird trotzdem entfernt
     * (ein Request pro Verbindung, kein weiteres read() noetig). */
    return G_SOURCE_REMOVE;
}

static gboolean on_listen_readable(gint listen_fd, GIOCondition cond, gpointer user) {
    (void)user;
    if (cond & (G_IO_ERR | G_IO_HUP)) return G_SOURCE_CONTINUE;
    int cfd = accept(listen_fd, NULL, NULL);
    if (cfd < 0) return G_SOURCE_CONTINUE;
    g_unix_fd_add(cfd, G_IO_IN | G_IO_ERR | G_IO_HUP, on_client_readable, NULL);
    return G_SOURCE_CONTINUE;
}

static int make_listen_socket(const char *path) {
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) { *slash = '\0'; mkdir(dir, 0755); }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    unlink(path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }
    chmod(path, 0666); /* wie fluxaid.sock: lokale Prozesse, keine Netz-Exposition */
    if (listen(fd, 4) < 0) { perror("listen"); close(fd); return -1; }
    return fd;
}

int main(void) {
    mkdir(FLUX_WEBVIEW_FRAME_DIR, 0755);

    int listen_fd = make_listen_socket(FLUX_WEBVIEW_SOCK_PATH);
    if (listen_fd < 0) return 1;

    if (webview_init(on_frame, NULL) != 0) {
        fprintf(stderr, "fluxweb: webview_init() fehlgeschlagen\n");
        return 1;
    }
    webview_set_size(g_viewport_w, g_viewport_h);

    g_unix_fd_add(listen_fd, G_IO_IN, on_listen_readable, NULL);

    fprintf(stderr, "fluxweb: lauscht auf %s\n", FLUX_WEBVIEW_SOCK_PATH);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}
