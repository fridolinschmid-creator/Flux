#include "provider.h"
#include "tools.h"
#include "../../common/flux_config.h"
#include "../../common/flux_log.h"

#include <curl/curl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * Multi-Provider-Unterstuetzung
 *
 * Flux kann gegen mehrere KI-Anbieter sprechen. Es gibt zwei API-Formate:
 *   - FMT_ANTHROPIC  : Anthropic Messages API (system separat, content[].text,
 *                      Header x-api-key + anthropic-version, Prompt-Caching)
 *   - FMT_OPENAI     : OpenAI-kompatibel (system als erste Nachricht,
 *                      choices[].message.content, Header Authorization: Bearer)
 *                      -- DeepSeek und NVIDIA NIM nutzen dieses Format.
 *
 * Der aktive Anbieter wird ueber den Config-Key `ai_provider` gewaehlt
 * (Standard: anthropic). Der API-Key kommt aus dem providerspezifischen
 * Config-Key oder der Umgebungsvariable. Das Modell ist pro Anbieter
 * konfigurierbar (mit sinnvollem Standard).
 * ========================================================================== */

/* Maximale Anzahl aufeinanderfolgender Tool-Schritte pro Anfrage
 * (Agenten-Schleife) -- verhindert Endlosschleifen. */
#define FLUX_MAX_TOOL_STEPS 6

typedef enum { FMT_ANTHROPIC, FMT_OPENAI } api_format_t;

typedef struct {
    const char  *id;            /* interner Bezeichner (ai_provider-Wert) */
    const char  *label;         /* Anzeigename */
    const char  *url;           /* API-Endpunkt */
    api_format_t format;
    const char  *key_cfg;       /* Config-Key fuer den API-Key */
    const char  *model_cfg;     /* Config-Key fuer das Modell */
    const char  *default_model; /* Standardmodell, falls keins gesetzt */
    const char  *env_key;       /* Umgebungsvariable als Fallback fuer Key */
} flux_provider_def_t;

/* Vordefinierte Anbieter -- in den Einstellungen auswaehlbar. */
static const flux_provider_def_t PROVIDERS[] = {
    { "anthropic", "Anthropic Claude",
      "https://api.anthropic.com/v1/messages", FMT_ANTHROPIC,
      "api_key", "anthropic_model", "claude-haiku-4-5-20251001",
      "FLUX_AI_API_KEY" },
    { "deepseek", "DeepSeek",
      "https://api.deepseek.com/chat/completions", FMT_OPENAI,
      "deepseek_key", "deepseek_model", "deepseek-chat",
      "DEEPSEEK_API_KEY" },
    { "nvidia", "NVIDIA NIM",
      "https://integrate.api.nvidia.com/v1/chat/completions", FMT_OPENAI,
      "nvidia_key", "nvidia_model", "meta/llama-3.1-8b-instruct",
      "NVIDIA_API_KEY" },
};
static const int PROVIDERS_N = (int)(sizeof(PROVIDERS) / sizeof(PROVIDERS[0]));

static const flux_provider_def_t *provider_by_id(const char *id) {
    if (id && *id)
        for (int i = 0; i < PROVIDERS_N; i++)
            if (strcmp(PROVIDERS[i].id, id) == 0) return &PROVIDERS[i];
    return &PROVIDERS[0]; /* Standard: anthropic */
}

/* Ermittelt den aktiven Anbieter, dessen API-Key und Modell.
 * Gibt 1 zurueck, wenn ein nutzbarer Key vorliegt, sonst 0. */
