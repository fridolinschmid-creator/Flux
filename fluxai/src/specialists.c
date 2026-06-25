/* specialists.c -- Spezialisten-Erkenner fuer feinkoernige Identitaet.
 *
 * flux_plant_identify -- Pflanzenart via Pl@ntNet-API (multipart/form-data).
 * flux_logo_detect    -- Logo-/Markenerkennung + OCR via Google Cloud Vision.
 *
 * Beide Funktionen sind EHRLICH: fehlt der jeweilige API-Key in der Config,
 * geben sie 0 zurueck und schreiben eine klare deutsche "nicht konfiguriert"-
 * Meldung, statt eine Antwort zu erfinden. Aufbau und Fehlerstil orientieren
 * sich an vision.c (Bild lesen, ggf. PPM->JPEG via convert, libcurl-POST,
 * flux_http_buf fuer die Antwort, 30s Timeout).
 *
 * Benoetigt: libcurl. Fuer .ppm-Eingaben zusaetzlich ImageMagick (convert).
 */
#include "specialists.h"
#include "../../common/flux_config.h"
#include "../../common/flux_util.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>

#define PLANTNET_API_URL "https://my-api.plantnet.org/v2/identify/all"
#define GVISION_API_URL  "https://vision.googleapis.com/v1/images:annotate"

/* ---- Pfad-Sicherheit -------------------------------------------------- *
 * Wie in tools.c/vision.c: kanonisch aufloesen und nur unter /home/user/
 * zulassen, damit ueber die Spezialisten keine beliebigen Dateien gelesen
 * werden koennen. Gibt 1 zurueck und schreibt den kanonischen Pfad nach
 * resolved (Groesse PATH_MAX), 0 sonst. */
static int spec_path_ok(const char *path, char *resolved) {
    if (!path || !*path) return 0;
    if (strstr(path, "..")) return 0;
    if (!realpath(path, resolved)) return 0;
    if (strncmp(resolved, "/home/user/", 11) != 0) return 0;
    return 0 == access(resolved, R_OK) ? 1 : 0;
}

/* ---- Bild laden (ggf. PPM->JPEG) -------------------------------------- *
 * Liest die Bilddatei nach *data_out / *size_out (heap, vom Aufrufer mit
 * free() freizugeben). Ist die Eingabe eine .ppm-Datei, wird sie -- exakt wie
 * in vision.c -- per fork/execvp("convert", ... "jpg:<tmp>") nach JPEG
 * gewandelt; *is_jpeg wird dann auf 1 gesetzt. Bei .jpg/.jpeg gilt JPEG, sonst
 * (z.B. .png) PNG. Gibt 1 bei Erfolg zurueck, 0 bei Fehler (out enthaelt dann
 * eine deutsche Fehlermeldung). */
