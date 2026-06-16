#include "contacts.h"
#include "../../common/flux_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define FLUX_CONTACTS_MAX_BYTES 65536

static int valid_field(const char *s) {
    /* Tab und Newline wuerden das Tab-getrennte Format zerstoeren. */
    return s && *s && !strchr(s, '\t') && !strchr(s, '\n');
}

static size_t load_all(char *buf, size_t cap) {
    FILE *f = fopen(FLUX_CONTACTS_PATH, "r");
    if (!f) return 0;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n;
}

int flux_contacts_add(const char *name, const char *phone, char *out, size_t out_cap) {
    if (!valid_field(name) || !valid_field(phone)) {
        snprintf(out, out_cap, "Name oder Nummer ungueltig (kein Tab/Newline erlaubt).");
        return 0;
    }

    mkdir(FLUX_DATA_DIR, 0755);

    static char buf[FLUX_CONTACTS_MAX_BYTES];
    load_all(buf, sizeof(buf));

    FILE *out_f = fopen(FLUX_CONTACTS_PATH ".tmp", "w");
    if (!out_f) {
        snprintf(out, out_cap, "Kontakt konnte nicht gespeichert werden (Datei nicht schreibbar).");
        return 0;
    }

    /* Vorhandene Zeilen uebernehmen, ausser dem gleichnamigen Kontakt --
     * der wird gleich durch die neue Nummer ersetzt statt verdoppelt. */
    char *line = buf, *nl;
    while (line && *line) {
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            if (strcasecmp(line, name) != 0)
                fprintf(out_f, "%s\t%s\n", line, tab + 1);
        }
        line = nl ? nl + 1 : NULL;
    }
    fprintf(out_f, "%s\t%s\n", name, phone);
    fclose(out_f);
    rename(FLUX_CONTACTS_PATH ".tmp", FLUX_CONTACTS_PATH);

    snprintf(out, out_cap, "Kontakt \"%s\" gespeichert (%s).", name, phone);
    return 1;
}

int flux_contacts_find(const char *name, char *out, size_t out_cap) {
    static char buf[FLUX_CONTACTS_MAX_BYTES];
    if (load_all(buf, sizeof(buf)) == 0) return 0;

    char *line = buf, *nl;
    while (line && *line) {
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            if (strcasestr(line, name)) {
                snprintf(out, out_cap, "%s\t%s", line, tab + 1);
                return 1;
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    return 0;
}

int flux_contacts_list(char *out, size_t out_cap) {
    static char buf[FLUX_CONTACTS_MAX_BYTES];
    if (load_all(buf, sizeof(buf)) == 0) {
        snprintf(out, out_cap, "Noch keine Kontakte gespeichert.");
        return 0;
    }

    size_t len = 0;
    out[0] = '\0';
    char *line = buf, *nl;
    int n = 0;
    while (line && *line) {
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            int written = snprintf(out + len, out_cap - len, "%s%s: %s",
                                    n ? "\n" : "", line, tab + 1);
            if (written < 0 || (size_t)written >= out_cap - len) break;
            len += (size_t)written;
            n++;
        }
        line = nl ? nl + 1 : NULL;
    }
    if (n == 0) snprintf(out, out_cap, "Noch keine Kontakte gespeichert.");
    return n > 0;
}
