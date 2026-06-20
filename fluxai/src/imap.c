#include "imap.h"
#include "../../common/flux_config.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define IMAP_MAX_UNREAD 8

/* ---- Curl-Hilfspuffer ------------------------------------------------- */

struct membuf { char *data; size_t len, cap; };

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    struct membuf *mb = ud;
    size_t add = size * nmemb;
    if (mb->len + add + 1 > mb->cap) add = mb->cap - mb->len - 1;
    if (add == 0) return size * nmemb; /* Puffer voll: verwerfen, nicht abbrechen */
    memcpy(mb->data + mb->len, ptr, add);
    mb->len += add;
    mb->data[mb->len] = '\0';
    return size * nmemb;
}

/* ---- Konfiguration --------------------------------------------------- */

/* Liest IMAP-Zugangsdaten. Gibt 1 zurueck, wenn Host+Benutzer+Passwort da
 * sind. user/pass fallen auf smtp_* zurueck. */
static int imap_config(char *host, size_t hcap, char *port, size_t pcap,
                       char *user, size_t ucap, char *pass, size_t pacap) {
    if (!flux_config_get("imap_host", host, hcap) || !host[0]) return 0;
    if (!flux_config_get("imap_port", port, pcap) || !port[0])
        snprintf(port, pcap, "993");
    if (!flux_config_get("imap_user", user, ucap) || !user[0])
        flux_config_get("smtp_user", user, ucap);
    if (!flux_config_get("imap_pass", pass, pacap) || !pass[0])
        flux_config_get("smtp_pass", pass, pacap);
    return user[0] && pass[0];
}

/* Fuehrt einen IMAP-Befehl gegen INBOX aus. custom_req == NULL => die per
 * url_suffix adressierte Nachricht wird komplett geladen. Gibt 1 bei Erfolg. */
static int imap_perform(const char *host, const char *port,
                        const char *user, const char *pass,
                        const char *url_suffix, const char *custom_req,
                        char *out, size_t out_cap) {
    CURL *curl = curl_easy_init();
    if (!curl) { snprintf(out, out_cap, "Interner Fehler (curl)."); return 0; }

    char url[512];
    snprintf(url, sizeof(url), "imaps://%s:%s/INBOX%s",
             host, port, url_suffix ? url_suffix : "");

    struct membuf mb = { .data = out, .len = 0, .cap = out_cap };
    out[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERNAME, user);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, pass);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    if (custom_req) curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, custom_req);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        snprintf(out, out_cap, "IMAP-Fehler: %s", curl_easy_strerror(res));
        return 0;
    }
    return 1;
}

/* ---- Parser (testbar) ------------------------------------------------- */

int flux_imap_parse_search(const char *resp, unsigned long *ids, int max_ids) {
    if (!resp) return 0;
    const char *p = strstr(resp, "SEARCH");
    if (!p) return 0;
    p += 6; /* hinter "SEARCH" */
    int n = 0;
    while (*p && n < max_ids) {
        while (*p && !isdigit((unsigned char)*p)) {
            if (*p == '\r' || *p == '\n') break; /* nur die SEARCH-Zeile */
            p++;
        }
        if (!isdigit((unsigned char)*p)) break;
        char *end;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p) break;
        ids[n++] = v;
        p = end;
    }
    return n;
}

/* Haengt den Wert eines Headers (z.B. "From:") als "Label: wert" an out. */
static void append_header(const char *resp, const char *field,
                          const char *label, char *out, size_t out_cap) {
    /* zeilenweise suchen, Gross-/Kleinschreibung des Feldnamens ignorieren */
    const char *p = resp;
    size_t flen = strlen(field);
    while (*p) {
        const char *line = p;
        const char *eol = strpbrk(p, "\r\n");
        size_t llen = eol ? (size_t)(eol - line) : strlen(line);
        if (llen > flen && strncasecmp(line, field, flen) == 0) {
            const char *v = line + flen;
            while (*v == ' ' || *v == '\t') v++;
            size_t vlen = (size_t)((line + llen) - v);
            size_t ol = strlen(out);
            snprintf(out + ol, out_cap - ol, "  %s %.*s\n", label, (int)vlen, v);
            return;
        }
        if (!eol) break;
        p = eol + 1;
    }
}

