/* vision.c -- KI-Bildanalyse via Anthropic Vision API.
 * PPM → JPEG (ImageMagick convert), base64-codiert, POST an /v1/messages.
 * Benoetigt: libcurl, ImageMagick (convert-Befehl).
 */
#include "vision.h"
#include "../../common/flux_config.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#define VISION_MODEL   "claude-haiku-4-5-20251001"
#define VISION_API_URL "https://api.anthropic.com/v1/messages"

/* Portabler Speicherloesch-Helfer: explicit_bzero ist nicht in jeder
 * libc deklariert und darf nicht durch den Optimierer entfernt werden. */
static void flux_secure_zero(void *ptr, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
}


/* ---- Base64-Encoder ------------------------------------------------- */

static const char b64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const unsigned char *in, size_t in_len,
                              char *out, size_t out_cap) {
    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 3) {
        unsigned int n  = (unsigned int)in[i] << 16;
        if (i + 1 < in_len) n |= (unsigned int)in[i + 1] << 8;
        if (i + 2 < in_len) n |= (unsigned int)in[i + 2];
        if (o + 5 >= out_cap) break;
        out[o++] = b64_chars[(n >> 18) & 0x3F];
        out[o++] = b64_chars[(n >> 12) & 0x3F];
        out[o++] = (i + 1 < in_len) ? b64_chars[(n >>  6) & 0x3F] : '=';
        out[o++] = (i + 2 < in_len) ? b64_chars[ n        & 0x3F] : '=';
    }
    out[o] = '\0';
    return o;
}

/* ---- Curl-Hilfsfunktionen ------------------------------------------- */

struct dyn_buf {
    char  *data;
    size_t len;
    size_t cap;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    struct dyn_buf *mb = ud;
    size_t add = size * nmemb;
    if (mb->len + add + 1 > mb->cap) {
        size_t nc = mb->cap * 2 + add + 1024;
        char *nd = realloc(mb->data, nc);
        if (!nd) return 0;
        mb->data = nd;
        mb->cap  = nc;
    }
    memcpy(mb->data + mb->len, ptr, add);
    mb->len += add;
    mb->data[mb->len] = '\0';
    return add;
}

/* Sucht "text":"..." in JSON-Antwort und dekodiert Escapes. */
static int extract_text(const char *json, char *out, size_t cap) {
    const char *key = "\"text\":\"";
    const char *p   = strstr(json, key);
    if (!p) return 0;
    p += strlen(key);
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < cap) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'n':  out[o++] = '\n'; break;
                case 't':  out[o++] = '\t'; break;
                case '"':  out[o++] = '"';  break;
                case '\\': out[o++] = '\\'; break;
                default:   out[o++] = *p;   break;
            }
        } else {
            out[o++] = *p;
        }
        p++;
    }
    out[o] = '\0';
    return o > 0;
}

