/* vision.c -- KI-Bildanalyse via Anthropic Vision API.
 * PPM → JPEG (ImageMagick convert), base64-codiert, POST an /v1/messages.
 * Benoetigt: libcurl, ImageMagick (convert-Befehl).
 */
#include "vision.h"
#include "provider.h"
#include "../../common/flux_config.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

/* Vision laeuft ausschliesslich gegen die Anthropic Messages API: nur dieser
 * Anbieter unterstuetzt das unten gebaute Image-Content-Block-Format. Das
 * konkrete Modell wird zur Laufzeit vom aktiven Anbieter aufgeloest
 * (flux_provider_vision), der Endpunkt bleibt fest. */
#define VISION_API_URL "https://api.anthropic.com/v1/messages"

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
    /* API-Key und Modell bestimmen.
     *
     * Bildanalyse braucht einen Vision-faehigen Anbieter; im Multi-Provider-
     * System ist das aktuell nur Anthropic (FMT_ANTHROPIC). Ist ein expliziter
     * api_key_hint gegeben, wird dieser als Override genutzt (immer mit dem
     * Anthropic-Vision-Modell). Andernfalls fragen wir den aktiven Anbieter
     * ab -- ist dieser NICHT Anthropic, brechen wir mit einer klaren Meldung
     * ab, statt ein Anthropic-Format an einen fremden Endpunkt zu schicken. */
    char key_buf[512]   = {0};
    char model_buf[200] = {0};
    char label_buf[128] = {0};
    const char *api_key = NULL;
    const char *model   = NULL;

    if (api_key_hint && *api_key_hint) {
        api_key = api_key_hint;
        /* Modell des aktiven Anbieters mitnehmen, sofern Anthropic; sonst
         * Standard-Vision-Modell. label_buf wird hier nicht benoetigt. */
        if (flux_provider_vision(NULL, 0, model_buf, sizeof(model_buf),
                                 NULL, 0) && model_buf[0])
            model = model_buf;
        else
            model = "claude-haiku-4-5-20251001";
    } else if (flux_provider_vision(key_buf, sizeof(key_buf),
                                    model_buf, sizeof(model_buf),
                                    label_buf, sizeof(label_buf))) {
        api_key = key_buf;
        model   = model_buf;
    } else {
        /* Aktiver Anbieter ist nicht Vision-faehig oder hat keinen Key. */
        if (label_buf[0])
            snprintf(out, out_cap,
                     "Bildanalyse benoetigt einen Vision-faehigen Anbieter "
                     "(derzeit Anthropic). Aktiver Anbieter: %s.", label_buf);
        else
            snprintf(out, out_cap, "Kein API-Key konfiguriert.");
        return 0;
    }
    if (!api_key || !*api_key) {
        snprintf(out, out_cap, "Kein API-Key konfiguriert.");
        return 0;
    }

    /* PPM → JPEG per ImageMagick ohne system()-Shell-Injection.
     *
     * Pro Aufruf eine eindeutige Tempdatei via mkstemp() statt eines festen
     * Pfads: Ein vorhersagbarer Name in einem geteilten /tmp ist anfaellig fuer
     * Symlink-Angriffe und nicht nebenlaeufig sicher (der Daemon forkt pro
     * Anfrage). mkstemp legt die Datei sicher (O_EXCL, 0600) an; wir schliessen
     * den Deskriptor und lassen ImageMagick exakt diesen Pfad ueberschreiben.
     * Format wird via "jpg:"-Praefix erzwungen, unabhaengig von der Endung. */
    char tmpl[] = "/tmp/flux_vision_XXXXXX";
    int tfd = mkstemp(tmpl);
    if (tfd < 0) {
        snprintf(out, out_cap, "Temporaere Datei konnte nicht angelegt werden.");
        return 0;
    }
    close(tfd);
    const char *tmp_jpg = tmpl;

    {
        char jpg_target[64];
        snprintf(jpg_target, sizeof(jpg_target), "jpg:%s", tmp_jpg);

        pid_t pid = fork();
        if (pid < 0) {
            unlink(tmp_jpg);
            snprintf(out, out_cap, "fork() fehlgeschlagen.");
            return 0;
        }
        if (pid == 0) {
            /* Child: stderr schliessen, dann convert ausfuehren */
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
            char quality[] = "80";
            char *argv[] = { "convert", (char *)ppm_path, "-quality", quality,
                             jpg_target, NULL };
            execvp("convert", argv);
            _exit(127);
        }
        int wstatus = 0;
        waitpid(pid, &wstatus, 0);
        if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
            unlink(tmp_jpg);
            snprintf(out, out_cap,
                     "Bildkonvertierung fehlgeschlagen (convert nicht installiert?).");
            return 0;
        }
    }

    /* JPEG-Datei einlesen */
    FILE *jf = fopen(tmp_jpg, "rb");
    if (!jf) {
        unlink(tmp_jpg);
        snprintf(out, out_cap, "Bilddatei nicht lesbar.");
        return 0;
    }
    fseek(jf, 0, SEEK_END);
    long fsize = ftell(jf);
    fseek(jf, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(jf);
        unlink(tmp_jpg);
        snprintf(out, out_cap, "Bild zu gross oder leer.");
        return 0;
    }
    unsigned char *jpeg_data = malloc((size_t)fsize);
    if (!jpeg_data) { fclose(jf); unlink(tmp_jpg); return 0; }
    if ((long)fread(jpeg_data, 1, (size_t)fsize, jf) != fsize) {
        fclose(jf); free(jpeg_data); unlink(tmp_jpg);
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
        model, b64);
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
    int ok = 0;
    if (res != CURLE_OK) {
        snprintf(out, out_cap, "Netzwerkfehler: %s", curl_easy_strerror(res));
    } else if (!extract_text(resp.data, out, out_cap)) {
        snprintf(out, out_cap, "KI-Antwort konnte nicht gelesen werden.");
    } else {
        ok = 1;
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    free(resp.data);
    free(body);
    return ok;
}
