#include "exec.h"
#include "mail.h"
#include "telephony.h"
#include "radio.h"
#include "logsync.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define CONTACTS_PATH "/etc/flux/contacts.txt"

/* "Name <addr>" -> "addr". Sonst unveraendert. */
static void strip_angle_addr(char *s) {
    char *lt = strchr(s, '<');
    char *gt = lt ? strchr(lt, '>') : NULL;
    if (lt && gt && gt > lt) {
        size_t n = (size_t)(gt - lt - 1);
        memmove(s, lt + 1, n);
        s[n] = '\0';
    }
}

/* Loest einen Namen ueber /etc/flux/contacts.txt auf.
 * want_email: 1 = E-Mail-Feld (mit '@'), 0 = Telefon-Feld (Ziffern).
 * Schreibt das Ergebnis nach out und gibt 1 zurueck, wenn gefunden.
 * Format je Zeile: "Name,Telefon,Email[,...]". */
static int resolve_contact(const char *name, int want_email,
                           char *out, size_t cap) {
    if (!name || !*name) return 0;
    FILE *f = fopen(CONTACTS_PATH, "r");
    if (!f) return 0;

    /* Suchbegriff in Kleinbuchstaben */
    char want[128]; size_t wi = 0;
    for (const char *q = name; *q && wi < sizeof(want)-1; q++)
        want[wi++] = (char)tolower((unsigned char)*q);
    want[wi] = '\0';

    char line[256];
    int found = 0;
    while (!found && fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (!line[0] || line[0] == '#') continue;

        /* Name = erstes Feld */
        char *c1 = strchr(line, ',');
        char namebuf[128];
        size_t nl = c1 ? (size_t)(c1 - line) : strlen(line);
        if (nl >= sizeof(namebuf)) nl = sizeof(namebuf)-1;
        memcpy(namebuf, line, nl); namebuf[nl] = '\0';
        char namelo[128]; size_t ni = 0;
        for (const char *q = namebuf; *q && ni < sizeof(namelo)-1; q++)
            namelo[ni++] = (char)tolower((unsigned char)*q);
        namelo[ni] = '\0';
        if (!strstr(namelo, want)) continue;
        if (!c1) continue; /* Zeile ohne Felder -> nichts zum Aufloesen */

        /* passendes Feld suchen: mit '@' (Mail) bzw. mit Ziffer (Telefon) */
        char *save;
        for (char *tok = strtok_r(c1 + 1, ",", &save);
             tok; tok = strtok_r(NULL, ",", &save)) {
            while (*tok == ' ') tok++;
            int has_at = strchr(tok, '@') != NULL;
            int has_digit = 0;
            for (const char *t = tok; *t; t++) if (isdigit((unsigned char)*t)) has_digit = 1;
            if (want_email && has_at) { snprintf(out, cap, "%s", tok); found = 1; break; }
            if (!want_email && has_digit && !has_at) { snprintf(out, cap, "%s", tok); found = 1; break; }
        }
    }
    fclose(f);
    return found;
}

void flux_exec_action(const char *payload, char *out, size_t out_cap) {
    const char *line_end = strchr(payload, '\n');
    char type[16] = {0};
    size_t type_len = line_end ? (size_t)(line_end - payload) : strlen(payload);
    if (type_len >= sizeof(type)) type_len = sizeof(type) - 1;
    memcpy(type, payload, type_len);

    char to[256] = {0}, subject[256] = {0}, body[4096] = {0};
    const char *p = line_end ? line_end + 1 : "";
    while (*p) {
        if (strncmp(p, "TO:", 3) == 0) {
            p += 3;
            const char *e = strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            if (len >= sizeof(to)) len = sizeof(to) - 1;
            memcpy(to, p, len); to[len] = '\0';
            p = e ? e + 1 : p + len;
        } else if (strncmp(p, "SUBJECT:", 8) == 0) {
            p += 8;
            const char *e = strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            if (len >= sizeof(subject)) len = sizeof(subject) - 1;
            memcpy(subject, p, len); subject[len] = '\0';
            p = e ? e + 1 : p + len;
        } else if (strncmp(p, "BODY:", 5) == 0) {
            p += 5;
            if (*p == '\n') p++;
            snprintf(body, sizeof(body), "%s", p);
            break;
        } else {
            const char *e = strchr(p, '\n');
            p = e ? e + 1 : p + strlen(p);
        }
    }

    /* "email"/"e-mail" als Synonym fuer "mail" akzeptieren */
    if (strcasecmp(type, "email") == 0 || strcasecmp(type, "e-mail") == 0)
        snprintf(type, sizeof(type), "mail");

    /* Empfaenger robust aufloesen -- die KI liefert manchmal nur den Namen.
     * "Name <addr>" -> "addr"; reiner Name -> aus Kontakten nachschlagen. */
    strip_angle_addr(to);
    if (strcasecmp(type, "mail") == 0) {
        if (!strchr(to, '@')) {
            char resolved[256];
            if (resolve_contact(to, 1, resolved, sizeof(resolved)))
                snprintf(to, sizeof(to), "%s", resolved);
        }
    } else { /* sms / call: Telefonnummer noetig */
        int has_digit = 0;
        for (const char *t = to; *t; t++) if (isdigit((unsigned char)*t)) has_digit = 1;
        if (!has_digit) {
            char resolved[256];
            if (resolve_contact(to, 0, resolved, sizeof(resolved)))
                snprintf(to, sizeof(to), "%s", resolved);
        }
    }

    if (strcasecmp(type, "flight") == 0) {
        /* Flugmodus: Zustand ("an"/"aus") steht im body. */
        int on = (body[0] == 'a' && body[1] == 'n'); /* "an" -> 1, sonst aus */
        flux_radio_set_airplane(on, out, out_cap);
        return;
    }

    if (strcasecmp(type, "logs") == 0) {
        /* Vom Nutzer angestossener Log-Upload an das konfigurierte Backend. */
        flux_logsync_upload(out, out_cap);
        return;
    }

    if (strcasecmp(type, "mail") == 0) {
        if (!strchr(to, '@')) {
            snprintf(out, out_cap,
                     "Keine E-Mail-Adresse fuer \"%s\" gefunden. Lege den Kontakt "
                     "mit E-Mail an oder gib die Adresse direkt an.", to);
            return;
        }
        flux_mail_send(to, subject, body, out, out_cap);
    } else if (strcasecmp(type, "sms") == 0) {
        flux_telephony_send_sms(to, body, out, out_cap);
    } else if (strcasecmp(type, "call") == 0) {
        flux_telephony_call(to, out, out_cap);
    } else {
        snprintf(out, out_cap, "Unbekannter Aktionstyp '%s'.", type);
    }
}
