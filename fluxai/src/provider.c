#include "provider.h"
#include "../../common/flux_config.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLUX_DEFAULT_MODEL "claude-haiku-4-5-20251001"
#define FLUX_API_URL       "https://api.anthropic.com/v1/messages"

/* Erlaubt der KI, statt einer normalen Textantwort eine konkrete
 * Aktion vorzuschlagen (Mail/SMS/Anruf). flux-shell zeigt dafuer einen
 * Bestaetigungs-Dialog (Senden/Bearbeiten/Abbrechen) -- die KI fuehrt
 * also nie direkt etwas aus, sie schlaegt nur vor (siehe
 * shell/src/action.h fuer den Parser, fluxai/src/exec.c fuer die
 * tatsaechliche Ausfuehrung nach Bestaetigung). */
#define FLUX_SYSTEM_PROMPT \
    "Du bist der KI-Assistent des Telefon-Betriebssystems Flux. " \
    "Antworte normalerweise kurz und klar auf Deutsch in normalem Text. " \
    "WENN der Nutzer eindeutig eine E-Mail senden, eine SMS senden oder " \
    "einen Anruf taetigen moechte UND du Empfaenger und Inhalt sicher aus " \
    "der Nachricht ableiten kannst, antworte AUSSCHLIESSLICH in folgendem " \
    "Format, ohne zusaetzlichen Text davor oder danach:\n" \
    "ACTION:<mail|sms|call>\n" \
    "TO:<E-Mail-Adresse, Telefonnummer oder Name>\n" \
    "SUBJECT:<Betreff, nur bei mail, sonst leer lassen>\n" \
    "BODY:\n" \
    "<Nachrichtentext, bei call ein kurzer Anrufgrund>\n" \
    "Falls Empfaenger oder Inhalt unklar sind, frage stattdessen ganz " \
    "normal nach den fehlenden Angaben (kein ACTION-Format). Wenn der " \
    "Nutzer offensichtlich keine Nachricht/keinen Anruf will, antworte " \
    "immer ganz normal in Text."

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

/* Sucht "text":"..." in der Anthropic-Antwort und entschluesselt die
 * gaengigen JSON-Escapes. Bewusst kein vollwertiger JSON-Parser --
 * fuer eine einzelne erwartete Antwortform reicht das fuer den
 * Prototyp; ein echter Parser ist ein klarer Folgeschritt. */
static int extract_text(const char *json, char *out, size_t out_cap) {
    const char *key = "\"text\":\"";
    const char *p = strstr(json, key);
    if (!p) return 0;
    p += strlen(key);

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

void flux_provider_ask(const char *question, char *out, size_t out_cap) {
    char key_buf[256];
    const char *api_key = NULL;
    if (flux_config_get("api_key", key_buf, sizeof(key_buf)) && key_buf[0])
        api_key = key_buf;
    else
        api_key = getenv("FLUX_AI_API_KEY");

    if (!api_key || !*api_key) {
        snprintf(out, out_cap,
            "Kein Cloud-Zugang konfiguriert. Trage einen API-Key in den "
            "Einstellungen ein (oder setze FLUX_AI_API_KEY), um Fragen zu "
            "stellen, die ich nicht lokal beantworten kann.");
        return;
    }

    const char *model = getenv("FLUX_AI_MODEL");
    if (!model || !*model) model = FLUX_DEFAULT_MODEL;

    char body[8192];
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"max_tokens\":500,\"system\":\"", model);
    json_escape_append(body, sizeof(body), FLUX_SYSTEM_PROMPT);
    strncat(body, "\",\"messages\":[{\"role\":\"user\",\"content\":\"",
            sizeof(body) - strlen(body) - 1);
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
    } else if (!extract_text(respbuf, out, out_cap)) {
        snprintf(out, out_cap, "Antwort konnte nicht gelesen werden.");
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}
