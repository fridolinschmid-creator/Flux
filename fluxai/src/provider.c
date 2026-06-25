#include "provider.h"
#include "tools.h"
#include "../../common/flux_config.h"

#include <curl/curl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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

int flux_provider_vision(char *key_out, size_t key_cap,
                         char *model_out, size_t model_cap,
                         char *label_out, size_t label_cap) {
    char key[512]   = {0};
    char model[200] = {0};
    const flux_provider_def_t *p =
        resolve_provider(key, sizeof(key), model, sizeof(model));

    if (label_out && label_cap) snprintf(label_out, label_cap, "%s", p->label);

    /* Nur Anthropic unterstuetzt das von vision.c gebaute Image-Format. */
    if (p->format != FMT_ANTHROPIC || key[0] == '\0')
        return 0;

    if (key_out && key_cap)     snprintf(key_out, key_cap, "%s", key);
    if (model_out && model_cap) snprintf(model_out, model_cap, "%s", model);
    return 1;
}

/* --- Conversation context (last CTX_MAX turns) --- */
#define CTX_MAX 12
#define CTX_LOG "/etc/flux/conv_log.txt"
typedef struct { char q[256]; char a[512]; } ctx_turn_t;
static ctx_turn_t ctx_history[CTX_MAX];
static int        ctx_n = 0;

/* Schreibt ctx_history persistent nach CTX_LOG. */
static void ctx_persist(void) {
    mkdir("/etc/flux", 0755);
    FILE *f = fopen(CTX_LOG, "w");
    if (!f) return;
    for (int i = 0; i < ctx_n; i++)
        fprintf(f, "Q:%s\nA:%s\n---\n", ctx_history[i].q, ctx_history[i].a);
    fclose(f);
}

/* Laedt ctx_history beim Start aus CTX_LOG. */
static void ctx_load(void) {
    FILE *f = fopen(CTX_LOG, "r");
    if (!f) return;
    ctx_n = 0;
    ctx_turn_t *cur = NULL;
    char line[768];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (strncmp(line, "Q:", 2) == 0 && ctx_n < CTX_MAX) {
            cur = &ctx_history[ctx_n++];
            snprintf(cur->q, sizeof(cur->q), "%s", line + 2);
            cur->a[0] = '\0';
        } else if (strncmp(line, "A:", 2) == 0 && cur) {
            snprintf(cur->a, sizeof(cur->a), "%s", line + 2);
        } else if (strcmp(line, "---") == 0) {
            cur = NULL;
        }
    }
    fclose(f);
}

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
    ctx_persist();
}

struct membuf {
    char  *data;
    size_t len;
    size_t cap;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct membuf *mb = userdata;
    size_t add = size * nmemb;
    if (mb->len + add + 1 > mb->cap) {
        size_t nc = mb->cap ? mb->cap * 2 : 16384;
        while (nc < mb->len + add + 1) nc *= 2;
        char *nd = realloc(mb->data, nc);
        if (!nd) return 0; /* OOM -> curl bricht ab */
        mb->data = nd;
        mb->cap  = nc;
    }
    memcpy(mb->data + mb->len, ptr, add);
    mb->len += add;
    mb->data[mb->len] = '\0';
    return add;
}

void flux_provider_init(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    ctx_load();
}

/* --- Wachsender String-Builder fuer den JSON-Request-Body ---
 * Ersetzt das fruehere feste body[24576] mit strncat: dort wurde der
 * Body bei vielen Kontext-Turns + grossem System-Prompt still abgeschnitten
 * -> ungueltiges JSON an die API. Hier waechst der Puffer stattdessen und
 * setzt bei OOM ein Fehler-Flag, das der Aufrufer prueft. */
typedef struct { char *buf; size_t len, cap; int err; } strbuf_t;

