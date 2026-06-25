/* flux_util.c -- siehe flux_util.h. Implementierung der gemeinsamen
 * Hilfsfunktionen (Base64, HTTP-Antwortpuffer, ASCII-Kleinschreibung,
 * JSON-String-Helfer). */
#include "flux_util.h"

#include <stdlib.h>
#include <string.h>

/* ---- Base64 ---------------------------------------------------------- */

static const char b64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t flux_base64_encode(const unsigned char *in, size_t in_len,
                          char *out, size_t out_cap) {
    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 3) {
        unsigned int n = (unsigned int)in[i] << 16;
        if (i + 1 < in_len) n |= (unsigned int)in[i + 1] << 8;
        if (i + 2 < in_len) n |= (unsigned int)in[i + 2];
        if (o + 5 >= out_cap) break;
        out[o++] = b64_chars[(n >> 18) & 0x3F];
        out[o++] = b64_chars[(n >> 12) & 0x3F];
        out[o++] = (i + 1 < in_len) ? b64_chars[(n >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < in_len) ? b64_chars[n & 0x3F] : '=';
    }
    out[o] = '\0';
    return o;
}

/* ---- Wachsender HTTP-Antwortpuffer ----------------------------------- */

int flux_http_buf_init(flux_http_buf *buf, size_t initial_cap) {
    if (initial_cap < 1) initial_cap = 1;
    buf->data = malloc(initial_cap);
    buf->len = 0;
    buf->cap = initial_cap;
    if (!buf->data) { buf->cap = 0; return -1; }
    buf->data[0] = '\0';
    return 0;
}

void flux_http_buf_free(flux_http_buf *buf) {
    if (!buf) return;
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

size_t flux_http_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    flux_http_buf *mb = userdata;
    size_t add = size * nmemb;
    if (mb->len + add + 1 > mb->cap) {
        size_t nc = mb->cap ? mb->cap * 2 : 16384;
        while (nc < mb->len + add + 1) nc *= 2;
        char *nd = realloc(mb->data, nc);
        if (!nd) return 0; /* OOM -> libcurl bricht ab */
        mb->data = nd;
        mb->cap = nc;
    }
    memcpy(mb->data + mb->len, ptr, add);
    mb->len += add;
    mb->data[mb->len] = '\0';
    return add;
}

/* ---- String-Helfer --------------------------------------------------- */

void flux_str_tolower_ascii(char *dst, size_t dstcap, const char *src) {
    if (!dst || dstcap == 0) return;
    size_t o = 0;
    if (src) {
        for (; src[o] && o + 1 < dstcap; o++) {
            char c = src[o];
            dst[o] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        }
    }
    dst[o] = '\0';
}

/* ---- JSON-String-Helfer ---------------------------------------------- */

size_t flux_json_decode_string(const char *p, char *out, size_t cap) {
    size_t o = 0;
    if (cap == 0) return 0;
    while (*p && *p != '"' && o + 1 < cap) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'n': out[o++] = '\n'; break;
                case 't': out[o++] = '\t'; break;
                case 'r': out[o++] = '\r'; break;
                case '"': out[o++] = '"';  break;
                case '/': out[o++] = '/';  break;
                case '\\': out[o++] = '\\'; break;
                case 'u': {
                    /* \uXXXX -- Basic Multilingual Plane nach UTF-8 */
                    if (p[1] && p[2] && p[3] && p[4]) {
                        char hex[5] = { p[1], p[2], p[3], p[4], 0 };
                        unsigned int v = (unsigned int)strtol(hex, NULL, 16);
                        if (v < 0x80) {
                            out[o++] = (char)v;
                        } else if (v < 0x800) {
                            if (o + 2 < cap) {
                                out[o++] = (char)(0xC0 | (v >> 6));
                                out[o++] = (char)(0x80 | (v & 0x3F));
                            }
                        } else {
                            if (o + 3 < cap) {
                                out[o++] = (char)(0xE0 | (v >> 12));
                                out[o++] = (char)(0x80 | ((v >> 6) & 0x3F));
                                out[o++] = (char)(0x80 | (v & 0x3F));
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
    return o;
}

int flux_json_get_string(const char *json, const char *key,
                         char *out, size_t cap) {
    if (cap == 0) return 0;
    out[0] = '\0';
    if (!json || !key) return 0;

    /* Suchmuster "key" aufbauen und das erste Vorkommen finden. */
    char pat[128];
    size_t kl = strlen(key);
    if (kl + 3 > sizeof(pat)) return 0; /* Schluessel zu lang */
    pat[0] = '"';
    memcpy(pat + 1, key, kl);
    pat[1 + kl] = '"';
    pat[2 + kl] = '\0';

    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += kl + 2;                 /* hinter abschliessendes " des Schluessels */
    while (*p == ' ') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ') p++;
    if (*p != '"') return 0;
    p++;                         /* hinter oeffnendes " des Wertes */

    flux_json_decode_string(p, out, cap);
    return 1;
}
