#include "flux_secret.h"
#include "flux_config.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAGIC      "FLUXSEC1"
#define MAGIC_LEN  8
#define NONCE_LEN  12
#define TAG_LEN    16
#define KEY_LEN    32

static void devkey_path(char *out, size_t cap) {
    snprintf(out, cap, "%s/.devkey", flux_config_dir());
}

/* Liest den 32-Byte Device-Key. create=1 erzeugt ihn (zufaellig, 0600),
 * falls er fehlt. Gibt 1 bei Erfolg zurueck. */
static int load_devkey(unsigned char key[KEY_LEN], int create) {
    char path[512];
    devkey_path(path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(key, 1, KEY_LEN, f);
        fclose(f);
        if (n == KEY_LEN) return 1;
    }
    if (!create) return 0;

    if (RAND_bytes(key, KEY_LEN) != 1) return 0;
    mkdir(flux_config_dir(), 0700);
    f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = fwrite(key, 1, KEY_LEN, f);
    fclose(f);
    chmod(path, 0600);
    return n == KEY_LEN;
}

/* Schluessel = SHA-256(devkey || /etc/machine-id). */
static int derive_key(unsigned char out[KEY_LEN], int create) {
    unsigned char dk[KEY_LEN];
    if (!load_devkey(dk, create)) return 0;

    unsigned char mid[256];
    size_t midn = 0;
    FILE *f = fopen("/etc/machine-id", "rb");
    if (f) { midn = fread(mid, 1, sizeof(mid), f); fclose(f); }

    EVP_MD_CTX *md = EVP_MD_CTX_new();
    if (!md) return 0;
    unsigned int dlen = 0;
    int ok = EVP_DigestInit_ex(md, EVP_sha256(), NULL) == 1 &&
             EVP_DigestUpdate(md, dk, KEY_LEN) == 1 &&
             (midn == 0 || EVP_DigestUpdate(md, mid, midn) == 1) &&
             EVP_DigestFinal_ex(md, out, &dlen) == 1;
    EVP_MD_CTX_free(md);
    return ok && dlen == KEY_LEN;
}

int flux_secret_is_sealed(const unsigned char *buf, size_t len) {
    return len >= MAGIC_LEN && memcmp(buf, MAGIC, MAGIC_LEN) == 0;
}

int flux_secret_active(void) {
    const char *e = getenv("FLUX_CONFIG_ENCRYPT");
    if (e && e[0] == '1') return 1;
    unsigned char k[KEY_LEN];
    return load_devkey(k, 0);
}

int flux_secret_seal(const unsigned char *pt, size_t pt_len,
                     unsigned char *out, size_t out_cap) {
    if (out_cap < MAGIC_LEN + NONCE_LEN + pt_len + TAG_LEN) return -1;

    unsigned char key[KEY_LEN];
    if (!derive_key(key, 1)) return -1;

    unsigned char *nonce = out + MAGIC_LEN;
    if (RAND_bytes(nonce, NONCE_LEN) != 1) return -1;
    memcpy(out, MAGIC, MAGIC_LEN);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    unsigned char *ct = out + MAGIC_LEN + NONCE_LEN;
    int len = 0, ctlen = 0, ok = 0;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, nonce) == 1 &&
        EVP_EncryptUpdate(ctx, ct, &len, pt, (int)pt_len) == 1) {
        ctlen = len;
        if (EVP_EncryptFinal_ex(ctx, ct + ctlen, &len) == 1) {
            ctlen += len;
            if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN,
                                    ct + ctlen) == 1) {
                ok = 1;
            }
        }
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return -1;
    return MAGIC_LEN + NONCE_LEN + ctlen + TAG_LEN;
}

int flux_secret_open(const unsigned char *in, size_t in_len,
                     unsigned char *out, size_t out_cap) {
    if (!flux_secret_is_sealed(in, in_len)) return -1;
    if (in_len < (size_t)(MAGIC_LEN + NONCE_LEN + TAG_LEN)) return -1;

    unsigned char key[KEY_LEN];
    if (!derive_key(key, 0)) return -1;

    const unsigned char *nonce = in + MAGIC_LEN;
    const unsigned char *ct    = in + MAGIC_LEN + NONCE_LEN;
    size_t ctlen = in_len - MAGIC_LEN - NONCE_LEN - TAG_LEN;
    const unsigned char *tag   = ct + ctlen;
    if (ctlen > out_cap) return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int len = 0, ptlen = 0, ok = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, nonce) == 1 &&
        EVP_DecryptUpdate(ctx, out, &len, ct, (int)ctlen) == 1) {
        ptlen = len;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN,
                                (void *)tag) == 1 &&
            EVP_DecryptFinal_ex(ctx, out + ptlen, &len) == 1) {
            ptlen += len;
            ok = 1;
        }
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok ? ptlen : -1;
}
