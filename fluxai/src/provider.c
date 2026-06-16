#include "provider.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLUX_DEFAULT_MODEL "claude-haiku-4-5-20251001"
#define FLUX_API_URL       "https://api.anthropic.com/v1/messages"

struct membuf {
    char  *data;
    size_t len;
    size_t cap;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct membuf *mb = userdata;
    size_t add = size * nmemb;
    if (mb->len + add + 1 > mb->cap)
        return 0; /* Antwort zu gross fuer den Demo-Puffer -> abbrechen */
    memcpy(mb->data + mb->len, ptr, add);
    mb->len += add;
    mb->data[mb->len] = '\0';
    return add;
}

void flux_provider_init(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

/* Haengt s JSON-escaped an out an (begrenzt durch cap). */
static void json_escape_append(char *out, size_t cap, const char *s) {
    size_t len = strlen(out);
    for (; *s && len + 2 < cap; s++) {
        char c = *s;
        if (c == '"' || c == '\\') {
            out[len++] = '\\';
            out[len++] = c;
        } else if (c == '\n') {
            out[len++] = '\\';
            out[len++] = 'n';
        } else if ((unsigned char)c < 0x20) {
            continue;
        } else {
            out[len++] = c;
        }
    }
    out[len] = '\0';
}

/* Sucht "<key>":"..." in der Antwort und entschluesselt die gaengigen
 * JSON-Escapes. Bewusst kein vollwertiger JSON-Parser -- fuer eine
 * einzelne erwartete Antwortform reicht das fuer den Prototyp; ein
 * echter Parser ist ein klarer Folgeschritt. key ist je Provider
 * unterschiedlich: Anthropic liefert "text", OpenAI-kompatible APIs
 * (Ollama, LM Studio) liefern "content". key enthaelt nur "<feldname>"
 * (mit Anfuehrungszeichen, ohne Doppelpunkt) -- der Doppelpunkt und
 * optionales Leerraum davor/danach (gueltiges JSON erlaubt das) werden
 * hier ueberbrueckt, statt ihn fest im Aufrufer zu verdrahten. */
static int extract_json_string(const char *json, const char *key, char *out, size_t out_cap) {
    const char *p = strstr(json, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return 0;
    p++;

    size_t o = 0;
    while (*p && *p != '"' && o + 1 < out_cap) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'n': out[o++] = '\n'; break;
                case 't': out[o++] = '\t'; break;
                case '"': out[o++] = '"';  break;
                case '\\': out[o++] = '\\'; break;
                default: out[o++] = *p; break;
            }
        } else {
            out[o++] = *p;
        }
        p++;
    }
    out[o] = '\0';
    return o > 0;
}

/* Lokales Modell zuerst (Ollama oder LM Studio, beide sprechen eine
 * OpenAI-kompatible /v1/chat/completions-API) -- bleibt komplett aus,
 * wenn FLUX_AI_LOCAL_URL nicht gesetzt ist, kein Verhaltensunterschied
 * zum bisherigen reinen Cloud-Fallback. Schlaegt die Anfrage fehl
 * (Server nicht erreichbar, Antwort nicht lesbar), faellt dieselbe
 * Frage stillschweigend an die Cloud weiter -- analog zum Prinzip
 * "lokale Intents zuerst" aus actions.c, nur eine Ebene weiter oben. */
static int try_local_llm(const char *question, char *out, size_t out_cap) {
    const char *base = getenv("FLUX_AI_LOCAL_URL");
    if (!base || !*base) return 0;

    const char *model = getenv("FLUX_AI_LOCAL_MODEL");
    if (!model || !*model) model = "local-model";

    size_t blen = strlen(base);
    if (blen > 0 && base[blen - 1] == '/') blen--;
    char url[512];
    snprintf(url, sizeof(url), "%.*s/v1/chat/completions", (int)blen, base);

    char body[4096];
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"max_tokens\":300,\"messages\":"
             "[{\"role\":\"user\",\"content\":\"", model);
    json_escape_append(body, sizeof(body), question);
    strncat(body, "\"}]}", sizeof(body) - strlen(body) - 1);

    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    char respbuf[16384];
    respbuf[0] = '\0';
    struct membuf mb = { .data = respbuf, .len = 0, .cap = sizeof(respbuf) };

    struct curl_slist *headers = curl_slist_append(NULL, "content-type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mb);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);

    CURLcode res = curl_easy_perform(curl);
    int ok = (res == CURLE_OK) && extract_json_string(respbuf, "\"content\"", out, out_cap);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok;
}

void flux_provider_ask(const char *question, char *out, size_t out_cap) {
    if (try_local_llm(question, out, out_cap))
        return;

    const char *api_key = getenv("FLUX_AI_API_KEY");
    if (!api_key || !*api_key) {
        snprintf(out, out_cap,
            "Kein Cloud-Zugang konfiguriert. Setze FLUX_AI_API_KEY, "
            "um Fragen zu stellen, die ich nicht lokal beantworten kann.");
        return;
    }

    const char *model = getenv("FLUX_AI_MODEL");
    if (!model || !*model) model = FLUX_DEFAULT_MODEL;

    char body[4096];
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"max_tokens\":300,\"messages\":"
             "[{\"role\":\"user\",\"content\":\"", model);
    json_escape_append(body, sizeof(body), question);
    strncat(body, "\"}]}", sizeof(body) - strlen(body) - 1);

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(out, out_cap, "Interner Fehler: curl_easy_init() fehlgeschlagen.");
        return;
    }

    char respbuf[16384];
    respbuf[0] = '\0';
    struct membuf mb = { .data = respbuf, .len = 0, .cap = sizeof(respbuf) };

    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, "content-type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, FLUX_API_URL);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        snprintf(out, out_cap, "Netzwerkfehler: %s", curl_easy_strerror(res));
    } else if (!extract_json_string(respbuf, "\"text\"", out, out_cap)) {
        snprintf(out, out_cap, "Antwort konnte nicht gelesen werden.");
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}