static int load_image(const char *resolved, unsigned char **data_out,
                      long *size_out, int *is_jpeg, char *out, size_t out_cap) {
    *data_out = NULL;
    *size_out = 0;
    *is_jpeg = 0;

    const char *read_path = resolved;
    char tmpl[] = "/tmp/flux_spec_XXXXXX";
    int converted = 0;

    size_t pl = strlen(resolved);
    int is_ppm  = (pl > 4 && strcasecmp(resolved + pl - 4, ".ppm")  == 0);
    int is_jpg  = (pl > 4 && strcasecmp(resolved + pl - 4, ".jpg")  == 0) ||
                  (pl > 5 && strcasecmp(resolved + pl - 5, ".jpeg") == 0);

    if (is_ppm) {
        /* PPM -> JPEG via ImageMagick, ohne system()-Shell (vgl. vision.c). */
        int tfd = mkstemp(tmpl);
        if (tfd < 0) {
            snprintf(out, out_cap, "Temporaere Datei konnte nicht angelegt werden.");
            return 0;
        }
        close(tfd);
        char jpg_target[64];
        snprintf(jpg_target, sizeof(jpg_target), "jpg:%s", tmpl);
        pid_t pid = fork();
        if (pid < 0) {
            unlink(tmpl);
            snprintf(out, out_cap, "fork() fehlgeschlagen.");
            return 0;
        }
        if (pid == 0) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
            char quality[] = "80";
            char *argv[] = { "convert", (char *)resolved, "-quality", quality,
                             jpg_target, NULL };
            execvp("convert", argv);
            _exit(127);
        }
        int wstatus = 0;
        waitpid(pid, &wstatus, 0);
        if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
            unlink(tmpl);
            snprintf(out, out_cap,
                     "Bildkonvertierung fehlgeschlagen (convert nicht installiert?).");
            return 0;
        }
        read_path = tmpl;
        converted = 1;
        *is_jpeg = 1;
    } else {
        *is_jpeg = is_jpg;  /* .jpg/.jpeg -> JPEG, sonst (z.B. .png) -> PNG */
    }

    FILE *f = fopen(read_path, "rb");
    if (!f) {
        if (converted) unlink(tmpl);
        snprintf(out, out_cap, "Bilddatei nicht lesbar.");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(f);
        if (converted) unlink(tmpl);
        snprintf(out, out_cap, "Bild zu gross oder leer.");
        return 0;
    }
    unsigned char *data = malloc((size_t)fsize);
    if (!data) {
        fclose(f);
        if (converted) unlink(tmpl);
        snprintf(out, out_cap, "Kein Speicher.");
        return 0;
    }
    if ((long)fread(data, 1, (size_t)fsize, f) != fsize) {
        fclose(f);
        free(data);
        if (converted) unlink(tmpl);
        snprintf(out, out_cap, "Lesefehler beim Bild.");
        return 0;
    }
    fclose(f);
    if (converted) unlink(tmpl);

    *data_out = data;
    *size_out = fsize;
    return 1;
}

/* ====================================================================== *
 *  Pl@ntNet -- Pflanzenart
 * ====================================================================== */

/* Sucht das erste numerische Feld "key": <zahl> in json und schreibt es als
 * Text nach out. Gibt 1 bei Erfolg. Ergaenzt flux_json_get_string (das nur
 * String-Werte in Anfuehrungszeichen liest). */
static int json_get_number(const char *json, const char *key,
                           char *out, size_t cap) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    size_t o = 0;
    while (*p && (*p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' ||
                  (*p >= '0' && *p <= '9')) && o + 1 < cap)
        out[o++] = *p++;
    out[o] = '\0';
    return o > 0;
}

