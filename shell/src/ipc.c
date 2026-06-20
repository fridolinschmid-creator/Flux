#include "ipc.h"
#include "../../common/flux_protocol.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

/* Haengt ein P:-Teilstueck (escaped) entschluesselt an den Live-Puffer an.
 * Im P:-Frame ist nur "\\n" -> Zeilenumbruch und "\\\\" -> Backslash
 * kodiert (siehe worker.c/write_partial). */
static void append_unescaped(char *dst, size_t *dlen, size_t cap,
                             const char *src, size_t n) {
    size_t o = *dlen;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        if (src[i] == '\\' && i + 1 < n) {
            i++;
            dst[o++] = (src[i] == 'n') ? '\n' : src[i];
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = '\0';
    *dlen = o;
}

/* Gemeinsamer Kern: sendet request, liest die (evtl. gestreamte) Antwort.
 * P:-Frames werden progressiv ueber on_partial gemeldet; out enthaelt am
 * Ende den vollstaendigen Antworttext (das finale A:/ERR: ist massgeblich).
 *
 * Liest in einer Schleife bis zum abschliessenden "\nEND" -- ein einzelnes
 * read() reicht beim Streaming nicht mehr (mehrere Frames). Rueckwaerts-
 * kompatibel: ohne P:-Frames verhaelt sich das wie eine einzelne A:-Antwort. */
static void send_raw_cb(const char *request,
                        flux_partial_cb on_partial, void *ud,
                        char *out, size_t out_cap) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { snprintf(out, out_cap, "fluxaid nicht erreichbar (socket)."); return; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, FLUX_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        snprintf(out, out_cap, "fluxaid laeuft nicht (kein Socket unter %s).", FLUX_SOCK_PATH);
        close(fd);
        return;
    }
    if (write(fd, request, strlen(request)) < 0) {
        snprintf(out, out_cap, "Fehler beim Senden an fluxaid.");
        close(fd);
        return;
    }

    char buf[32768];
    size_t len = 0;       /* belegte Bytes in buf            */
    size_t processed = 0; /* bis hierhin Zeilen ausgewertet  */
    int    in_final = 0;  /* finales A:/ERR: erreicht?       */
    size_t final_off = 0; /* Beginn des finalen Antworttexts */
    size_t live_len = 0;  /* Laenge der zusammengesetzten P:-Antwort in out */
    int    done = 0;
    if (out_cap) out[0] = '\0';

    for (;;) {
        ssize_t n = read(fd, buf + len, sizeof(buf) - 1 - len);
        if (n <= 0) break;
        len += (size_t)n;
        buf[len] = '\0';

        /* Vor dem finalen Frame: zeilenweise nach P:/A:/ERR: scannen. */
        while (!in_final && processed < len) {
            char *nl = memchr(buf + processed, '\n', len - processed);
            if (!nl) break;
            const char *line = buf + processed;
            size_t llen = (size_t)(nl - line);
            size_t next = (size_t)(nl - buf) + 1;

            if (llen >= 2 && line[0] == 'P' && line[1] == ':') {
                append_unescaped(out, &live_len, out_cap, line + 2, llen - 2);
                if (on_partial) on_partial(out, ud);
                processed = next;
            } else if (llen >= 2 && line[0] == 'A' && line[1] == ':') {
                in_final = 1; final_off = processed + 2;
            } else if (llen >= 4 && strncmp(line, "ERR:", 4) == 0) {
                in_final = 1; final_off = processed + 4;
            } else {
                processed = next; /* unbekannte Zeile (z.B. spaeteres S:) */
            }
        }

        /* Im finalen Frame: bis "\nEND" einsammeln (Text darf \n enthalten). */
        if (in_final) {
            char *e = strstr(buf + final_off, "\nEND");
            if (e) {
                size_t flen = (size_t)(e - (buf + final_off));
                if (flen >= out_cap) flen = out_cap - 1;
                memcpy(out, buf + final_off, flen);
                out[flen] = '\0';
                done = 1;
                break;
            }
        }

        if (len >= sizeof(buf) - 1) break; /* Schutz gegen Ueberlauf */
    }
    close(fd);

    if (!done && out[0] == '\0')
        snprintf(out, out_cap, "Keine Antwort von fluxaid erhalten.");
}

void flux_ipc_send_raw(const char *request, char *out, size_t out_cap) {
    send_raw_cb(request, NULL, NULL, out, out_cap);
}

void flux_ipc_ask(const char *question, char *out, size_t out_cap) {
    char req[FLUX_MAX_LINE];
    snprintf(req, sizeof(req), "Q:%s\n", question);
    send_raw_cb(req, NULL, NULL, out, out_cap);
}

void flux_ipc_ask_stream(const char *question,
                         flux_partial_cb on_partial, void *ud,
                         char *out, size_t out_cap) {
    char req[FLUX_MAX_LINE];
    snprintf(req, sizeof(req), "Q:%s\n", question);
    send_raw_cb(req, on_partial, ud, out, out_cap);
}
