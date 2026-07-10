/* flux_webview_protocol.h -- Mini-Protokoll zwischen flux-shell und dem
 * fluxweb-Companion-Prozess (echter JS-faehiger Browser via WPE WebKit,
 * siehe fluxweb/README.md).
 *
 * Eigener Socket/eigenes Protokoll statt Wiederverwendung von
 * flux_protocol.h: fluxaid (Q:/X:) und fluxweb (hier) sind unabhaengige
 * Prozesse mit unabhaengigen Zwecken -- fluxaid denkt/spricht mit dem
 * LLM, fluxweb rendert Webseiten. Sie ueber einen gemeinsamen Socket zu
 * fuehren wuerde beide Protokolle unnoetig verkoppeln.
 *
 *   Client (flux-shell) -> Server (fluxweb):
 *     "LOAD:<url>\n"            -- Seite laden
 *     "CLICK:<x>,<y>\n"         -- an Position (Viewport-Pixel) klicken
 *     "SCROLL:<dy>\n"           -- um dy Pixel scrollen (kann negativ sein)
 *     "SIZE:<w>,<h>\n"          -- Viewport-Groesse setzen (einmalig beim Verbindungsaufbau)
 *
 *   Server -> Client (auf jeden Befehl, wenn der Frame fertig ist):
 *     "FRAME:<path>,<w>,<h>\nEND\n"  -- Pfad zu einer rohen RGBA-Datei
 *                                       (w*h*4 Byte, im /run/flux/-Tmpfs)
 *                                       mit dem aktuellen Seiten-Frame
 *     "ERR:<meldung>\nEND\n"
 *
 * Bewusst dateibasiert statt der Frame-Bytes direkt ueber den Socket:
 * ein Viewport-Frame (480x854x4 Byte ~ 1.6 MB) ist fuer das simple
 * Zeilenprotokoll unhandlich; ein Pfad in ein tmpfs ist einfacher und
 * die Shell kann ihn direkt per mmap/read lesen und mit
 * flux_fb_blit_rgba() (fb.h) ins eigene Backbuffer blitten.
 */
#ifndef FLUX_WEBVIEW_PROTOCOL_H
#define FLUX_WEBVIEW_PROTOCOL_H

#define FLUX_WEBVIEW_SOCK_PATH  "/run/flux/fluxweb.sock"
#define FLUX_WEBVIEW_FRAME_DIR  "/run/flux/webview"
#define FLUX_WEBVIEW_MAX_LINE   1024

#endif