int flux_plant_identify(const char *image_path, char *out, size_t out_cap) {
    if (!image_path || !*image_path) {
        snprintf(out, out_cap, "Fehler: kein Bildpfad angegeben.");
        return 0;
    }

    char key[256] = {0};
    if (!flux_config_get("plantnet_key", key, sizeof(key)) || !key[0]) {
        snprintf(out, out_cap,
                 "Pflanzenerkennung nicht konfiguriert (Pl@ntNet-API-Key fehlt: "
                 "setze plantnet_key in den Einstellungen).");
        return 0;
    }

    char resolved[PATH_MAX];
    if (!spec_path_ok(image_path, resolved)) {
        snprintf(out, out_cap,
                 "Bilddatei nicht gefunden oder ausserhalb von /home/user/: %s",
                 image_path);
        return 0;
    }

    unsigned char *img = NULL;
    long imgsize = 0;
    int is_jpeg = 0;
    if (!load_image(resolved, &img, &imgsize, &is_jpeg, out, out_cap))
        return 0;  /* out bereits gesetzt */

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(img);
        snprintf(out, out_cap, "Fehler: curl nicht verfuegbar.");
        return 0;
    }

    flux_http_buf resp;
    if (flux_http_buf_init(&resp, 4096) != 0) {
        curl_easy_cleanup(curl);
        free(img);
        snprintf(out, out_cap, "Fehler: kein Speicher.");
        return 0;
    }

    char url[512];
    snprintf(url, sizeof(url), "%s?api-key=%s", PLANTNET_API_URL, key);

    /* multipart/form-data: Bildteil "images" + Formfeld "organs=auto" */
    curl_mime *mime = curl_mime_init(curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "images");
    curl_mime_data(part, (const char *)img, (size_t)imgsize);
    curl_mime_filename(part, is_jpeg ? "plant.jpg" : "plant.png");
    curl_mime_type(part, is_jpeg ? "image/jpeg" : "image/png");
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "organs");
    curl_mime_data(part, "auto", CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, flux_http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "flux-os/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);

    int ok = 0;
    if (res != CURLE_OK) {
        snprintf(out, out_cap, "Netzwerkfehler: %s", curl_easy_strerror(res));
    } else if (http == 401 || http == 403) {
        snprintf(out, out_cap,
                 "Pl@ntNet-Zugriff verweigert (HTTP %ld) -- ist plantnet_key gueltig?",
                 http);
    } else if (http != 200) {
        snprintf(out, out_cap, "Pl@ntNet-Fehler (HTTP %ld).", http);
    } else {
        /* "bestMatch":"<art>" ist der wahrscheinlichste Treffer; der erste
         * "score" im results-Array ist die zugehoerige Sicherheit (0..1). */
        char species[256] = {0};
        char score[32]    = {0};
        int hs = flux_json_get_string(resp.data, "bestMatch", species, sizeof(species));
        int hc = json_get_number(resp.data, "score", score, sizeof(score));
        if (hs && species[0]) {
            if (hc && score[0])
                snprintf(out, out_cap,
                         "Wahrscheinlichste Art: %s (Sicherheit %s)", species, score);
            else
                snprintf(out, out_cap, "Wahrscheinlichste Art: %s", species);
            ok = 1;
        } else {
            snprintf(out, out_cap,
                     "Keine Pflanzenart erkannt (Pl@ntNet lieferte kein Ergebnis).");
        }
    }

    curl_mime_free(mime);
    curl_easy_cleanup(curl);
    flux_http_buf_free(&resp);
    free(img);
    return ok;
}

/* ====================================================================== *
 *  Google Cloud Vision -- Logo/Marke + OCR-Text
 * ====================================================================== */

/* Liest aus dem ersten "logoAnnotations"- bzw. "textAnnotations"-Array die
 * "description"-Werte. Google liefert mehrere Logos; beim Text ist der erste
 * Eintrag der gesamte erkannte Block. Wir bleiben bewusst einfach: bis zu
 * max Treffer (logos) bzw. der erste (text), jeweils per
 * flux_json_get_string ab der gefundenen Array-Position. */
static int extract_descriptions(const char *json, const char *arr_key,
                                int max, char *out, size_t cap) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", arr_key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    int n = 0;
    size_t ol = strlen(out);
    while (n < max) {
        const char *d = strstr(p, "\"description\"");
        if (!d) break;
        char desc[512] = {0};
        if (!flux_json_get_string(d, "description", desc, sizeof(desc)) || !desc[0])
            break;
        /* Newlines im OCR-Text durch Leerzeichen ersetzen (kompakte Ausgabe) */
        for (char *q = desc; *q; q++) if (*q == '\n' || *q == '\r') *q = ' ';
        int w = snprintf(out + ol, cap > ol ? cap - ol : 0,
                         "%s%s", n ? ", " : "", desc);
        if (w > 0) ol += (size_t)w;
        n++;
        p = d + strlen("\"description\"");
    }
    return n;
}

