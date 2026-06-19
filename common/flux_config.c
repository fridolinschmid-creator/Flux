#include "flux_config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define FLUX_CFG_MAX_ENTRIES 32
#define FLUX_CFG_KEY_CAP     64
#define FLUX_CFG_VAL_CAP     512

typedef struct {
    char key[FLUX_CFG_KEY_CAP];
    char value[FLUX_CFG_VAL_CAP];
} flux_cfg_entry_t;

static int load_entries(flux_cfg_entry_t *entries, int max_entries) {
    FILE *f = fopen(FLUX_CONFIG_PATH, "r");
    if (!f) return 0;

    int n = 0;
    char line[FLUX_CFG_KEY_CAP + FLUX_CFG_VAL_CAP];
    while (n < max_entries && fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
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
    fclose(f);
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

    mkdir(FLUX_CONFIG_DIR, 0700);
    FILE *f = fopen(FLUX_CONFIG_PATH, "w");
    if (!f) return -1;
    for (int i = 0; i < n; i++)
        fprintf(f, "%s=%s\n", entries[i].key, entries[i].value);
    fclose(f);
    chmod(FLUX_CONFIG_PATH, 0600);
    return 0;
}
