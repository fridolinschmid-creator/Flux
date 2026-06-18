#include "provider.h"
#include "tools.h"
#include "../../common/flux_config.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLUX_DEFAULT_MODEL "claude-haiku-4-5-20251001"
#define FLUX_API_URL       "https://api.anthropic.com/v1/messages"

/* Erlaubt der KI, statt einer normalen Textantwort eine konkrete
 * Aktion vorzuschlagen (Mail/SMS/Anruf). flux-shell zeigt dafuer einen
 * Bestaetigungs-Dialog -- die KI fuehrt nie direkt etwas aus (siehe
 * shell/src/action.h fuer den Parser, fluxai/src/exec.c fuer die
 * Ausfuehrung nach Bestaetigung).
 *
 * Zusaetzlich kann die KI Tools aufrufen: TOOL:<name>\nARG:<wert>
 * Der Daemon fuehrt das Tool aus und sendet das Ergebnis in einem
 * zweiten API-Aufruf zurueck. Maximal ein Tool-Aufruf pro Anfrage. */
#define FLUX_SYSTEM_PROMPT_BASE \
    "Du bist der KI-Assistent des Telefon-Betriebssystems Flux. " \
    "Antworte normalerweise kurz und klar auf Deutsch in normalem Text. " \
    "WENN der Nutzer eindeutig eine E-Mail senden, eine SMS senden oder " \
    "einen Anruf taetigen moechte UND du Empfaenger und Inhalt sicher " \
    "ableiten kannst, antworte AUSSCHLIESSLICH in diesem Format:\n" \
    "ACTION:<mail|sms|call>\n" \
    "TO:<Empfaenger>\n" \
    "SUBJECT:<Betreff, nur bei mail>\n" \
    "BODY:\n" \
    "<Text>\n" \
    "Falls Empfaenger oder Inhalt unklar sind, frage nach. "

/* --- Conversation context (last CTX_MAX turns) --- */
#define CTX_MAX 3
typedef struct { char q[256]; char a[512]; } ctx_turn_t;
static ctx_turn_t ctx_history[CTX_MAX];
static int        ctx_n = 0;

static void ctx_add(const char *q, const char *a) {
    if (ctx_n < CTX_MAX) {
        snprintf(ctx_history[ctx_n].q, sizeof(ctx_history[0].q), "%s", q);
        snprintf(ctx_history[ctx_n].a, sizeof(ctx_history[0].a), "%s", a);
        ctx_n++;
    } else {
        memmove(ctx_history, ctx_history + 1, (CTX_MAX - 1) * sizeof(ctx_turn_t));
        snprintf(ctx_history[CTX_MAX-1].q, sizeof(ctx_history[0].q), "%s", q);
        snprintf(ctx_history[CTX_MAX-1].a, sizeof(ctx_history[0].a), "%s", a);
    }
}

struct membuf {
    char  *data;
    size_t len;
    size_t cap;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct membuf *mb = userdata;
    size_t add = size * nmemb;
    if (mb->len + add + 1 > mb->cap)
        return 0;
    memcpy(mb->data + mb->len, ptr, add);
    mb->len += add;
    mb->data[mb->len] = '\0';
    return add;
}