int flux_logo_detect(const char *image_path, char *out, size_t out_cap) {
    if (!image_path || !*image_path) {
        snprintf(out, out_cap, "Fehler: kein Bildpfad angegeben.");
        return 0;
    }

    char key[256] = {0};
    if (!flux_config_get("gvision_key", key, sizeof(key)) || !key[0]) {
        snprintf(out, out_cap,
                 "Logo-/Markenerkennung nicht konfiguriert (Google-Vision-API-Key "
                 "fehlt: setze gvision_key in den Einstellungen).");
        return 0;
    }

    char resolved[PATH_MAX];
    if (!spec_path_ok(image_path, resolved)) {
        snprintf(out, out_cap,
                 "Bilddatei nicht gefunden oder ausserhalb von /home/user/: %s",
                 image_path);
        return 0;
    }

    unsigned char *img = NULL;
    long imgsize = 0;
    int is_jpeg = 0;
    if (!load_image(resolved, &img, &imgsize, &is_jpeg, out, out_cap))
        return 0;  /* out bereits gesetzt */

    /* Base64-Kodierung (Google Vision erwartet content als base64). */
    size_t b64_cap = (size_t)imgsize * 4 / 3 + 8;
    char *b64 = malloc(b64_cap);
    if (!b64) {
        free(img);
        snprintf(out, out_cap, "Kein Speicher.");
        return 0;
    }
    flux_base64_encode(img, (size_t)imgsize, b64, b64_cap);
    free(img);

    /* JSON-Body: ein Request mit LOGO_DETECTION und TEXT_DETECTION. */
    size_t body_cap = b64_cap + 512;
    char *body = malloc(body_cap);
    if (!body) {
        free(b64);
        snprintf(out, out_cap, "Kein Speicher.");
        return 0;
    }
    int blen = snprintf(body, body_cap,
        "{\"requests\":[{"
        "\"image\":{\"content\":\"%s\"},"
        "\"features\":["
            "{\"type\":\"LOGO_DETECTION\",\"maxResults\":5},"
            "{\"type\":\"TEXT_DETECTION\",\"maxResults\":1}"
        "]}]}",
        b64);
    free(b64);
    if (blen <= 0 || (size_t)blen >= body_cap) {
        free(body);
        snprintf(out, out_cap, "Anfrage-Aufbau fehlgeschlagen.");
        return 0;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(body);
        snprintf(out, out_cap, "Fehler: curl nicht verfuegbar.");
        return 0;
    }

    flux_http_buf resp;
    if (flux_http_buf_init(&resp, 4096) != 0) {
        curl_easy_cleanup(curl);
        free(body);
        snprintf(out, out_cap, "Fehler: kein Speicher.");
        return 0;
    }

    char url[512];
    snprintf(url, sizeof(url), "%s?key=%s", GVISION_API_URL, key);

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "content-type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)blen);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, flux_http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "flux-os/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);

    int ok = 0;
    if (res != CURLE_OK) {
        snprintf(out, out_cap, "Netzwerkfehler: %s", curl_easy_strerror(res));
    } else if (http == 400 || http == 401 || http == 403) {
        snprintf(out, out_cap,
                 "Google-Vision-Zugriff verweigert (HTTP %ld) -- ist gvision_key gueltig?",
                 http);
    } else if (http != 200) {
        snprintf(out, out_cap, "Google-Vision-Fehler (HTTP %ld).", http);
    } else {
        char logos[1024] = {0};
        char text[2048]  = {0};
        int nl = extract_descriptions(resp.data, "logoAnnotations", 5,
                                      logos, sizeof(logos));
        int nt = extract_descriptions(resp.data, "textAnnotations", 1,
                                      text, sizeof(text));
        size_t pos = 0;
        if (nl > 0)
            pos += (size_t)snprintf(out + pos, out_cap - pos,
                                    "Erkannte Logos/Marken: %s", logos);
        if (nt > 0 && text[0])
            pos += (size_t)snprintf(out + pos, out_cap - pos,
                                    "%sErkannter Text: %s",
                                    pos ? "\n" : "", text);
        if (nl == 0 && nt == 0)
            snprintf(out, out_cap,
                     "Kein Logo und kein Text erkannt (Google Vision lieferte "
                     "kein Ergebnis).");
        else
            ok = 1;
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    flux_http_buf_free(&resp);
    free(body);
    return ok;
}
