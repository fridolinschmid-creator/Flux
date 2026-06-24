/* vision.c -- KI-Bildanalyse, zwei austauschbare Backends.
 *
 * Backend-Wahl ueber den Config-Key `vision_backend`:
 *   "cloud" (Default) -- Anthropic Vision API (/v1/messages), wie bisher.
 *   "local"           -- lokaler VLM-Server (OpenAI-kompatibel,
 *                        /v1/chat/completions mit Bild-Input), local-first.
 *
 * Beide Wege: PPM → JPEG (ImageMagick convert), base64-codiert, POST.
 * Der lokale Pfag ist analog zum llama.cpp-Textanbieter gebaut: kein
 * Vendoring, keine neue Bibliothek, nur HTTP an einen konfigurierbaren
 * lokalen Endpunkt (Config `vlm_url`, env-Fallback VLM_URL).
 *
 * EHRLICHKEIT: Ist der lokale VLM-Server nicht erreichbar, gibt es eine
 * wahrheitsgemaesse Meldung statt einer erfundenen Bildbeschreibung.
 *
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

/* Lokaler VLM-Server (OpenAI-kompatibel). Endpunkt + Modell konfigurierbar. */
#define VLM_DEFAULT_URL   "http://127.0.0.1:8081/v1/chat/completions"
#define VLM_DEFAULT_MODEL "moondream"

/* Bild-Analyse-Prompt -- fuer beide Backends gleich. */
#define VISION_PROMPT \
    "Beschreibe kurz, was auf diesem Bild zu sehen ist. " \
    "Nenne Motive, Stimmung und -- falls erkennbar -- den Ort oder Kontext. " \
    "Antworte auf Deutsch, maximal 3 Saetze."

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

/* Sucht das uebergebene JSON-Schluesselmuster (z.B. "\"text\":\"" fuer
 * Anthropic, "\"content\":\"" fuer das OpenAI-Format) und dekodiert Escapes. */