void flux_provider_init(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

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

/* Sucht "text":"..." in der Anthropic-Antwort und entschluesselt JSON-Escapes. */
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

/* Sendet einen API-Request. system_prompt und final_q werden JSON-escaped.
 * Fuegt die letzten ctx_n Gespraechsrunden als Kontext ein.
 * use_ctx steuert ob der globale Gespraechsverlauf eingebettet wird.
 * Gibt 1 bei Erfolg. */
static int api_call(const char *api_key, const char *model,
                    const char *system_prompt, const char *final_q,
                    char *out, size_t out_cap, int use_ctx) {
    char body[24576];
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"max_tokens\":600,\"system\":\"", model);
    json_escape_append(body, sizeof(body), system_prompt);
    strncat(body, "\",\"messages\":[", sizeof(body) - strlen(body) - 1);

    if (use_ctx) {
        for (int i = 0; i < ctx_n; i++) {
            strncat(body, "{\"role\":\"user\",\"content\":\"",
                    sizeof(body) - strlen(body) - 1);
            json_escape_append(body, sizeof(body), ctx_history[i].q);
            strncat(body, "\"},{\"role\":\"assistant\",\"content\":\"",
                    sizeof(body) - strlen(body) - 1);
            json_escape_append(body, sizeof(body), ctx_history[i].a);
            strncat(body, "\"},", sizeof(body) - strlen(body) - 1);
        }
    }

    strncat(body, "{\"role\":\"user\",\"content\":\"",
            sizeof(body) - strlen(body) - 1);
    json_escape_append(body, sizeof(body), final_q);
    strncat(body, "\"}]}", sizeof(body) - strlen(body) - 1);

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(out, out_cap, "Interner Fehler: curl_easy_init() fehlgeschlagen.");
        return 0;
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
    int ok = 0;
    if (res != CURLE_OK) {
        snprintf(out, out_cap, "Netzwerkfehler: %s", curl_easy_strerror(res));
    } else if (!extract_text(respbuf, out, out_cap)) {
        snprintf(out, out_cap, "Antwort konnte nicht gelesen werden.");
    } else {
        ok = 1;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok;
}

/* Prueft ob die KI-Antwort ein Tool-Aufruf ist und parst ihn.
 * Erwartet: "TOOL:<name>\nARG:<arg>" */
static int parse_tool_call(const char *response,
                            char *tool_name, size_t name_cap,
                            char *tool_arg,  size_t arg_cap) {
    if (strncmp(response, "TOOL:", 5) != 0) return 0;
    const char *p = response + 5;

    /* Tool-Name bis zum Newline */
    const char *nl = strchr(p, '\n');
    if (!nl) return 0;
    size_t nlen = (size_t)(nl - p);
    if (nlen >= name_cap) nlen = name_cap - 1;
    memcpy(tool_name, p, nlen);
    tool_name[nlen] = '\0';

    /* ARG: */
    p = nl + 1;
    if (strncmp(p, "ARG:", 4) != 0) {
        tool_arg[0] = '\0';
        return 1;
    }
    p += 4;
    size_t alen = strlen(p);
    /* Trailing Newline entfernen */
    while (alen > 0 && (p[alen-1] == '\n' || p[alen-1] == '\r')) alen--;
    if (alen >= arg_cap) alen = arg_cap - 1;
    memcpy(tool_arg, p, alen);
    tool_arg[alen] = '\0';
    return 1;
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
            "Einstellungen ein (oder setze FLUX_AI_API_KEY).");
        return;
    }

    const char *model = getenv("FLUX_AI_MODEL");
    if (!model || !*model) model = FLUX_DEFAULT_MODEL;

    /* System-Prompt = Basis + Tool-Beschreibung */
    char system_prompt[4096];
    snprintf(system_prompt, sizeof(system_prompt),
             "%s\n\n%s", FLUX_SYSTEM_PROMPT_BASE, flux_tools_description());

    /* Erster API-Aufruf -- mit Gespraechsverlauf */
    if (!api_call(api_key, model, system_prompt, question, out, out_cap, 1))
        return;

    /* Tool-Aufruf? */
    char tool_name[64], tool_arg[1024];
    if (!parse_tool_call(out, tool_name, sizeof(tool_name),
                              tool_arg, sizeof(tool_arg))) {
        /* Normale Antwort -- Austausch im Verlauf speichern */
        ctx_add(question, out);
        return;
    }

    /* Tool ausfuehren */
    char tool_result[4096];
    snprintf(tool_result, sizeof(tool_result),
             "Fehler: unbekanntes Tool '%s'", tool_name);
    flux_tool_exec(tool_name, tool_arg, tool_result, sizeof(tool_result));

    /* Zweiter API-Aufruf mit Tool-Ergebnis als Kontext.
     * Kein Gespraechsverlauf einbetten -- der followup-Text enthaelt
     * bereits die urspruengliche Frage als Kontext. */
    char followup[8192];
    snprintf(followup, sizeof(followup),
             "Urspruengliche Frage: \"%s\"\n"
             "Du hast Tool '%s' mit Argument '%s' aufgerufen.\n"
             "Ergebnis des Tools:\n%s\n\n"
             "Beantworte jetzt die urspruengliche Frage mit diesen Daten. "
             "Antworte auf Deutsch, kurz und klar.",
             question, tool_name, tool_arg, tool_result);

    if (api_call(api_key, model, system_prompt, followup, out, out_cap, 0))
        ctx_add(question, out);
}
