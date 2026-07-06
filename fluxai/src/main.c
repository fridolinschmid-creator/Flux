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
#include "notification.h"
#include "../../common/flux_protocol.h"
#include "../../common/flux_config.h"
#include "../../common/flux_privdrop.h"
#include "logsync.h"
#include "../../common/flux_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

/* Fehler-Hook: meldet ERROR+ ans Backend, wenn der Nutzer das aktiviert
 * hat (log_report=1) UND eine Backend-URL gesetzt ist (opt-in). So bleibt
 * das Backend ueber Hintergrund-Fehler auf dem Laufenden, ohne dass der
 * Nutzer es manuell anstossen muss. */
static void daemon_error_hook(flux_log_level_t level, const char *module,
                              const char *message) {
    char rep[8] = {0};
    if (!flux_config_get("log_report", rep, sizeof(rep)) || rep[0] != '1')
        return;
    flux_logsync_report(flux_log_level_name(level), module, message);
}

static int make_listen_socket(const char *path) {
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { LOGF("socket(): %s", strerror(errno)); exit(1); }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGF("bind(%s): %s", path, strerror(errno)); exit(1);
    }
    chmod(path, 0666); /* jede App darf den Assistenten fragen */
    if (listen(fd, 8) < 0) { LOGF("listen(): %s", strerror(errno)); exit(1); }
    LOGI("Socket bereit: %s", path);
    return fd;
}

/* Socket-Pfad: per FLUX_SOCK_PATH ueberschreibbar (fuer Tests/Dev ohne
 * Root-Zugriff auf /run), sonst der Standard aus flux_protocol.h. */
static const char *resolve_sock_path(void) {
    const char *p = getenv("FLUX_SOCK_PATH");
    return (p && *p) ? p : FLUX_SOCK_PATH;
}

/* Legt das Elternverzeichnis des Socket-Pfads an (z.B. /run/flux). */
static void ensure_sock_dir(const char *sock_path) {
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", sock_path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) { *slash = '\0'; mkdir(dir, 0755); }
}

/* Liefert die UID des verbundenen Peers (SO_PEERCRED). -1 bei Fehler. */
static int peer_uid(int fd, uid_t *uid) {
    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) return -1;
    *uid = cred.uid;
    return 0;
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
        /* Eine bestaetigte Aktion (Anruf/SMS/Mail) darf NUR der
         * vertrauenswuerdige UI-Prozess ausloesen -- sonst koennte jeder
         * lokale Prozess am 0666-Socket den Bestaetigungs-Dialog umgehen
         * und das Geraet zu Anrufen/SMS zwingen. Geprueft wird die
         * Peer-UID: sie muss der eigenen UID des Daemons entsprechen
         * (Shell und fluxaid laufen als derselbe Nutzer). */
        uid_t puid = (uid_t)-1;
        if (peer_uid(cfd, &puid) != 0 || puid != geteuid()) {
            fprintf(stderr,
                    "fluxaid: Aktionsanfrage von nicht autorisierter UID %ld abgelehnt\n",
                    (long)puid);
            const char *msg =
                "ERR:Nicht autorisiert -- Aktionen nur vom UI-Prozess\nEND\n";
            if (write(cfd, msg, strlen(msg)) < 0) { /* Client weg, egal */ }
            close(cfd);
            return;
        }
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
    flux_log_init("fluxaid");
    flux_log_set_error_hook(daemon_error_hook); /* opt-in Backend-Report bei Fehlern */
    flux_provider_init();

    const char *sock_path = resolve_sock_path();
    ensure_sock_dir(sock_path);
    int listen_fd = make_listen_socket(sock_path);

    /* Privilegien ablegen, sobald der Socket gebunden ist (opt-in: nur wenn
     * "service_user" konfiguriert ist -- siehe scripts/setup-users.sh). Der
     * Zustand unter FLUX_CONFIG_DIR muss dem Dienstnutzer gehoeren. */
    if (flux_privdrop("service_user", "FLUX_SERVICE_USER") != 0) {
        fprintf(stderr, "fluxaid: Privilege-Drop fehlgeschlagen -- beende\n");
        return 1;
    }

    fprintf(stderr, "fluxaid: lauscht auf %s\n", sock_path);

    /* Benachrichtigungs-Hintergrund-Thread starten */
    flux_notification_start();

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
        if (cfd < 0) {
            if (errno != EINTR) LOGW("accept(): %s", strerror(errno));
            continue;
        }
        handle_client(cfd); /* ein Request pro Verbindung reicht fuer den Prototyp */
    }
}
