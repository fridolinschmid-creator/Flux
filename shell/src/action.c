#include "action.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

const char *flux_action_type_label(flux_action_type_t type) {
    switch (type) {
        case FLUX_ACTION_MAIL:   return "mail";
        case FLUX_ACTION_SMS:    return "sms";
        case FLUX_ACTION_CALL:   return "call";
        case FLUX_ACTION_FLIGHT: return "flight";
        default:                 return "none";
    }
}

/* Normalisiert einen Flugmodus-Zustand auf "an"/"aus" (in-place).
 * Gibt 1 zurueck, wenn s ein erkanntes Token war, sonst 0. */
static int normalize_flight_state(char *s) {
    /* fuehrende/anhaengende Leerzeichen + Zeilenumbrueche entfernen */
    char *b = s;
    while (*b == ' ' || *b == '\n' || *b == '\r' || *b == '\t') b++;
    char buf[16] = {0};
    size_t i = 0;
    for (; b[i] && b[i] != '\n' && b[i] != '\r' && i < sizeof(buf) - 1; i++)
        buf[i] = (char)((b[i] >= 'A' && b[i] <= 'Z') ? b[i] + 32 : b[i]);
    while (i > 0 && buf[i-1] == ' ') buf[--i] = '\0';

    if (strcmp(buf, "an") == 0 || strcmp(buf, "ein") == 0 || strcmp(buf, "on") == 0 ||
        strcmp(buf, "1") == 0 || strcmp(buf, "true") == 0) {
        s[0] = 'a'; s[1] = 'n'; s[2] = '\0'; return 1;
    }
    if (strcmp(buf, "aus") == 0 || strcmp(buf, "off") == 0 || strcmp(buf, "0") == 0 ||
        strcmp(buf, "false") == 0) {
        s[0] = 'a'; s[1] = 'u'; s[2] = 's'; s[3] = '\0'; return 1;
    }
    return 0;
}

static flux_action_type_t parse_type(const char *s) {
    while (*s == ' ') s++;
    if (strcasecmp(s, "mail") == 0 || strcasecmp(s, "email") == 0 ||
        strcasecmp(s, "e-mail") == 0) return FLUX_ACTION_MAIL;
    if (strcasecmp(s, "sms") == 0)  return FLUX_ACTION_SMS;
    if (strcasecmp(s, "call") == 0 || strcasecmp(s, "anruf") == 0) return FLUX_ACTION_CALL;
    if (strcasecmp(s, "flight") == 0 || strcasecmp(s, "flugmodus") == 0) return FLUX_ACTION_FLIGHT;
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
        } else if (strncmp(p, "STATE:", 6) == 0) {
            /* Flugmodus-Zustand -- gleichwertig zu BODY, aber explizit. */
            p += 6;
            const char *e = strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            if (len >= sizeof(out->body)) len = sizeof(out->body) - 1;
            memcpy(out->body, p, len);
            out->body[len] = '\0';
            p = e ? e + 1 : p + len;
        } else if (strncmp(p, "BODY:", 5) == 0) {
            p += 5;
            if (*p == '\n') p++;
            snprintf(out->body, sizeof(out->body), "%s", p);
            break;
        } else {
            const char *e = strchr(p, '\n');
            p = e ? e + 1 : p + strlen(p);
        }
    }

    /* Flugmodus braucht keinen Empfaenger, sondern einen gueltigen
     * Zustand ("an"/"aus") im body. Andere Aktionen brauchen einen
     * Empfaenger. */
    if (out->type == FLUX_ACTION_FLIGHT)
        return normalize_flight_state(out->body);
    return out->to[0] != '\0';
}

void flux_action_build_request(const flux_action_t *a, char *out, size_t out_cap) {
    snprintf(out, out_cap, "X:%s\nTO:%s\nSUBJECT:%s\nBODY:\n%s\n",
             flux_action_type_label(a->type), a->to, a->subject, a->body);
}