static int extract_by_key(const char *json, const char *key,
                          char *out, size_t cap) {
    const char *p = strstr(json, key);
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

/* PPM → JPEG (ImageMagick) → base64. Gibt einen malloc()-Puffer zurueck, den
 * der Aufrufer freigeben muss; bei Fehler NULL und eine ehrliche Meldung in
 * *out. Diese Stufe ist fuer beide Backends (Cloud/Lokal) identisch. */
static char *load_image_b64(const char *ppm_path, char *out, size_t out_cap) {
    /* PPM → JPEG per ImageMagick ohne system()-Shell-Injection */
    const char *tmp_jpg = "/tmp/flux_vision_img.jpg";
    {
        pid_t pid = fork();
        if (pid < 0) {
            snprintf(out, out_cap, "fork() fehlgeschlagen.");
            return NULL;
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
            return NULL;
        }
    }

    /* JPEG-Datei einlesen */
    FILE *jf = fopen(tmp_jpg, "rb");
    if (!jf) { snprintf(out, out_cap, "Bilddatei nicht lesbar."); return NULL; }
    fseek(jf, 0, SEEK_END);
    long fsize = ftell(jf);
    fseek(jf, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(jf);
        snprintf(out, out_cap, "Bild zu gross oder leer.");
        return NULL;
    }
    unsigned char *jpeg_data = malloc((size_t)fsize);
    if (!jpeg_data) { fclose(jf); snprintf(out, out_cap, "Kein Speicher."); return NULL; }
    if ((long)fread(jpeg_data, 1, (size_t)fsize, jf) != fsize) {
        fclose(jf); free(jpeg_data);
        snprintf(out, out_cap, "Lesefehler beim Bild.");
        return NULL;
    }
    fclose(jf);
    unlink(tmp_jpg);

    /* Base64-Kodierung */
    size_t b64_cap = (size_t)fsize * 4 / 3 + 8;
    char *b64 = malloc(b64_cap);
    if (!b64) { free(jpeg_data); snprintf(out, out_cap, "Kein Speicher."); return NULL; }
    base64_encode(jpeg_data, (size_t)fsize, b64, b64_cap);
    free(jpeg_data);
    return b64; /* Aufrufer muss free()en; Laenge passt zu b64_cap */
}

/* ---- Cloud-Backend (Anthropic Vision, unveraendertes Verhalten) ----- */

static int vision_cloud(const char *b64, char *out, size_t out_cap,
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

    /* JSON-Body: { model, max_tokens, messages:[{role,content:[image,text]}] } */
    size_t body_cap = strlen(b64) + 2048;
    char *body = malloc(body_cap);
    if (!body) { snprintf(out, out_cap, "Kein Speicher."); return 0; }

    int blen = snprintf(body, body_cap,
        "{\"model\":\"%s\",\"max_tokens\":600,"
        "\"messages\":[{\"role\":\"user\",\"content\":["
        "{\"type\":\"image\",\"source\":{"
            "\"type\":\"base64\","
            "\"media_type\":\"image/jpeg\","
            "\"data\":\"%s\"}},"
        "{\"type\":\"text\",\"text\":\"" VISION_PROMPT "\"}"
        "]}]}",
        VISION_MODEL, b64);

    if (blen <= 0 || (size_t)blen >= body_cap) {
        free(body);
        snprintf(out, out_cap, "Anfrage-Aufbau fehlgeschlagen.");
        return 0;
    }

    CURL *curl = curl_easy_init();
    if (!curl) { free(body); snprintf(out, out_cap, "curl-Init fehlgeschlagen."); return 0; }

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
    } else if (!extract_by_key(resp.data, "\"text\":\"", out, out_cap)) {
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

/* ---- Lokales Backend (VLM-Server, OpenAI-kompatibel) ----------------
 *
 * Schickt das Bild als data:-URI im OpenAI-Vision-Format an einen lokalen
 * Server (/v1/chat/completions). Analog zum llama.cpp-Textanbieter: kein
 * API-Key noetig, nur HTTP an einen konfigurierbaren lokalen Endpunkt.
 *
 * ANDOCK-STELLE fuer das echte lokale VLM: Hier muss ein VLM-Server laufen,
 * der Bild-Input im OpenAI-Format versteht -- z.B. moondream, SmolVLM oder
 * MiniCPM-V (per llama-server mit --mmproj oder einem eigenen Server). Modell
 * `vlm_model` und Endpunkt `vlm_url` machen das Backend austauschbar. */
static int vision_local(const char *b64, char *out, size_t out_cap) {
    /* Endpunkt aufloesen: Config `vlm_url` > env VLM_URL > Default. */
    char url[512] = {0};
    if (!(flux_config_get("vlm_url", url, sizeof(url)) && url[0])) {
        const char *env = getenv("VLM_URL");
        snprintf(url, sizeof(url), "%s", (env && *env) ? env : VLM_DEFAULT_URL);
    }
    /* Modell: Config `vlm_model` > Default. */
    char model[200] = {0};
    if (!(flux_config_get("vlm_model", model, sizeof(model)) && model[0]))
        snprintf(model, sizeof(model), "%s", VLM_DEFAULT_MODEL);

    /* OpenAI-Vision-Body: content = [text, image_url(data:-URI)]. Das
     * base64-JPEG wird als data:image/jpeg;base64,... eingebettet. */
    size_t body_cap = strlen(b64) + 2048;
    char *body = malloc(body_cap);
    if (!body) { snprintf(out, out_cap, "Kein Speicher."); return 0; }

    int blen = snprintf(body, body_cap,
        "{\"model\":\"%s\",\"max_tokens\":600,"
        "\"messages\":[{\"role\":\"user\",\"content\":["
        "{\"type\":\"text\",\"text\":\"" VISION_PROMPT "\"},"
        "{\"type\":\"image_url\",\"image_url\":{"
            "\"url\":\"data:image/jpeg;base64,%s\"}}"
        "]}]}",
        model, b64);

    if (blen <= 0 || (size_t)blen >= body_cap) {
        free(body);
        snprintf(out, out_cap, "Anfrage-Aufbau fehlgeschlagen.");
        return 0;
    }

    CURL *curl = curl_easy_init();
    if (!curl) { free(body); snprintf(out, out_cap, "curl-Init fehlgeschlagen."); return 0; }

    struct dyn_buf resp = { .data = malloc(4096), .len = 0, .cap = 4096 };
    if (!resp.data) { curl_easy_cleanup(curl); free(body); return 0; }
    resp.data[0] = '\0';

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "content-type: application/json");
    /* Lokaler Server braucht typischerweise keinen Bearer-Key. */

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)blen);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L); /* lokale VLMs sind langsamer */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);

    CURLcode res = curl_easy_perform(curl);
    int ok = 0;
    if (res != CURLE_OK) {
        /* EHRLICH: lokaler VLM-Server nicht erreichbar -> keine Fantasie. */
        snprintf(out, out_cap,
            "Kein lokaler VLM-Server erkannt -- laeuft moondream/llama-server "
            "mit Vision unter %s? (%s)", url, curl_easy_strerror(res));
    } else if (!extract_by_key(resp.data, "\"content\":\"", out, out_cap)) {
        snprintf(out, out_cap,
            "VLM-Antwort konnte nicht gelesen werden (laeuft unter %s ein "
            "Vision-faehiges Modell?).", url);
    } else {
        ok = 1;
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    free(resp.data);
    free(body);
    return ok;
}

/* Dispatcher: waehlt anhand des Config-Keys `vision_backend` zwischen dem
 * Cloud-Pfad (Default, Anthropic) und dem lokalen VLM-Pfad. Die Bild-Aufbereitung
 * (PPM→JPEG→base64) ist fuer beide gleich und passiert nur einmal. */
int flux_vision_analyze(const char *ppm_path, char *out, size_t out_cap,
                        const char *api_key_hint) {
    char backend[16] = {0};
    flux_config_get("vision_backend", backend, sizeof(backend));
    int local = (strcmp(backend, "local") == 0);

    char *b64 = load_image_b64(ppm_path, out, out_cap);
    if (!b64) return 0; /* Fehlermeldung steht bereits in out */

    int ok = local ? vision_local(b64, out, out_cap)
                    : vision_cloud(b64, out, out_cap, api_key_hint);
    free(b64);
    return ok;
}