static void sb_init(strbuf_t *b) {
    b->cap = 8192; b->len = 0; b->err = 0;
    b->buf = malloc(b->cap);
    if (!b->buf) b->err = 1; else b->buf[0] = '\0';
}
static void sb_ensure(strbuf_t *b, size_t extra) {
    if (b->err) return;
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap * 2;
        while (nc < b->len + extra + 1) nc *= 2;
        char *nb = realloc(b->buf, nc);
        if (!nb) { b->err = 1; return; }
        b->buf = nb; b->cap = nc;
    }
}
/* Rohtext (bereits JSON-sicher) anhaengen. */
static void sb_raw(strbuf_t *b, const char *s) {
    size_t l = strlen(s);
    sb_ensure(b, l);
    if (b->err) return;
    memcpy(b->buf + b->len, s, l);
    b->len += l;
    b->buf[b->len] = '\0';
}
/* String JSON-escaped anhaengen. */
static void sb_json(strbuf_t *b, const char *s) {
    for (; *s; s++) {
        char c = *s;
        if ((unsigned char)c < 0x20 && c != '\n') continue;
        sb_ensure(b, 2);
        if (b->err) return;
        if (c == '"' || c == '\\') { b->buf[b->len++] = '\\'; b->buf[b->len++] = c; }
        else if (c == '\n')        { b->buf[b->len++] = '\\'; b->buf[b->len++] = 'n'; }
        else                       { b->buf[b->len++] = c; }
        b->buf[b->len] = '\0';
    }
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
                    const char *model, const char *sys_static,
                    const char *sys_dynamic, const char *final_q,
                    char *out, size_t out_cap, int use_ctx) {
    strbuf_t b;
    sb_init(&b);

    if (prov->format == FMT_ANTHROPIC) {
        /* Statischer Teil als cache-faehiger Block (Prompt-Caching), der
         * dynamische Teil (Datum/Gedaechtnis) als eigener, NICHT gecachter
         * Block -- sonst bricht der Cache jede Minute durch die Uhrzeit. */
        sb_raw(&b, "{\"model\":\"");
        sb_json(&b, model);
        sb_raw(&b, "\",\"max_tokens\":600,\"system\":["
                   "{\"type\":\"text\",\"text\":\"");
        sb_json(&b, sys_static);
        sb_raw(&b, "\",\"cache_control\":{\"type\":\"ephemeral\"}}");
        if (sys_dynamic && *sys_dynamic) {
            sb_raw(&b, ",{\"type\":\"text\",\"text\":\"");
            sb_json(&b, sys_dynamic);
            sb_raw(&b, "\"}");
        }
        sb_raw(&b, "],\"messages\":[");
    } else {
        /* OpenAI-kompatibel: system als erste Nachricht (kein Caching) */
        sb_raw(&b, "{\"model\":\"");
        sb_json(&b, model);
        sb_raw(&b, "\",\"max_tokens\":600,\"messages\":["
                   "{\"role\":\"system\",\"content\":\"");
        sb_json(&b, sys_static);
        if (sys_dynamic && *sys_dynamic) { sb_raw(&b, "\\n"); sb_json(&b, sys_dynamic); }
        sb_raw(&b, "\"},");
    }

    /* Gespraechsverlauf + aktuelle Frage (fuer beide Formate gleich) */
    if (use_ctx) {
        for (int i = 0; i < ctx_n; i++) {
            sb_raw(&b, "{\"role\":\"user\",\"content\":\"");
            sb_json(&b, ctx_history[i].q);
            sb_raw(&b, "\"},{\"role\":\"assistant\",\"content\":\"");
            sb_json(&b, ctx_history[i].a);
            sb_raw(&b, "\"},");
        }
    }
    sb_raw(&b, "{\"role\":\"user\",\"content\":\"");
    sb_json(&b, final_q);
    sb_raw(&b, "\"}]}");

    if (b.err) {
        free(b.buf);
        snprintf(out, out_cap, "Interner Fehler: Anfrage konnte nicht aufgebaut werden.");
        return 0;
    }
    const char *body = b.buf;

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
            break;
        }
        /* Antwortpuffer waechst bei Bedarf (curl_write_cb) -- frueher fixe
         * 16 KB, was bei langen Antworten zu einem falschen "Netzwerkfehler"
         * fuehrte. */
        struct membuf mb = { .data = malloc(16384), .len = 0, .cap = 16384 };
        if (!mb.data) { curl_easy_cleanup(curl); break; }
        mb.data[0] = '\0';

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
            free(mb.data);
            break;
        }
        if (http == 429 && attempt < 2) {
            free(mb.data);
            sleep(1 + attempt); /* einfacher Backoff bei Rate-Limit */
            continue;
        }
        if (extract_text(mb.data, prov->format, out, out_cap)) {
            ok = 1;
        } else if (out[0] == '\0') {
            snprintf(out, out_cap, "Antwort konnte nicht gelesen werden (%s).",
                     prov->label);
        }
        free(mb.data);
        break;
    }

    curl_slist_free_all(headers);
    free(b.buf);
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
    "Falls Empfaenger oder Inhalt wirklich unklar sind, frage nach. "

