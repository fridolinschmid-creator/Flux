#include "mail.h"
#include "../../common/flux_config.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct upload_state {
    const char *data;
    size_t pos;
    size_t len;
};

static size_t read_cb(char *buffer, size_t size, size_t nitems, void *userp) {
    struct upload_state *u = userp;
    size_t room = size * nitems;
    size_t remain = u->len - u->pos;
    size_t n = remain < room ? remain : room;
    if (n) {
        memcpy(buffer, u->data + u->pos, n);
        u->pos += n;
    }
    return n;
}

void flux_mail_send(const char *to, const char *subject, const char *body,
                     char *out, size_t out_cap) {
    char host[128], port[16], user[128], pass[128], from[128];
    int have_account =
        flux_config_get("smtp_host", host, sizeof(host)) && host[0] &&
        flux_config_get("smtp_user", user, sizeof(user)) && user[0] &&
        flux_config_get("smtp_pass", pass, sizeof(pass)) &&
        flux_config_get("smtp_from", from, sizeof(from)) && from[0];

    if (!have_account) {
        snprintf(out, out_cap,
            "Kein E-Mail-Konto eingerichtet. Gehe zu Einstellungen, um "
            "SMTP-Host, Postfach und Absenderadresse einzutragen.");
        return;
    }
    if (!flux_config_get("smtp_port", port, sizeof(port)) || !port[0])
        snprintf(port, sizeof(port), "587");

    if (!to[0]) {
        snprintf(out, out_cap, "Kein Empfaenger angegeben.");
        return;
    }

    char date_buf[64];
    time_t t = time(NULL);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S +0000", &tmv);

    char message[8192];
    snprintf(message, sizeof(message),
             "Date: %s\r\nTo: %s\r\nFrom: %s\r\nSubject: %s\r\n"
             "Content-Type: text/plain; charset=utf-8\r\n\r\n%s\r\n",
             date_buf, to, from, subject[0] ? subject : "(kein Betreff)", body);

    struct upload_state us = { .data = message, .pos = 0, .len = strlen(message) };

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(out, out_cap, "Interner Fehler: curl_easy_init() fehlgeschlagen.");
        return;
    }

    char url[256];
    snprintf(url, sizeof(url), "smtp://%s:%s", host, port);

    struct curl_slist *rcpt = curl_slist_append(NULL, to);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_TRY);
    curl_easy_setopt(curl, CURLOPT_USERNAME, user);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, pass);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpt);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_cb);
    curl_easy_setopt(curl, CURLOPT_READDATA, &us);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        snprintf(out, out_cap, "E-Mail-Versand fehlgeschlagen: %s", curl_easy_strerror(res));
    else
        snprintf(out, out_cap, "E-Mail an %s gesendet.", to);

    curl_slist_free_all(rcpt);
    curl_easy_cleanup(curl);
}
