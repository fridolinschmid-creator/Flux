/* flux_util.h -- gemeinsame Hilfsfunktionen fuer fluxaid (und kuenftig die
 * Shell). Buendelt Code, der zuvor mehrfach (in vision.c, provider.c und
 * tools.c) nahezu identisch vorlag:
 *   - Base64-Kodierung
 *   - ein wachsender HTTP-Antwortpuffer + passender libcurl-Write-Callback
 *   - ASCII-Kleinschreibung (locale-unabhaengig)
 *   - ein kleiner JSON-String-Helfer (Schluessel suchen + Escapes dekodieren)
 *
 * Bewusst minimal gehalten -- dieselbe Haltung wie bei flux_config/flux_sha256:
 * keine externe Bibliothek fuer eine Handvoll kleiner Bausteine.
 */
#ifndef FLUX_UTIL_H
#define FLUX_UTIL_H

#include <stddef.h>

/* ---- Base64 ---------------------------------------------------------- */

/* Kodiert in_len Bytes aus in nach out (nullterminiert). out_cap ist die
 * Groesse des Zielpuffers inkl. Platz fuer '\0'. Gibt die Anzahl
 * geschriebener Zeichen (ohne '\0') zurueck. Bricht ab, sobald out voll
 * waere -- der Aufrufer dimensioniert out mit mind. in_len*4/3 + 4. */
size_t flux_base64_encode(const unsigned char *in, size_t in_len,
                          char *out, size_t out_cap);

/* ---- Wachsender HTTP-Antwortpuffer ----------------------------------- */

/* Heap-basierter, bei Bedarf per realloc() wachsender Puffer fuer
 * libcurl-Antworten. WICHTIG: data MUSS aus malloc() stammen (oder NULL
 * sein) -- niemals auf einen Stack-Array zeigen lassen, da der Callback
 * realloc() aufruft. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} flux_http_buf;

/* Initialisiert buf mit einer Startkapazitaet. Gibt 0 bei Erfolg zurueck,
 * -1 bei fehlgeschlagenem malloc (buf->data ist dann NULL). */
int flux_http_buf_init(flux_http_buf *buf, size_t initial_cap);

/* Gibt den Puffer frei und setzt das Feld zurueck (data wird NULL). */
void flux_http_buf_free(flux_http_buf *buf);

/* libcurl-Write-Callback. Als CURLOPT_WRITEFUNCTION eintragen, mit einem
 * flux_http_buf* als CURLOPT_WRITEDATA. Waechst bei Bedarf; gibt bei OOM 0
 * zurueck, womit libcurl den Transfer abbricht. */
size_t flux_http_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata);

/* ---- String-Helfer --------------------------------------------------- */

/* Kopiert src nach dst und wandelt dabei ASCII-Grossbuchstaben (A-Z) in
 * Kleinbuchstaben um -- bewusst locale-unabhaengig (nur A-Z). dst wird
 * immer nullterminiert; bei zu kleinem dstcap wird abgeschnitten. */
void flux_str_tolower_ascii(char *dst, size_t dstcap, const char *src);

/* ---- JSON-String-Helfer ---------------------------------------------- */

/* Dekodiert einen JSON-String ab p (p zeigt direkt HINTER das oeffnende ")
 * bis zum schliessenden " nach out (nullterminiert). Behandelt die Escapes
 * \" \\ \/ \n \t \r sowie \uXXXX -> UTF-8 (BMP). Gibt die Anzahl
 * geschriebener Bytes (ohne '\0') zurueck. */
size_t flux_json_decode_string(const char *p, char *out, size_t cap);

/* Sucht das ERSTE Vorkommen von "key":"..." in json und dekodiert den
 * String-Wert nach out (siehe flux_json_decode_string). Toleriert
 * Leerzeichen zwischen ':' und dem oeffnenden ". Gibt 1 bei Erfolg
 * zurueck, 0 wenn der Schluessel nicht gefunden wurde. */
int flux_json_get_string(const char *json, const char *key,
                         char *out, size_t cap);

#endif
