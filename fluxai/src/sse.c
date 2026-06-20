#include "sse.h"

#include <string.h>

/* Haengt ein Stueck an den Gesamtpuffer an (begrenzt) und meldet die noch
 * nicht gemeldeten Bytes per Callback -- abhaengig von der Praefix-
 * Entscheidung. */
static void emit(flux_sse_t *s) {
    if (!s->cb) { s->emitted_len = s->full_len; return; }

    if (!s->decided) {
        /* Noch nicht genug Text, um ein Direktiv-Praefix auszuschliessen? */
        if (s->full_len < s->decide_at) return;
        s->decided = 1;
        s->suppressed = 0;
        if (s->suppress) {
            for (int i = 0; s->suppress[i]; i++) {
                if (strncmp(s->full, s->suppress[i], strlen(s->suppress[i])) == 0) {
                    s->suppressed = 1;
                    break;
                }
            }
        }
    }
    if (s->suppressed) { s->emitted_len = s->full_len; return; }

    if (s->emitted_len < s->full_len) {
        s->cb(s->full + s->emitted_len, s->ud);
        s->emitted_len = s->full_len;
    }
}

static void append_full(flux_sse_t *s, const char *t) {
    while (*t && s->full_len + 1 < s->full_cap)
        s->full[s->full_len++] = *t++;
    s->full[s->full_len] = '\0';
}

/* Zieht das "text"-Feld aus einer text_delta-Datenzeile und entschluesselt
 * die JSON-Escapes. */
static void handle_data_line(flux_sse_t *s, const char *p) {
    if (!strstr(p, "text_delta")) return;
    const char *q = strstr(p, "\"text\":\"");
    if (!q) return;
    q += 8;

    char buf[8192];
    size_t o = 0;
    while (*q && *q != '"' && o + 1 < sizeof(buf)) {
        if (*q == '\\' && q[1]) {
            q++;
            switch (*q) {
                case 'n': buf[o++] = '\n'; break;
                case 't': buf[o++] = '\t'; break;
                case 'r': break;
                case '"': buf[o++] = '"';  break;
                case '\\': buf[o++] = '\\'; break;
                case '/': buf[o++] = '/';  break;
                case 'u': /* \uXXXX -- selten in kurzen Antworten; ueberspringen */
                    if (q[1] && q[2] && q[3] && q[4]) q += 4;
                    break;
                default: buf[o++] = *q; break;
            }
            q++;
        } else {
            buf[o++] = *q++;
        }
    }
    buf[o] = '\0';
    if (o == 0) return;

    append_full(s, buf);
    emit(s);
}

static void process_line(flux_sse_t *s) {
    s->line[s->line_len] = '\0';
    const char *p = s->line;
    if (strncmp(p, "data:", 5) == 0) {
        p += 5;
        while (*p == ' ') p++;
        if (strcmp(p, "[DONE]") == 0) return;
        handle_data_line(s, p);
    }
    /* "event:"-Zeilen und Leerzeilen interessieren uns nicht. */
}

void flux_sse_init(flux_sse_t *s, char *full, size_t full_cap,
                   flux_delta_cb cb, void *ud,
                   const char *const *suppress) {
    s->full = full;
    s->full_cap = full_cap;
    s->full_len = 0;
    s->emitted_len = 0;
    s->line_len = 0;
    s->cb = cb;
    s->ud = ud;
    s->suppress = suppress;
    s->decided = 0;
    s->suppressed = 0;
    if (full_cap) s->full[0] = '\0';

    /* Ab wie vielen Bytes laesst sich das laengste Praefix entscheiden? */
    s->decide_at = 1;
    if (suppress) {
        for (int i = 0; suppress[i]; i++) {
            size_t l = strlen(suppress[i]);
            if (l > s->decide_at) s->decide_at = l;
        }
    }
}

void flux_sse_feed(flux_sse_t *s, const char *bytes, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = bytes[i];
        if (c == '\n') {
            process_line(s);
            s->line_len = 0;
        } else if (c != '\r') {
            if (s->line_len + 1 < sizeof(s->line))
                s->line[s->line_len++] = c;
            /* ueberlange Zeile: Rest bis zum naechsten \n verwerfen */
        }
    }
}

void flux_sse_finish(flux_sse_t *s) {
    /* angefangene Zeile ohne abschliessendes \n noch verarbeiten */
    if (s->line_len > 0) {
        process_line(s);
        s->line_len = 0;
    }
    /* Strom-Ende: Entscheidung jetzt erzwingen (decide_at=0). emit() fuehrt
     * weiterhin die Praefix-Pruefung aus -- strncmp ist auch bei einer
     * kuerzeren Antwort als das Praefix sicher (kein Fehl-Match). */
    if (!s->decided) {
        s->decide_at = 0;
        emit(s);
    }
}
