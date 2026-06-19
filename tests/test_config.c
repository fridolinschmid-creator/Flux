/* test_config.c -- Unit-Tests fuer flux_config + Verschluesselung at rest.
 *
 * Nutzt FLUX_CONFIG_DIR, um in einem temporaeren Verzeichnis zu arbeiten
 * (kein Root, kein /etc). Prueft Round-Trip mit Verschluesselung, dass die
 * Datei auf der Platte keinen Klartext enthaelt, und dass Legacy-Klartext
 * weiterhin gelesen wird.
 */
#include "../common/flux_config.h"
#include "../common/flux_secret.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int g_ok, g_bad;
#define CHECK(cond) do { \
    if (cond) { g_ok++; } \
    else { g_bad++; printf("  FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static size_t read_file(const char *path, unsigned char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

int main(void) {
    char dir[256];
    snprintf(dir, sizeof(dir), "/tmp/flux-cfg-test-%d", (int)getpid());
    setenv("FLUX_CONFIG_DIR", dir, 1);

    const char *secret = "sk-ant-SUPERSECRET-0xCAFE";
    char path[300];
    snprintf(path, sizeof(path), "%s/flux.conf", dir);

    /* --- Verschluesselter Modus --- */
    setenv("FLUX_CONFIG_ENCRYPT", "1", 1);

    char v[512];
    CHECK(flux_config_set("api_key", secret) == 0);
    CHECK(flux_config_get("api_key", v, sizeof(v)) == 1);
    CHECK(strcmp(v, secret) == 0);

    /* Zweiter Key, erster bleibt erhalten (Round-Trip ueber Datei). */
    CHECK(flux_config_set("pin_hash", "deadbeef") == 0);
    CHECK(flux_config_get("pin_hash", v, sizeof(v)) == 1 && strcmp(v, "deadbeef") == 0);
    CHECK(flux_config_get("api_key", v, sizeof(v)) == 1 && strcmp(v, secret) == 0);

    /* Datei auf Platte: versiegelt und KEIN Klartext-Secret sichtbar. */
    unsigned char raw[4096];
    size_t rn = read_file(path, raw, sizeof(raw));
    CHECK(rn > 0);
    CHECK(flux_secret_is_sealed(raw, rn) == 1);
    CHECK(memmem(raw, rn, secret, strlen(secret)) == NULL); /* Secret nicht im Klartext */

    /* Unbekannter Key -> 0. */
    CHECK(flux_config_get("does_not_exist", v, sizeof(v)) == 0);

    /* --- Legacy-Klartext (ohne Verschluesselung) wird weiter gelesen --- */
    unsetenv("FLUX_CONFIG_ENCRYPT");
    char dir2[256];
    snprintf(dir2, sizeof(dir2), "/tmp/flux-cfg-test-plain-%d", (int)getpid());
    setenv("FLUX_CONFIG_DIR", dir2, 1);
    char path2[300];
    snprintf(path2, sizeof(path2), "%s/flux.conf", dir2);
    mkdir(dir2, 0700);
    FILE *pf = fopen(path2, "w");
    CHECK(pf != NULL);
    if (pf) { fprintf(pf, "# legacy\nmodel=claude-haiku\n"); fclose(pf); }
    CHECK(flux_config_get("model", v, sizeof(v)) == 1 && strcmp(v, "claude-haiku") == 0);

    /* Aufraeumen. */
    unlink(path); rmdir(dir);
    unlink(path2); rmdir(dir2);

    printf("test_config: %d passed, %d failed\n", g_ok, g_bad);
    return g_bad ? 1 : 0;
}
