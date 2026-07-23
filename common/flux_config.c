#include "flux_config.h"
#include "flux_secret.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FLUX_CFG_MAX_ENTRIES 32
#define FLUX_CFG_KEY_CAP     64
#define FLUX_CFG_VAL_CAP     512
#define FLUX_CFG_RAW_CAP     32768  /* Datei (verschluesselt oder Klartext) */

typedef struct {
    char key[FLUX_CFG_KEY_CAP];
    char value[FLUX_CFG_VAL_CAP];
} flux_cfg_entry_t;

const char *flux_config_dir(void) {
    const char *e = getenv("FLUX_CONFIG_DIR");
    return (e && *e) ? e : FLUX_CONFIG_DIR;
}

static void config_path(char *out, size_t cap) {
    snprintf(out, cap, "%s/flux.conf", flux_config_dir());
}

/* Liest die Konfigurationsdatei als Klartext nach buf (NUL-terminiert).
 * Versiegelte Dateien werden transparent entschluesselt; Klartext-Dateien
 * (Legacy/unverschluesselt) werden direkt uebernommen.
 * Gibt die Laenge zurueck oder -1 (Datei fehlt / Entschluesselung scheitert). */
static int read_plaintext(char *buf, size_t cap) {
    char path[512];
    config_path(path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char raw[FLUX_CFG_RAW_CAP];
    size_t n = fread(raw, 1, sizeof(raw), f);
    fclose(f);

    if (flux_secret_is_sealed(raw, n)) {
        int pn = flux_secret_open(raw, n, (unsigned char *)buf, cap - 1);
        if (pn < 0) return -1;
        buf[pn] = '\0';
        return pn;
    }
    if (n >= cap) n = cap - 1;
    memcpy(buf, raw, n);
    buf[n] = '\0';
    return (int)n;
}

static int load_entries(flux_cfg_entry_t *entries, int max_entries) {
    char buf[FLUX_CFG_RAW_CAP];
    if (read_plaintext(buf, sizeof(buf)) < 0) return 0;

    int n = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save);
         line && n < max_entries;
         line = strtok_r(NULL, "\n", &save)) {
        if (line[0] == '\0' || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;

        /* Ueberlange Keys/Werte ueberspringen statt still abzuschneiden --
         * ein abgeschnittener Key wuerde sonst auf den falschen Eintrag
         * zeigen. */
        if (strlen(key) >= FLUX_CFG_KEY_CAP || strlen(value) >= FLUX_CFG_VAL_CAP)
            continue;

        memcpy(entries[n].key, key, strlen(key) + 1);
        memcpy(entries[n].value, value, strlen(value) + 1);
        n++;
    }
    return n;
}

int flux_config_get(const char *key, char *out, size_t out_cap) {
    flux_cfg_entry_t entries[FLUX_CFG_MAX_ENTRIES];
    int n = load_entries(entries, FLUX_CFG_MAX_ENTRIES);
    if (out_cap) out[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (strcmp(entries[i].key, key) == 0) {
            snprintf(out, out_cap, "%s", entries[i].value);
            return 1;
        }
    }
    return 0;
}

/* Schreibt die Eintraege als flux.conf -- verschluesselt, wenn
 * flux_secret_active(), sonst Klartext. Bei einem Verschluesselungsfehler
 * wird bewusst NICHT die Konfiguration verworfen: es wird auf Klartext
 * zurueckgefallen (eine unlesbare Konfig wuerde z.B. den PIN-Hash und
 * damit das Geraet aussperren). */
int flux_config_set(const char *key, const char *value) {
    flux_cfg_entry_t entries[FLUX_CFG_MAX_ENTRIES];
    int n = load_entries(entries, FLUX_CFG_MAX_ENTRIES);

    int found = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(entries[i].key, key) == 0) {
            snprintf(entries[i].value, sizeof(entries[i].value), "%s", value);
            found = 1;
            break;
        }
    }
    if (!found) {
        if (n >= FLUX_CFG_MAX_ENTRIES) return -1;
        snprintf(entries[n].key, sizeof(entries[n].key), "%s", key);
        snprintf(entries[n].value, sizeof(entries[n].value), "%s", value);
        n++;
    }

    /* Klartext-Abbild aufbauen. */
    char plain[FLUX_CFG_RAW_CAP];
    size_t off = 0;
    for (int i = 0; i < n && off < sizeof(plain); i++)
        off += snprintf(plain + off, sizeof(plain) - off, "%s=%s\n",
                        entries[i].key, entries[i].value);

    mkdir(flux_config_dir(), 0700);
    char path[512];
    config_path(path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    int rc = 0;
    if (flux_secret_active()) {
        unsigned char sealed[FLUX_CFG_RAW_CAP + 64];
        int sn = flux_secret_seal((unsigned char *)plain, strlen(plain),
                                  sealed, sizeof(sealed));
        if (sn > 0) {
            if (fwrite(sealed, 1, (size_t)sn, f) != (size_t)sn) rc = -1;
        } else {
            /* Verschluesselung fehlgeschlagen -> Klartext, Konfig nicht verlieren */
            if (fwrite(plain, 1, strlen(plain), f) != strlen(plain)) rc = -1;
        }
    } else {
        if (fwrite(plain, 1, strlen(plain), f) != strlen(plain)) rc = -1;
    }
    fclose(f);
    chmod(path, 0600);
    return rc;
}
