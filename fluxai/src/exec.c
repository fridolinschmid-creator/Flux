#include "exec.h"
#include "mail.h"
#include "telephony.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

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

    if (strcasecmp(type, "mail") == 0) {
        flux_mail_send(to, subject, body, out, out_cap);
    } else if (strcasecmp(type, "sms") == 0) {
        flux_telephony_send_sms(to, body, out, out_cap);
    } else if (strcasecmp(type, "call") == 0) {
        flux_telephony_call(to, out, out_cap);
    } else {
        snprintf(out, out_cap, "Unbekannter Aktionstyp '%s'.", type);
    }
}