static const flux_provider_def_t *resolve_provider(char *key_out, size_t key_cap,
                                                   char *model_out, size_t model_cap) {
    char sel[64] = {0};
    flux_config_get("ai_provider", sel, sizeof(sel));
    const flux_provider_def_t *p = provider_by_id(sel);

    if (key_out && key_cap) {
        key_out[0] = '\0';
        char buf[512] = {0};
        if (flux_config_get(p->key_cfg, buf, sizeof(buf)) && buf[0])
            snprintf(key_out, key_cap, "%s", buf);
        else {
            const char *env = p->env_key ? getenv(p->env_key) : NULL;
            if (env && *env) snprintf(key_out, key_cap, "%s", env);
        }
    }
    if (model_out && model_cap) {
        char buf[200] = {0};
        if (flux_config_get(p->model_cfg, buf, sizeof(buf)) && buf[0])
            snprintf(model_out, model_cap, "%s", buf);
        else
            snprintf(model_out, model_cap, "%s", p->default_model);
    }
    return p;
}

int flux_provider_active(char *key_out, size_t key_cap,
                         char *model_out, size_t model_cap) {
    char key[512] = {0};
    resolve_provider(key, sizeof(key), model_out, model_cap);
    if (key_out && key_cap) snprintf(key_out, key_cap, "%s", key);
    return key[0] != '\0';
}

int flux_provider_available(void) {
    char key[512] = {0};
    resolve_provider(key, sizeof(key), NULL, 0);
    return key[0] != '\0';
}

/* --- Conversation context (last CTX_MAX turns) --- */
#define CTX_MAX 12
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

/* Dekodiert einen JSON-String ab *p (direkt hinter dem oeffnenden ")
 * bis zum schliessenden ". */
static int decode_json_string(const char *p, char *out, size_t out_cap) {
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < out_cap) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'n': out[o++] = '\n'; break;
                case 't': out[o++] = '\t'; break;
                case 'r': break;
                case '"': out[o++] = '"';  break;
                case '/': out[o++] = '/';  break;
                case '\\': out[o++] = '\\'; break;
                case 'u': {
                    /* \uXXXX -- nur Basic-Latin/Latin-1 grob behandeln */
                    if (p[1] && p[2] && p[3] && p[4]) {
                        char hex[5] = { p[1], p[2], p[3], p[4], 0 };
                        unsigned int cp = (unsigned int)strtol(hex, NULL, 16);
                        if (cp < 0x80) { out[o++] = (char)cp; }
                        else if (cp < 0x800) {
                            if (o + 2 < out_cap) {
                                out[o++] = (char)(0xC0 | (cp >> 6));
                                out[o++] = (char)(0x80 | (cp & 0x3F));
                            }
                        } else {
                            if (o + 3 < out_cap) {
                                out[o++] = (char)(0xE0 | (cp >> 12));
                                out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                out[o++] = (char)(0x80 | (cp & 0x3F));
                            }
                        }
                        p += 4;
                    }
                    break;
                }
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

/* Extrahiert den Antworttext aus der JSON-Antwort -- formatabhaengig.
 * Anthropic: "text":"..."   OpenAI: "content":"..." (im choices[].message). */
static int extract_text(const char *json, api_format_t fmt, char *out, size_t out_cap) {
    const char *key = (fmt == FMT_ANTHROPIC) ? "\"text\":\"" : "\"content\":\"";
    const char *p = strstr(json, key);
    if (p) {
        p += strlen(key);
        if (decode_json_string(p, out, out_cap)) return 1;
    }
    /* Fehlerfall: API liefert {"error":{"message":"..."}} oder aehnlich */
    const char *err = strstr(json, "\"message\":\"");
    if (err) {
        err += strlen("\"message\":\"");
        char msg[512];
        if (decode_json_string(err, msg, sizeof(msg))) {
            snprintf(out, out_cap, "API-Fehler: %s", msg);
            return 0;
        }
    }
    return 0;
}

/* Baut den Request-Body fuer das jeweilige Format und sendet ihn.
 * Gibt 1 bei Erfolg. */