int flux_vision_analyze(const char *ppm_path, char *out, size_t out_cap,
                        const char *api_key_hint) {
    /* API-Key bestimmen */
    char key_buf[256] = {0};
    const char *api_key = api_key_hint;
    if (!api_key || !*api_key) {
        if (flux_config_get("api_key", key_buf, sizeof(key_buf)) && key_buf[0])
            api_key = key_buf;
        else
            api_key = getenv("FLUX_AI_API_KEY");
    }
    if (!api_key || !*api_key) {
        snprintf(out, out_cap, "Kein API-Key konfiguriert.");
        return 0;
    }

    /* PPM → JPEG per ImageMagick (Anthropic akzeptiert kein PPM).
     * Kein system()/Shell -- direkt execl() um Shell-Injection zu vermeiden. */
    const char *tmp_jpg = "/tmp/flux_vision_img.jpg";
    {
        pid_t pid = fork();
        if (pid < 0) {
            snprintf(out, out_cap, "fork() fehlgeschlagen.");
            return 0;
        }
        if (pid == 0) {
            /* Child: stderr schliessen, dann convert ausfuehren */
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
            char quality[] = "80";
            char *argv[] = { "convert", (char *)ppm_path, "-quality", quality,
                             (char *)tmp_jpg, NULL };
            execvp("convert", argv);
            _exit(127);
        }
        int wstatus = 0;
        waitpid(pid, &wstatus, 0);
        if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
            snprintf(out, out_cap,
                     "Bildkonvertierung fehlgeschlagen (convert nicht installiert?).");
            return 0;
        }
    }

    /* JPEG-Datei einlesen */
    FILE *jf = fopen(tmp_jpg, "rb");
    if (!jf) { snprintf(out, out_cap, "Bilddatei nicht lesbar."); return 0; }
    fseek(jf, 0, SEEK_END);
    long fsize = ftell(jf);
    fseek(jf, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(jf);
        snprintf(out, out_cap, "Bild zu gross oder leer.");
        return 0;
    }
    unsigned char *jpeg_data = malloc((size_t)fsize);
    if (!jpeg_data) { fclose(jf); return 0; }
    if ((long)fread(jpeg_data, 1, (size_t)fsize, jf) != fsize) {
        fclose(jf); free(jpeg_data);
        snprintf(out, out_cap, "Lesefehler beim Bild.");
        return 0;
    }
    fclose(jf);
    unlink(tmp_jpg);

    /* Base64-Kodierung */
    size_t b64_cap = (size_t)fsize * 4 / 3 + 8;
    char *b64 = malloc(b64_cap);
    if (!b64) { free(jpeg_data); return 0; }
    base64_encode(jpeg_data, (size_t)fsize, b64, b64_cap);
    free(jpeg_data);

    /* JSON-Body aufbauen:
     * { model, max_tokens, messages: [{ role, content: [image, text] }] } */
    size_t body_cap = b64_cap + 2048;
    char *body = malloc(body_cap);
    if (!body) { free(b64); return 0; }

    int blen = snprintf(body, body_cap,
        "{\"model\":\"%s\",\"max_tokens\":600,"
        "\"messages\":[{\"role\":\"user\",\"content\":["
        "{\"type\":\"image\",\"source\":{"
            "\"type\":\"base64\","
            "\"media_type\":\"image/jpeg\","
            "\"data\":\"%s\"}},"
        "{\"type\":\"text\",\"text\":"
            "\"Beschreibe kurz, was auf diesem Bild zu sehen ist. "
            "Nenne Motive, Stimmung und -- falls erkennbar -- den Ort oder Kontext. "
            "Antworte auf Deutsch, maximal 3 Saetze.\"}"
        "]}]}",
        VISION_MODEL, b64);
    free(b64);

    if (blen <= 0 || (size_t)blen >= body_cap) {
        free(body);
        snprintf(out, out_cap, "Anfrage-Aufbau fehlgeschlagen.");
        return 0;
    }

    /* HTTP-POST per libcurl */
    CURL *curl = curl_easy_init();
    if (!curl) { free(body); return 0; }

    struct dyn_buf resp = { .data = malloc(4096), .len = 0, .cap = 4096 };
    if (!resp.data) { curl_easy_cleanup(curl); free(body); return 0; }
    resp.data[0] = '\0';

    char auth_hdr[300];
    snprintf(auth_hdr, sizeof(auth_hdr), "x-api-key: %s", api_key);

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, auth_hdr);
    hdrs = curl_slist_append(hdrs, "anthropic-version: 2023-06-01");
    hdrs = curl_slist_append(hdrs, "content-type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, VISION_API_URL);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)blen);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    int ok = 0;
    if (res != CURLE_OK) {
        snprintf(out, out_cap, "Netzwerkfehler: %s", curl_easy_strerror(res));
    } else if (http_code == 401) {
        snprintf(out, out_cap, "API-Key ungueltig oder abgelaufen (HTTP 401).");
    } else if (http_code == 429) {
        snprintf(out, out_cap, "API-Limit erreicht, bitte kurz warten (HTTP 429).");
    } else if (http_code != 200) {
        snprintf(out, out_cap, "Bildanalyse-Fehler (HTTP %ld).", http_code);
    } else if (!extract_text(resp.data, out, out_cap)) {
        const char *ekey = "\"message\":\"";
        const char *ep = strstr(resp.data, ekey);
        if (ep) {
            ep += strlen(ekey);
            char errbuf[256] = {0};
            size_t ei = 0;
            while (*ep && *ep != '"' && ei + 1 < sizeof(errbuf))
                errbuf[ei++] = *ep++;
            errbuf[ei] = '\0';
            snprintf(out, out_cap, "KI-Fehler: %s", errbuf);
        } else {
            snprintf(out, out_cap, "KI-Antwort konnte nicht gelesen werden.");
        }
    } else {
        ok = 1;
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    free(resp.data);
    free(body);
    flux_secure_zero(key_buf, sizeof(key_buf));
    return ok;
}