/* Liest die letzten (cap-1) Bytes einer Datei nach out. Bei append-basierten
 * Dateien wie memory.txt sind das die NEUESTEN Eintraege -- der frueher
 * genutzte fread-vom-Anfang las stattdessen die aeltesten und liess neue
 * Erinnerungen, sobald die Datei > 2 KB war, gar nicht erst beim Modell
 * ankommen. */
static void read_file_tail(const char *path, char *out, size_t cap) {
    out[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f) return;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return; }
    long want = (long)cap - 1;
    long off = (sz > want) ? sz - want : 0;
    fseek(f, off, SEEK_SET);
    size_t n = fread(out, 1, cap - 1, f);
    out[n] = '\0';
    fclose(f);
    /* Wurde mitten in einer Zeile abgeschnitten, bis zum naechsten \n
     * vorruecken, damit kein halber Eintrag im Prompt landet. */
    if (off > 0) {
        char *nl = strchr(out, '\n');
        if (nl && nl[1]) memmove(out, nl + 1, strlen(nl + 1) + 1);
    }
}

/* Statischer, cache-faehiger Teil: Basis-Anweisung + Tool-Beschreibungen.
 * Aendert sich zwischen Anfragen nicht -> Prompt-Cache greift. */
static void build_static_prompt(char *out, size_t cap) {
    snprintf(out, cap, "%s\n\n%s", FLUX_SYSTEM_PROMPT_BASE, flux_tools_description());
}

/* Dynamischer Teil: Datum/Uhrzeit + Gedaechtnis + Praeferenzen. Wird bewusst
 * NICHT gecacht, weil er sich (Uhrzeit!) staendig aendert. */
static void build_dynamic_prompt(char *out, size_t cap) {
    time_t _t = time(NULL); struct tm _tm; localtime_r(&_t, &_tm);
    char _dt[64]; strftime(_dt, sizeof(_dt), "%A, %d. %B %Y, %H:%M Uhr", &_tm);

    char _mem[2048] = {0};
    read_file_tail("/etc/flux/memory.txt", _mem, sizeof(_mem));
    char _prefs[512] = {0};
    read_file_tail("/etc/flux/prefs.txt", _prefs, sizeof(_prefs));

    snprintf(out, cap, "Aktuelles Datum/Uhrzeit: %s\n", _dt);
    if (_mem[0]) {
        size_t l = strlen(out);
        snprintf(out + l, cap - l,
                 "\nKI-Gedaechtnis (persoenliche Infos des Nutzers -- immer beachten):\n%s\n", _mem);
    }
    if (_prefs[0]) {
        size_t l = strlen(out);
        snprintf(out + l, cap - l,
                 "\nNutzerpraeferenzen (beachten):\n%s\n", _prefs);
    }
}

void flux_provider_ask(const char *question, char *out, size_t out_cap) {
    /* Kontext frisch aus der Datei laden: fluxaid bearbeitet jede Anfrage in
     * einem eigenen Kindprozess (siehe main.c). Der in-memory ctx_history des
     * Elternprozesses bleibt sonst auf dem Stand vom Start stehen -- Neuladen
     * stellt die Gespraechs-Historie ueber Prozessgrenzen hinweg sicher. */
    ctx_load();

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

    char sys_static[8192];
    char sys_dynamic[4096];
    build_static_prompt(sys_static, sizeof(sys_static));
    build_dynamic_prompt(sys_dynamic, sizeof(sys_dynamic));

    /* Erster API-Aufruf -- mit Gespraechsverlauf */
    if (!api_call(prov, api_key, model, sys_static, sys_dynamic, question, out, out_cap, 1))
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

        if (!api_call(prov, api_key, model, sys_static, sys_dynamic, followup, out, out_cap, 0))
            return;
    }

    ctx_add(question, out);
}