static int api_call(const flux_provider_def_t *prov, const char *api_key,
                    const char *model, const char *system_prompt,
                    const char *final_q, char *out, size_t out_cap, int use_ctx) {
    char body[24576];

    if (prov->format == FMT_ANTHROPIC) {
        /* system als Cache-faehiger Block (Prompt-Caching spart Kosten/Latenz) */
        snprintf(body, sizeof(body),
                 "{\"model\":\"%s\",\"max_tokens\":600,"
                 "\"system\":[{\"type\":\"text\",\"text\":\"", model);
        json_escape_append(body, sizeof(body), system_prompt);
        strncat(body, "\",\"cache_control\":{\"type\":\"ephemeral\"}}],"
                      "\"messages\":[", sizeof(body) - strlen(body) - 1);

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
    } else {
        /* OpenAI-kompatibel: system als erste Nachricht */
        snprintf(body, sizeof(body),
                 "{\"model\":\"%s\",\"max_tokens\":600,\"messages\":["
                 "{\"role\":\"system\",\"content\":\"", model);
        json_escape_append(body, sizeof(body), system_prompt);
        strncat(body, "\"},", sizeof(body) - strlen(body) - 1);

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
    }

    char respbuf[16384];
    struct curl_slist *headers = NULL;
    char auth_header[600];
    if (prov->format == FMT_ANTHROPIC) {
        snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
        headers = curl_slist_append(headers, auth_header);
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        headers = curl_slist_append(headers, "anthropic-beta: prompt-caching-2024-07-31");
    } else {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers = curl_slist_append(headers, auth_header);
    }
    headers = curl_slist_append(headers, "content-type: application/json");

    /* Bis zu 2 Versuche bei Rate-Limit (HTTP 429), z.B. NVIDIA NIM. */
    int ok = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        CURL *curl = curl_easy_init();
        if (!curl) {
            snprintf(out, out_cap, "Interner Fehler: curl_easy_init() fehlgeschlagen.");
            curl_slist_free_all(headers);
            return 0;
        }
        respbuf[0] = '\0';
        struct membuf mb = { .data = respbuf, .len = 0, .cap = sizeof(respbuf) };

        curl_easy_setopt(curl, CURLOPT_URL, prov->url);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mb);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl);
        long http = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            snprintf(out, out_cap, "Netzwerkfehler (%s): %s",
                     prov->label, curl_easy_strerror(res));
            LOGE("provider %s: Netzwerkfehler: %s", prov->label, curl_easy_strerror(res));
            break;
        }
        if (http == 429 && attempt < 2) {
            LOGW("provider %s: Rate-Limit (HTTP 429), Versuch %d", prov->label, attempt + 1);
            sleep(1 + attempt); /* einfacher Backoff bei Rate-Limit */
            continue;
        }
        if (http < 200 || http >= 300)
            LOGE("provider %s: HTTP %ld", prov->label, http);
        if (extract_text(respbuf, prov->format, out, out_cap)) {
            ok = 1;
        } else if (out[0] == '\0') {
            snprintf(out, out_cap, "Antwort konnte nicht gelesen werden (%s).",
                     prov->label);
        }
        break;
    }

    curl_slist_free_all(headers);
    return ok;
}

/* Prueft ob die KI-Antwort einen Tool-Aufruf enthaelt und parst ihn.
 * Erwartet eine Zeile "TOOL:<name>" gefolgt von "ARG:<arg>" -- die Zeile
 * darf auch NACH etwas Vortext stehen (manche Modelle schreiben z.B.
 * "Gespeichert.\n\nTOOL:..."). */
