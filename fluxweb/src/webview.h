/* webview.h -- Schnittstelle zwischen main.c (Socket-Server, auf dem
 * Host kompilierbar/getestet) und webview.c (die eigentlichen WPE/
 * libwpe-Aufrufe, NICHT verifiziert -- siehe README.md).
 *
 * main.c kennt keine WPE-Typen, nur diese Funktionen -- so kann der
 * Socket-/Protokoll-Teil unabhaengig von WPE gebaut und getestet werden
 * (siehe `make` ohne WPE=1).
 */
#ifndef FLUXWEB_WEBVIEW_H
#define FLUXWEB_WEBVIEW_H

#include <stddef.h>

/* Wird aufgerufen, sobald ein neuer Frame fertig gerendert ist (w>=0,
 * path = Datei mit rohen RGBA-Bytes w*h*4, bleibt bis zum naechsten
 * Aufruf gueltig) ODER sobald feststeht, dass der letzte Request nicht
 * erfuellt werden kann (w<0, path = deutsche Fehlermeldung). Der
 * Fehlerfall ist wichtig: ohne ihn wuerde main.c auf eine Antwort warten,
 * die nie kommt (z.B. im Stub-Build ohne WPE=1) -- "ehrlich fehlschlagen"
 * statt den Client fuer immer haengen zu lassen. Laeuft im GLib-Main-
 * Loop-Kontext von main.c. */
typedef void (*webview_frame_cb)(const char *path, int w, int h, void *user);

/* Einmalig beim Start. Gibt 0 bei Erfolg. */
int webview_init(webview_frame_cb on_frame, void *user);

/* Viewport-Groesse setzen (vor dem ersten load noetig). */
void webview_set_size(int w, int h);

/* Seite laden. Ergebnis kommt asynchron ueber den frame_cb (oder nie,
 * wenn die Seite dauerhaft haengt -- main.c muss einen Timeout setzen). */
void webview_load(const char *url);

/* Klick an Viewport-Position (x,y) simulieren. */
void webview_click(int x, int y);

/* Um dy Pixel scrollen (negativ = hoch). */
void webview_scroll(int dy);

#endif
