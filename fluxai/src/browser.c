/* browser.c -- siehe browser.h.
 *
 * Warum kein echter grafischer Browser: FluxOS zeichnet direkt auf den
 * Framebuffer (siehe shell/src/fb.c), es gibt kein X11/Wayland und keine
 * GPU auf dem Zielsystem (QEMU aarch64 / Raspberry Pi 5 via Buildroot).
 * Ein Engine mit JavaScript/CSS-Layout (z.B. WPE WebKit) waere technisch
 * moeglich, braeuchte aber dutzende zusaetzliche Buildroot-Pakete und ist
 * ein eigenstaendiges, viel groesseres Vorhaben. Dieses Tool liefert
 * stattdessen einen schnellen, ehrlichen Text-/Lesemodus: echte Webseiten,
 * echte Navigation ueber nummerierte Links, ohne Bilder/Skripte/Layout.
 *
 * Zustand (aktuelle Seite + Linkliste) ist bewusst als static gehalten:
 * fluxaid ist ein einzelner lang laufender Prozess (siehe main.c), jede
 * Q:-Anfrage laeuft aber in ihrem eigenen kurzlebigen handle_client()-
 * Aufruf -- ein Modulzustand ist hier die einfachste Art, "aktuelle Seite"
 * ueber mehrere Zuege (open, dann click) hinweg zu merken, ohne das
 * Protokoll um eine Sitzungs-ID zu erweitern.
 */
#include "browser.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../../common/flux_util.h"

#define MAX_LINKS   25
#define HREF_CAP    512
#define LTEXT_CAP   80

typedef struct {
    char href[HREF_CAP];   /* bereits zu einer absoluten URL aufgeloest */
    char text[LTEXT_CAP];
} browser_link_t;

static char s_base_url[HREF_CAP] = {0};
static browser_link_t s_links[MAX_LINKS];
static int s_link_n = 0;

/* strcasestr ist eine GNU-Erweiterung, hier lokal nachgebaut damit
 * browser.c auch mit striktem POSIX-libc (Cross-Compile-Toolchain) baut,
 * ohne sich auf _GNU_SOURCE-Verfuegbarkeit der Ziel-libc zu verlassen. */
static const char *strcasestr_flux(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    for (; *hay; hay++)
        if (strncasecmp(hay, needle, nlen) == 0) return hay;
    return NULL;
}

/* Zerlegt eine absolute URL in "scheme://host[:port]" und das Verzeichnis
 * des Pfads (mit abschliessendem '/'). Fuer die Aufloesung relativer
 * Links. Best-effort -- kein vollstaendiger RFC-3986-Parser. */
static void split_url(const char *url, char *scheme_host, size_t sh_cap,
                      char *dir, size_t dir_cap) {
    scheme_host[0] = '\0';
    dir[0] = '\0';
    const char *p = strstr(url, "://");
    if (!p) return;
    const char *path_start = strchr(p + 3, '/');
    size_t sh_len = path_start ? (size_t)(path_start - url) : strlen(url);
    if (sh_len >= sh_cap) sh_len = sh_cap - 1;
    memcpy(scheme_host, url, sh_len);
    scheme_host[sh_len] = '\0';

    if (!path_start) { snprintf(dir, dir_cap, "/"); return; }
    const char *last_slash = strrchr(path_start, '/');
    size_t dir_len = (size_t)(last_slash - path_start) + 1;
    if (dir_len >= dir_cap) dir_len = dir_cap - 1;
    memcpy(dir, path_start, dir_len);
    dir[dir_len] = '\0';
}

/* Loest href relativ zu base_url auf. Gibt 0 zurueck (nicht navigierbar)
 * fuer javascript:/mailto:/tel:/reine Anker-Links. */