static int parse_tool_call(const char *response,
                            char *tool_name, size_t name_cap,
                            char *tool_arg,  size_t arg_cap) {
    /* "TOOL:" am Anfang oder an einem Zeilenanfang finden */
    const char *p = NULL;
    if (strncmp(response, "TOOL:", 5) == 0) {
        p = response + 5;
    } else {
        const char *nl = strstr(response, "\nTOOL:");
        if (!nl) return 0;
        p = nl + 6;
    }

    const char *nl = strchr(p, '\n');
    if (!nl) return 0;
    size_t nlen = (size_t)(nl - p);
    while (nlen > 0 && (p[nlen-1] == '\r' || p[nlen-1] == ' ')) nlen--;
    if (nlen >= name_cap) nlen = name_cap - 1;
    memcpy(tool_name, p, nlen);
    tool_name[nlen] = '\0';

    p = nl + 1;
    /* eventuelle Leerzeilen vor ARG: ueberspringen */
    while (*p == '\n' || *p == '\r') p++;
    if (strncmp(p, "ARG:", 4) != 0) {
        tool_arg[0] = '\0';
        return 1;
    }
    p += 4;
    /* ARG geht bis Zeilenende (Tool-Argumente sind einzeilig) */
    const char *aend = strchr(p, '\n');
    size_t alen = aend ? (size_t)(aend - p) : strlen(p);
    while (alen > 0 && (p[alen-1] == '\n' || p[alen-1] == '\r')) alen--;
    if (alen >= arg_cap) alen = arg_cap - 1;
    memcpy(tool_arg, p, alen);
    tool_arg[alen] = '\0';
    return 1;
}

#define FLUX_SYSTEM_PROMPT_BASE \
    "Du bist der KI-Assistent des Telefon-Betriebssystems Flux. " \
    "Antworte kurz und klar auf Deutsch. " \
    "WICHTIG -- Wenn der Nutzer dir persoenliche Informationen nennt " \
    "(Namen, Beziehungen, Geburtstage, Praeferenzen, Preise, wichtige Fakten), " \
    "speichere diese SOFORT mit dem memory_save-Tool, bevor du antwortest. " \
    "Bestaetigung: 'Notiert.' oder 'Gespeichert.' genuegt. " \
    "Du kannst MEHRERE Tools nacheinander aufrufen -- eines pro Antwort. " \
    "Enthaelt eine Anfrage mehrere Aufgaben (z.B. Wecker stellen UND eine Mail " \
    "schreiben), erledige JEDE Teilaufgabe. Fuehre eindeutige Aufgaben wie " \
    "Wecker/Erinnerungen/Notizen SOFORT aus, ohne nachzufragen. Frage hoechstens " \
    "zu EINER Teilaufgabe nach und erledige die anderen trotzdem. " \
    "Sollst du eine Mail/SMS an eine Person senden, deren Adresse/Nummer du " \
    "nicht kennst, rufe ZUERST contacts_search mit dem Namen auf und nutze " \
    "das Ergebnis, statt nachzufragen. " \
    "WENN der Nutzer eindeutig eine E-Mail senden, eine SMS senden oder " \
    "einen Anruf taetigen moechte UND du Empfaenger und Inhalt sicher " \
    "ableiten kannst, antworte AUSSCHLIESSLICH in diesem Format:\n" \
    "ACTION:<mail|sms|call>\n" \
    "TO:<Empfaenger>\n" \
    "SUBJECT:<Betreff, nur bei mail>\n" \
    "BODY:\n" \
    "<Text>\n" \
    "Falls Empfaenger oder Inhalt wirklich unklar sind, frage nach. " \
    "WENN der Nutzer den Flugmodus ein- oder ausschalten moechte " \
    "('aktivier Flugmodus', 'Funk aus', 'Flugmodus aus'), antworte " \
    "AUSSCHLIESSLICH in diesem Format (das Geraet zeigt vor dem Schalten " \
    "einen Bestaetigungs-Dialog -- du schaltest nie direkt):\n" \
    "ACTION:flight\n" \
    "STATE:<an|aus>\n" \
    "Nur den AKTUELLEN Flugmodus-Status abfragen ('ist Flugmodus an?') " \
    "geht ohne Bestaetigung ueber das flight_mode-Tool. "

