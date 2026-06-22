#include "logsync.h"
#include "../../common/flux_config.h"
#include "../../common/flux_log.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#define LOG_PATH        "/var/log/flux/flux.log"
#define MAX_UPLOAD_BYTES (512 * 1024)   /* nur die letzten 512 KB hochladen */

/* Antwort verwerfen, aber Groesse abnehmen, damit curl zufrieden ist. */
static size_t discard_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    (void)ptr; (void)ud;
    return size * nmemb;
}

/* Liefert die konfigurierte Backend-URL ohne abschliessenden '/'.
 * Gibt 1 zurueck, wenn gesetzt, sonst 0. */
static int backend_base(char *out, size_t cap) {
    char url[512] = {0};
    if (!flux_config_get("log_backend_url", url, sizeof(url)) || !url[0])
        return 0;
    size_t n = strlen(url);
    while (n > 0 && (url[n-1] == '/' || url[n-1] == ' ')) url[--n] = '\0';
    if (!url[0]) return 0;
    snprintf(out, cap, "%s", url);
    return 1;
}

static void device_id(char *out, size_t cap) {
    char host[128] = {0};
    if (gethostname(host, sizeof(host) - 1) != 0 || !host[0])
        snprintf(host, sizeof(host), "flux");
    /* Optionaler Override per Config (z.B. mehrere Geraete unterscheiden). */
    char cfg[128] = {0};
    if (flux_config_get("device_id", cfg, sizeof(cfg)) && cfg[0])
        snprintf(out, cap, "%s", cfg);
    else
        snprintf(out, cap, "%s", host);
}

/* Gemeinsamer POST. path z.B. "/ingest". data/len = Rumpf. content_type
 * z.B. "text/plain". Schreibt bei Bedarf eine Fehlerursache nach err. */
static int do_post(const char *path, const char *data, size_t len,
                   const char *content_type, char *err, size_t err_cap) {
    char base[512];
    if (!backend_base(base, sizeof(base))) {
        if (err) snprintf(err, err_cap, "kein Backend konfiguriert");
        return 0;
    }
    char url[600];
    snprintf(url, sizeof(url), "%s%s", base, path);

    char dev[160];
    device_id(dev, sizeof(dev));
    char dev_header[256];
    snprintf(dev_header, sizeof(dev_header), "X-Flux-Device: %s", dev);
    char ct_header[128];
    snprintf(ct_header, sizeof(ct_header), "Content-Type: %s", content_type);

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (err) snprintf(err, err_cap, "curl_easy_init fehlgeschlagen");
        return 0;
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, dev_header);
    headers = curl_slist_append(headers, ct_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)len);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_cb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);

    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (err) snprintf(err, err_cap, "%s", curl_easy_strerror(res));
        return 0;
    }
    if (http < 200 || http >= 300) {
        if (err) snprintf(err, err_cap, "HTTP %ld", http);
        return 0;
    }
    return 1;
}

int flux_logsync_upload(char *out, size_t out_cap) {
    char base[512];
    if (!backend_base(base, sizeof(base))) {
        snprintf(out, out_cap,
            "Kein Fehler-Backend konfiguriert. Trage in den Einstellungen "
            "unter \"Fehler-Backend (URL)\" eine Adresse ein (z.B. "
            "http://macbook.local:8899), dann koennen die Logs gesendet werden.");
        return 0;
    }

    FILE *f = fopen(LOG_PATH, "rb");
    if (!f) {
        snprintf(out, out_cap,
            "Logdatei %s nicht lesbar -- es wurde noch nichts protokolliert "
            "oder die Rechte fehlen.", LOG_PATH);
        LOGW("logsync: %s nicht lesbar", LOG_PATH);
        return 0;
    }

    /* Nur die letzten MAX_UPLOAD_BYTES senden (bei grossen Logs). */
    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    long start = (total > MAX_UPLOAD_BYTES) ? (total - MAX_UPLOAD_BYTES) : 0;
    if (start < 0) start = 0;
    fseek(f, start, SEEK_SET);

    size_t want = (size_t)(total - start);
    char *buf = malloc(want + 1);
    if (!buf) {
        fclose(f);
        snprintf(out, out_cap, "Speicher fuer Log-Upload nicht verfuegbar.");
        LOGE("logsync: malloc(%zu) fehlgeschlagen", want + 1);
        return 0;
    }
    size_t got = fread(buf, 1, want, f);
    buf[got] = '\0';
    fclose(f);

    char err[160] = {0};
    int ok = do_post("/ingest", buf, got, "text/plain; charset=utf-8", err, sizeof(err));
    free(buf);

    if (ok) {
        snprintf(out, out_cap,
            "%zu Bytes Log an %s gesendet. Im Backend (Browser) einsehbar.",
            got, base);
        LOGI("logsync: %zu Bytes an %s/ingest gesendet", got, base);
        return 1;
    }
    snprintf(out, out_cap,
        "Log-Upload an %s fehlgeschlagen: %s. Backend erreichbar und URL "
        "korrekt? (Format z.B. http://host:8899)", base, err);
    LOGE("logsync: Upload an %s/ingest fehlgeschlagen: %s", base, err);
    return 0;
}

int flux_logsync_report(const char *level, const char *module, const char *message) {
    char base[512];
    if (!backend_base(base, sizeof(base)))
        return 0; /* opt-in: ohne Backend nichts tun, kein Fehler */

    /* Einfaches, server-seitig leicht parsbares Format: "level|modul|text". */
    char body[2048];
    int n = snprintf(body, sizeof(body), "%s|%s|%s",
                     level ? level : "ERROR",
                     module ? module : "flux",
                     message ? message : "");
    if (n < 0) return 0;
    size_t len = (n < (int)sizeof(body)) ? (size_t)n : sizeof(body) - 1;

    char err[160] = {0};
    int ok = do_post("/report", body, len, "text/plain; charset=utf-8", err, sizeof(err));
    if (!ok)
        LOGW("logsync: Auto-Report an %s/report fehlgeschlagen: %s", base, err);
    return ok;
}