static int resolve_href(const char *base_url, const char *href,
                        char *out, size_t cap) {
    while (*href == ' ') href++;
    if (!*href || *href == '#') return 0;
    if (strncasecmp(href, "javascript:", 11) == 0) return 0;
    if (strncasecmp(href, "mailto:", 7) == 0) return 0;
    if (strncasecmp(href, "tel:", 4) == 0) return 0;

    if (strncasecmp(href, "http://", 7) == 0 || strncasecmp(href, "https://", 8) == 0) {
        snprintf(out, cap, "%s", href);
    } else if (href[0] == '/' && href[1] == '/') {
        const char *scheme_end = strstr(base_url, "://");
        size_t slen = scheme_end ? (size_t)(scheme_end - base_url) : 5;
        char scheme[16]; snprintf(scheme, sizeof(scheme), "%.*s", (int)slen, base_url);
        snprintf(out, cap, "%s:%s", scheme, href);
    } else {
        char scheme_host[HREF_CAP], dir[HREF_CAP];
        split_url(base_url, scheme_host, sizeof(scheme_host), dir, sizeof(dir));
        if (href[0] == '/')
            snprintf(out, cap, "%s%s", scheme_host, href);
        else
            snprintf(out, cap, "%s%s%s", scheme_host, dir, href);
    }
    /* Fragment abschneiden (#... ist innerhalb derselben Seite). */
    char *frag = strchr(out, '#');
    if (frag) *frag = '\0';
    return out[0] != '\0';
}

/* Findet das echte Ende eines Start-/End-Tags: das naechste '>' AUSSERHALB
 * eines gequoteten Attributwerts. Ein rohes '>' innerhalb von z.B.
 * data-mw='{"...":"...&lt;/ref>"}' (haeufig auf MediaWiki-Seiten, wo JSON-
 * Attribute HTML-Entities aber keine rohen '>' escapen) wuerde sonst das
 * Tag zu frueh "schliessen" und den Rest des JSON-Attributs als
 * sichtbaren Seitentext durchsickern lassen. */
static const char *find_tag_end(const char *p) {
    char q = 0;
    for (; *p; p++) {
        if (q) { if (*p == q) q = 0; continue; }
        if (*p == '"' || *p == '\'') { q = *p; continue; }
        if (*p == '>') return p;
    }
    return NULL;
}