static void build_system_prompt(char *system_prompt, size_t cap) {
    time_t _t = time(NULL); struct tm _tm; localtime_r(&_t, &_tm);
    char _dt[64]; strftime(_dt, sizeof(_dt), "%A, %d. %B %Y, %H:%M Uhr", &_tm);

    char _prefs[512] = {0};
    FILE *_pf = fopen("/etc/flux/prefs.txt", "r");
    if (_pf) { size_t _n = fread(_prefs, 1, sizeof(_prefs)-1, _pf); _prefs[_n] = '\0'; fclose(_pf); }

    char _mem[2048] = {0};
    FILE *_mf = fopen("/etc/flux/memory.txt", "r");
    if (_mf) { size_t _n = fread(_mem, 1, sizeof(_mem)-1, _mf); _mem[_n] = '\0'; fclose(_mf); }

    snprintf(system_prompt, cap, "%s\n\nAktuelles Datum/Uhrzeit: %s\n",
             FLUX_SYSTEM_PROMPT_BASE, _dt);
    if (_mem[0]) {
        size_t l = strlen(system_prompt);
        snprintf(system_prompt + l, cap - l,
                 "\nKI-Gedaechtnis (persoenliche Infos des Nutzers -- immer beachten):\n%s\n", _mem);
    }
    if (_prefs[0]) {
        size_t l = strlen(system_prompt);
        snprintf(system_prompt + l, cap - l,
                 "\nNutzerpraeferenzen (beachten):\n%s\n", _prefs);
    }
    {
        size_t l = strlen(system_prompt);
        snprintf(system_prompt + l, cap - l, "\n%s", flux_tools_description());
    }
}

void flux_provider_ask(const char *question, char *out, size_t out_cap) {
    char api_key[512] = {0};
    char model[200]   = {0};
    const flux_provider_def_t *prov =
        resolve_provider(api_key, sizeof(api_key), model, sizeof(model));

    if (!api_key[0]) {
        snprintf(out, out_cap,
            "Kein Cloud-Zugang fuer %s konfiguriert. Trage in den Einstellungen "
            "einen API-Key fuer den gewaehlten Anbieter ein (oder waehle einen "
            "anderen Anbieter).", prov->label);
        return;
    }

    char system_prompt[8192];
    build_system_prompt(system_prompt, sizeof(system_prompt));

    /* Erster API-Aufruf -- mit Gespraechsverlauf */
    if (!api_call(prov, api_key, model, system_prompt, question, out, out_cap, 1))
        return;

    /* Agenten-Schleife: solange die Antwort ein Tool-Aufruf ist, das Tool
     * ausfuehren, das Ergebnis anhaengen und erneut fragen. So koennen
     * mehrere Tools nacheinander laufen (z.B. Kontakt suchen -> Mail) und
     * am Ende eine normale Antwort ODER ein ACTION:-Vorschlag stehen. */
    char log[6144] = {0};   /* laufendes Protokoll der Tool-Schritte */
    for (int step = 0; step < FLUX_MAX_TOOL_STEPS; step++) {
        char tool_name[64], tool_arg[1024];
        if (!parse_tool_call(out, tool_name, sizeof(tool_name),
                                  tool_arg, sizeof(tool_arg)))
            break; /* normale Antwort oder ACTION: -- fertig */

        char tool_result[4096];
        snprintf(tool_result, sizeof(tool_result),
                 "Fehler: unbekanntes Tool '%s'", tool_name);
        flux_tool_exec(tool_name, tool_arg, tool_result, sizeof(tool_result));

        size_t ll = strlen(log);
        snprintf(log + ll, sizeof(log) - ll,
                 "- %s(%s) => %.400s\n", tool_name, tool_arg, tool_result);

        char followup[8192];
        snprintf(followup, sizeof(followup),
                 "Urspruengliche Anfrage: \"%s\"\n\n"
                 "Bereits ausgefuehrte Schritte (Tool => Ergebnis):\n%s\n"
                 "Wenn fuer die Anfrage noch ein weiterer Schritt noetig ist, "
                 "rufe das naechste Tool auf (NUR im Format TOOL:/ARG:). "
                 "Wenn eine Mail/SMS/ein Anruf zu bestaetigen ist, antworte im "
                 "ACTION:-Format. Sonst antworte final auf Deutsch, kurz und klar "
                 "und fasse zusammen, was erledigt wurde.",
                 question, log);

        if (!api_call(prov, api_key, model, system_prompt, followup, out, out_cap, 0))
            return;
    }

    ctx_add(question, out);
}