void flux_imap_extract_headers(unsigned long uid, const char *fetch_resp,
                               char *out, size_t out_cap) {
    size_t ol = strlen(out);
    snprintf(out + ol, out_cap - ol, "Mail UID %lu:\n", uid);
    append_header(fetch_resp, "From:",    "Von:    ", out, out_cap);
    append_header(fetch_resp, "Subject:", "Betreff:", out, out_cap);
    append_header(fetch_resp, "Date:",    "Datum:  ", out, out_cap);
}

/* ---- Public ----------------------------------------------------------- */

void flux_imap_fetch_unread(char *out, size_t out_cap) {
    char host[128], port[16], user[128], pass[160];
    if (!imap_config(host, sizeof(host), port, sizeof(port),
                     user, sizeof(user), pass, sizeof(pass))) {
        snprintf(out, out_cap,
                 "Kein IMAP-Konto eingerichtet. Trage in /etc/flux/flux.conf "
                 "imap_host (und ggf. imap_user/imap_pass) ein -- Benutzer und "
                 "Passwort werden sonst von den SMTP-Einstellungen uebernommen.");
        return;
    }

    /* 1. Ungelesene UIDs suchen */
    char buf[8192];
    if (!imap_perform(host, port, user, pass, "", "UID SEARCH UNSEEN",
                      buf, sizeof(buf))) {
        snprintf(out, out_cap, "%s", buf);
        return;
    }
    unsigned long ids[64];
    int n = flux_imap_parse_search(buf, ids, 64);
    if (n == 0) {
        snprintf(out, out_cap, "Keine ungelesenen Mails.");
        return;
    }

    /* 2. Die juengsten bis zu IMAP_MAX_UNREAD Kopfzeilen holen */
    int start = n > IMAP_MAX_UNREAD ? n - IMAP_MAX_UNREAD : 0;
    int shown = n - start;
    out[0] = '\0';
    size_t ol = (size_t)snprintf(out, out_cap,
        "%d ungelesene Mail(s)%s -- juengste %d:\n", n,
        n > shown ? " (Auszug)" : "", shown);
    (void)ol;

    for (int i = start; i < n; i++) {
        char req[128];
        snprintf(req, sizeof(req),
                 "UID FETCH %lu (BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])",
                 ids[i]);
        if (imap_perform(host, port, user, pass, "", req, buf, sizeof(buf)))
            flux_imap_extract_headers(ids[i], buf, out, out_cap);
    }
}

void flux_imap_read(const char *uid, char *out, size_t out_cap) {
    if (!uid || !*uid) { snprintf(out, out_cap, "Fehler: keine UID angegeben."); return; }
    char host[128], port[16], user[128], pass[160];
    if (!imap_config(host, sizeof(host), port, sizeof(port),
                     user, sizeof(user), pass, sizeof(pass))) {
        snprintf(out, out_cap, "Kein IMAP-Konto eingerichtet (siehe mail_unread).");
        return;
    }
    /* BODY.PEEK[TEXT] laedt den Text, ohne \Seen zu setzen */
    char req[96];
    snprintf(req, sizeof(req), "UID FETCH %s BODY.PEEK[TEXT]", uid);
    char buf[16384];
    if (!imap_perform(host, port, user, pass, "", req, buf, sizeof(buf))) {
        snprintf(out, out_cap, "%s", buf);
        return;
    }
    /* Erste IMAP-Statuszeile "* N FETCH (...{len}" abschneiden, dann Text */
    const char *body = strchr(buf, '\n');
    body = body ? body + 1 : buf;
    snprintf(out, out_cap, "%.*s", (int)(out_cap - 1), body);
}