static void decode_entities(const char *s, char *out, size_t cap) {
    size_t o = 0;
    for (const char *p = s; *p && o + 1 < cap; ) {
        if (*p == '&') {
            if (strncmp(p, "&amp;", 5) == 0)      { out[o++] = '&'; p += 5; continue; }
            if (strncmp(p, "&lt;", 4) == 0)       { out[o++] = '<'; p += 4; continue; }
            if (strncmp(p, "&gt;", 4) == 0)       { out[o++] = '>'; p += 4; continue; }
            if (strncmp(p, "&quot;", 6) == 0)     { out[o++] = '"'; p += 6; continue; }
            if (strncmp(p, "&#39;", 5) == 0 || strncmp(p, "&apos;", 6) == 0) {
                out[o++] = '\''; p += (p[2] == '3') ? 5 : 6; continue;
            }
            if (strncmp(p, "&nbsp;", 6) == 0)     { out[o++] = ' '; p += 6; continue; }
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
}

/* Wandelt HTML in lesbaren Text um, sammelt dabei bis zu MAX_LINKS Links
 * in s_links[] (fuer flux_browser_click()). Schreibt max. body_cap-1
 * Zeichen nach out_body. Best-effort-Parser: keine DOM/CSS-Semantik,
 * nur genug um Text und Links herauszuziehen. */
static void html_to_text(const char *html, const char *base_url,
                         char *out_body, size_t body_cap) {
    s_link_n = 0;
    size_t o = 0;
    int last_was_space = 1; /* fuehrenden Leerraum unterdruecken */
    int in_a = 0;
    char a_href[HREF_CAP] = {0};
    char a_text[LTEXT_CAP] = {0};
    size_t a_text_len = 0;

    const char *p = html;
    while (*p && o + 1 < body_cap) {
        if (*p == '<') {
            /* Kommentar */
            if (strncmp(p, "<!--", 4) == 0) {
                const char *end = strstr(p + 4, "-->");
                p = end ? end + 3 : p + strlen(p);
                continue;
            }
            /* script/style/noscript komplett ueberspringen (kein Text daraus) */
            if (strncasecmp(p + 1, "script", 6) == 0 ||
                strncasecmp(p + 1, "style", 5) == 0 ||
                strncasecmp(p + 1, "noscript", 8) == 0) {
                const char *tagend = find_tag_end(p);
                if (!tagend) break;
                const char *closer = (strncasecmp(p + 1, "script", 6) == 0) ? "</script" :
                                     (strncasecmp(p + 1, "style", 5) == 0) ? "</style" : "</noscript";
                const char *end = strcasestr_flux(tagend, closer);
                p = end ? strchr(end, '>') : NULL;
                p = p ? p + 1 : (html + strlen(html));
                continue;
            }
            int is_close = (p[1] == '/');
            int is_a = (strncasecmp(p + (is_close ? 2 : 1), "a", 1) == 0 &&
                        !isalnum((unsigned char)p[(is_close ? 2 : 1) + 1]));

            if (!is_close && is_a) {
                /* href="..." innerhalb dieses Tags suchen */
                const char *tagend = find_tag_end(p);
                if (!tagend) break;
                const char *h = strcasestr_flux(p, "href=");
                a_href[0] = '\0';
                if (h && h < tagend) {
                    h += 5;
                    char q = (*h == '"' || *h == '\'') ? *h++ : ' ';
                    const char *hend = (q == ' ') ? h : strchr(h, q);
                    if (!hend) hend = tagend;
                    size_t hl = (size_t)(hend - h);
                    if (hl >= sizeof(a_href)) hl = sizeof(a_href) - 1;
                    memcpy(a_href, h, hl);
                    a_href[hl] = '\0';
                }
                in_a = 1;
                a_text_len = 0;
                a_text[0] = '\0';
                p = tagend + 1;
                continue;
            }
            if (is_close && is_a) {
                if (in_a && a_text_len > 0 && s_link_n < MAX_LINKS) {
                    char resolved[HREF_CAP];
                    char decoded_text[LTEXT_CAP];
                    decode_entities(a_text, decoded_text, sizeof(decoded_text));
                    if (a_href[0] && resolve_href(base_url, a_href, resolved, sizeof(resolved))) {
                        snprintf(s_links[s_link_n].href, HREF_CAP, "%s", resolved);
                        snprintf(s_links[s_link_n].text, LTEXT_CAP, "%s", decoded_text);
                        s_link_n++;
                        int n = s_link_n;
                        size_t ol = strlen(out_body);
                        snprintf(out_body + ol, body_cap - ol, "%s [%d]", decoded_text, n);
                        o = strlen(out_body);
                        last_was_space = 0;
                    }
                }
                in_a = 0;
                const char *tagend = find_tag_end(p);
                p = tagend ? tagend + 1 : p + strlen(p);
                continue;
            }
            /* Block-Elemente: Zeilenumbruch statt Leerzeichen */
            int block = (strncasecmp(p + (is_close?2:1), "p", 1) == 0 ||
                        strncasecmp(p + (is_close?2:1), "div", 3) == 0 ||
                        strncasecmp(p + (is_close?2:1), "li", 2) == 0 ||
                        strncasecmp(p + (is_close?2:1), "br", 2) == 0 ||
                        strncasecmp(p + (is_close?2:1), "h1", 2) == 0 ||
                        strncasecmp(p + (is_close?2:1), "h2", 2) == 0 ||
                        strncasecmp(p + (is_close?2:1), "h3", 2) == 0 ||
                        strncasecmp(p + (is_close?2:1), "tr", 2) == 0);
            const char *tagend = find_tag_end(p);
            p = tagend ? tagend + 1 : p + strlen(p);
            if (block && !last_was_space) {
                if (in_a) { if (a_text_len + 1 < sizeof(a_text)) a_text[a_text_len++] = ' '; }
                else if (o + 1 < body_cap) { out_body[o++] = '\n'; out_body[o] = '\0'; }
                last_was_space = 1;
            }
            continue;
        }

        unsigned char c = (unsigned char)*p;
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ' && last_was_space) { p++; continue; }

        if (in_a) {
            if (a_text_len + 1 < sizeof(a_text)) a_text[a_text_len++] = (char)c;
            a_text[a_text_len] = '\0';
        } else {
            out_body[o++] = (char)c;
            out_body[o] = '\0';
        }
        last_was_space = (c == ' ');
        p++;
    }
    out_body[o] = '\0';
}

static int fetch_url(const char *url, flux_http_buf *mb, char *err, size_t err_cap) {
    CURL *curl = curl_easy_init();
    if (!curl) { snprintf(err, err_cap, "curl nicht verfuegbar"); return 0; }

    if (flux_http_buf_init(mb, 32768) != 0) {
        curl_easy_cleanup(curl);
        snprintf(err, err_cap, "kein Speicher");
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    /* Nur HTTP/HTTPS erlauben, auch fuer Redirect-Ziele -- sonst liesse
     * sich ueber file:// o.ae. theoretisch lokaler Dateiinhalt abgreifen.
     * *_STR-Variante (seit curl 7.85.0, Buildroot 2024.02 hat curl 8.x). */
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)(5 * 1024 * 1024));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, flux_http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, mb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (compatible; FluxOS-Reader/1.0)");

    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        flux_http_buf_free(mb);
        snprintf(err, err_cap, "Seite nicht erreichbar: %s", curl_easy_strerror(res));
        return 0;
    }
    if (http >= 400) {
        flux_http_buf_free(mb);
        snprintf(err, err_cap, "Server antwortete mit Status %ld", http);
        return 0;
    }
    return 1;
}

