#include "email.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct upload_state {
    const char *data;
    size_t left;
};

static size_t read_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct upload_state *st = userdata;
    size_t room = size * nmemb;
    size_t n = st->left < room ? st->left : room;
    if (n == 0) return 0;
    memcpy(ptr, st->data, n);
    st->data += n;
    st->left -= n;
    return n;
}

int flux_email_send(const char *to, const char *subject, const char *body,
                     char *out, size_t out_cap) {
    const char *smtp_url  = getenv("FLUX_SMTP_URL");
    const char *user      = getenv("FLUX_SMTP_USER");
    const char *pass      = getenv("FLUX_SMTP_PASS");
    const char *from       = getenv("FLUX_SMTP_FROM");

    if (!smtp_url || !*smtp_url || !from || !*from) {
        snprintf(out, out_cap,
            "Kein Mail-Zugang konfiguriert. Setze FLUX_SMTP_URL, FLUX_SMTP_FROM "
            "(und meist FLUX_SMTP_USER/FLUX_SMTP_PASS), um E-Mails zu senden.");
        return 0;
    }
    if (!to || !*to || strchr(to, '\n')) {
        snprintf(out, out_cap, "Ungueltiger Empfaenger.");
        return 0;
    }

    char from_addr[256], to_addr[256];
    snprintf(from_addr, sizeof(from_addr), "<%s>", from);
    snprintf(to_addr, sizeof(to_addr), "<%s>", to);

    char date_buf[64];
    time_t t = time(NULL);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S +0000", &tmv);

    char message[8192];
    snprintf(message, sizeof(message),
             "Date: %s\r\n"
             "To: %s\r\n"
             "From: %s\r\n"
             "Subject: %s\r\n"
             "\r\n"
             "%s\r\n",
             date_buf, to, from, subject ? subject : "", body ? body : "");

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(out, out_cap, "Interner Fehler: curl_easy_init() fehlgeschlagen.");
        return 0;
    }

    struct curl_slist *rcpt = curl_slist_append(NULL, to_addr);
    struct upload_state st = { .data = message, .left = strlen(message) };

    curl_easy_setopt(curl, CURLOPT_URL, smtp_url);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from_addr);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpt);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    if (user && *user) curl_easy_setopt(curl, CURLOPT_USERNAME, user);
    if (pass && *pass) curl_easy_setopt(curl, CURLOPT_PASSWORD, pass);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_cb);
    curl_easy_setopt(curl, CURLOPT_READDATA, &st);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    CURLcode res = curl_easy_perform(curl);
    int ok = (res == CURLE_OK);
    if (ok) {
        snprintf(out, out_cap, "Mail an %s gesendet.", to);
    } else {
        snprintf(out, out_cap, "Mail-Versand fehlgeschlagen: %s", curl_easy_strerror(res));
    }

    curl_slist_free_all(rcpt);
    curl_easy_cleanup(curl);
    return ok;
}
