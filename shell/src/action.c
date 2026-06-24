#include "action.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

const char *flux_action_type_label(flux_action_type_t type) {
    switch (type) {
        case FLUX_ACTION_MAIL:    return "mail";
        case FLUX_ACTION_SMS:     return "sms";
        case FLUX_ACTION_CALL:    return "call";
        case FLUX_ACTION_SETTING: return "setting";
        default:                return "none";
    }
}

static flux_action_type_t parse_type(const char *s) {
    while (*s == ' ') s++;
    if (strcasecmp(s, "mail") == 0 || strcasecmp(s, "email") == 0 ||
        strcasecmp(s, "e-mail") == 0) return FLUX_ACTION_MAIL;
    if (strcasecmp(s, "sms") == 0)  return FLUX_ACTION_SMS;
    if (strcasecmp(s, "call") == 0 || strcasecmp(s, "anruf") == 0) return FLUX_ACTION_CALL;
    if (strcasecmp(s, "setting") == 0 || strcasecmp(s, "einstellung") == 0)
        return FLUX_ACTION_SETTING;
    return FLUX_ACTION_NONE;
}

int flux_action_parse(const char *answer, flux_action_t *out) {
    memset(out, 0, sizeof(*out));

    /* "ACTION:" am Anfang ODER an einem Zeilenanfang finden -- manche
     * Modelle schreiben etwas Vortext vor den Aktionsblock. */
    const char *a = answer;
    if (strncmp(a, "ACTION:", 7) != 0) {
        const char *nl = strstr(a, "\nACTION:");
        if (!nl) return 0;
        a = nl + 1;
    }

    const char *p = a + 7;
    const char *line_end = strchr(p, '\n');
    if (!line_end) return 0;

    char type_buf[16] = {0};
    size_t type_len = (size_t)(line_end - p);
    if (type_len >= sizeof(type_buf)) return 0;
    memcpy(type_buf, p, type_len);

    out->type = parse_type(type_buf);
    if (out->type == FLUX_ACTION_NONE) return 0;

    p = line_end + 1;

    /* Einstellungs-Aktion: eigenes Mini-Format KEY/VALUE/DESC. Die
     * eigentliche Sicherheits-Pruefung (Allowlist + Wert) macht der
     * Daemon (fluxai/src/exec.c) -- die Shell parst nur zum Anzeigen. */
    if (out->type == FLUX_ACTION_SETTING) {
        while (*p) {
            const char *e = strchr(p, '\n');
            size_t llen = e ? (size_t)(e - p) : strlen(p);
            if (strncmp(p, "KEY:", 4) == 0) {
                const char *v = p + 4; size_t vl = llen - 4;
                if (vl >= sizeof(out->key)) vl = sizeof(out->key) - 1;
                memcpy(out->key, v, vl); out->key[vl] = '\0';
            } else if (strncmp(p, "VALUE:", 6) == 0) {
                const char *v = p + 6; size_t vl = llen - 6;
                if (vl >= sizeof(out->value)) vl = sizeof(out->value) - 1;
                memcpy(out->value, v, vl); out->value[vl] = '\0';
            } else if (strncmp(p, "DESC:", 5) == 0) {
                const char *v = p + 5; size_t vl = llen - 5;
                if (vl >= sizeof(out->desc)) vl = sizeof(out->desc) - 1;
                memcpy(out->desc, v, vl); out->desc[vl] = '\0';
            }
            p = e ? e + 1 : p + llen;
        }
        /* Whitespace am Anfang von KEY/VALUE entfernen */
        char *k = out->key;   while (*k == ' ') k++; if (k != out->key) memmove(out->key, k, strlen(k) + 1);
        char *vv = out->value; while (*vv == ' ') vv++; if (vv != out->value) memmove(out->value, vv, strlen(vv) + 1);
        return out->key[0] != '\0';
    }

    while (*p) {
        if (strncmp(p, "TO:", 3) == 0) {
            p += 3;
            const char *e = strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            if (len >= sizeof(out->to)) len = sizeof(out->to) - 1;
            memcpy(out->to, p, len);
            out->to[len] = '\0';
            p = e ? e + 1 : p + len;
        } else if (strncmp(p, "SUBJECT:", 8) == 0) {
            p += 8;
            const char *e = strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            if (len >= sizeof(out->subject)) len = sizeof(out->subject) - 1;
            memcpy(out->subject, p, len);
            out->subject[len] = '\0';
            p = e ? e + 1 : p + len;
        } else if (strncmp(p, "BODY:", 5) == 0) {
            p += 5;
            if (*p == '\n') p++;
            snprintf(out->body, sizeof(out->body), "%s", p);
            return out->to[0] != '\0';
        } else {
            const char *e = strchr(p, '\n');
            p = e ? e + 1 : p + strlen(p);
        }
    }
    return out->to[0] != '\0';
}

void flux_action_build_request(const flux_action_t *a, char *out, size_t out_cap) {
    if (a->type == FLUX_ACTION_SETTING) {
        snprintf(out, out_cap, "X:setting\nKEY:%s\nVALUE:%s\n", a->key, a->value);
        return;
    }
    snprintf(out, out_cap, "X:%s\nTO:%s\nSUBJECT:%s\nBODY:\n%s\n",
             flux_action_type_label(a->type), a->to, a->subject, a->body);
}