static int render_page(const char *url, char *out, size_t cap) {
    flux_http_buf mb;
    char err[128];
    if (!fetch_url(url, &mb, err, sizeof(err))) {
        snprintf(out, cap, "%s", err);
        return 1;
    }

    snprintf(s_base_url, sizeof(s_base_url), "%s", url);

    char title[160] = {0};
    const char *t0 = strcasestr_flux(mb.data, "<title");
    if (t0) {
        const char *tgt = strchr(t0, '>');
        if (tgt) {
            const char *tend = strcasestr_flux(tgt, "</title");
            if (tend) {
                char raw[160];
                size_t tl = (size_t)(tend - tgt - 1);
                if (tl >= sizeof(raw)) tl = sizeof(raw) - 1;
                memcpy(raw, tgt + 1, tl); raw[tl] = '\0';
                decode_entities(raw, title, sizeof(title));
            }
        }
    }

    char body[3200] = {0};
    html_to_text(mb.data, url, body, sizeof(body));
    flux_http_buf_free(&mb);

    size_t o = 0;
    if (title[0]) o += (size_t)snprintf(out + o, cap - o, "# %s\n", title);
    o += (size_t)snprintf(out + o, cap - o, "(%s)\n\n", url);
    if (o < cap) o += (size_t)snprintf(out + o, cap - o, "%s", body);

    if (s_link_n > 0 && o < cap) {
        o += (size_t)snprintf(out + o, cap - o, "\n\n[Links durch Zahl in eckigen Klammern "
                                                  "-- zum Weiternavigieren browser_click mit der "
                                                  "Zahl aufrufen.]");
    }
    (void)o;
    return 1;
}

int flux_browser_open(const char *url, char *out, size_t cap) {
    if (!url || !*url) {
        snprintf(out, cap, "Fehler: keine URL angegeben");
        return 1;
    }
    char full[HREF_CAP];
    if (strncasecmp(url, "http://", 7) != 0 && strncasecmp(url, "https://", 8) != 0)
        snprintf(full, sizeof(full), "https://%s", url);
    else
        snprintf(full, sizeof(full), "%s", url);
    return render_page(full, out, cap);
}

int flux_browser_click(const char *arg, char *out, size_t cap) {
    if (!arg || !*arg) {
        snprintf(out, cap, "Fehler: keine Link-Nummer angegeben");
        return 1;
    }
    int n = atoi(arg);
    if (n < 1 || n > s_link_n) {
        snprintf(out, cap,
                 "Kein Link Nummer %d auf der aktuellen Seite (nur %d verfuegbar). "
                 "Erst browser_open aufrufen.", n, s_link_n);
        return 1;
    }
    return render_page(s_links[n - 1].href, out, cap);
}
