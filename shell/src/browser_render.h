/* browser_render.h -- echtes visuelles HTML/CSS-Rendering fuer den
 * Browser-Screen (litehtml + gumbo, vendored unter shell/src/litehtml/).
 *
 * Kein JavaScript, keine Interaktivitaet ausser Link-Antippen -- aber
 * echtes Layout, echte Schriftgroessen/-farben, echte Bilder statt reinem
 * Text. Die gesamte C++/litehtml-Komplexitaet ist hinter dieser reinen
 * C-Schnittstelle versteckt, damit main.c/ui.c (C11) unveraendert bleiben
 * koennen.
 *
 * Architektur-Hinweis: anders als der textbasierte Browser (fluxaid,
 * ueber die Q:/A:-IPC) laedt dieser Pfad Seiten UND Bilder direkt aus
 * flux-shell selbst (eigenes libcurl). Der Grund: eine ganze HTML-Seite
 * plus Bilder passt nicht durch das mit FLUX_MAX_RESPONSE (8400 Byte)
 * gedeckelte Zeilenprotokoll, das fuer kurze KI-Antworten gedacht ist --
 * eine Protokolladerung dafuer waere ein groesserer Eingriff gewesen als
 * der Shell direkten Netzzugriff fuer diesen einen Zweck zu geben.
 */
#ifndef FLUX_BROWSER_RENDER_H
#define FLUX_BROWSER_RENDER_H

#ifdef __cplusplus
extern "C" {
#endif

/* fb.h selbst hat keine eigenen extern "C"-Wachen (wird sonst nur aus
 * reinem C eingebunden) -- deshalb hier innerhalb des Blocks einbinden,
 * sonst wuerden die fb.c-Symbole mit C++-Namensverstuemmelung erwartet
 * und der Linker faende sie nicht. */
#include "fb.h"

/* Laedt url, parst/layoutet HTML+CSS fuer die gegebene Viewport-Breite.
 * resolved_url erhaelt die tatsaechliche (evtl. redirectete) Adresse,
 * title den Seitentitel. Gibt 0 bei Erfolg, -1 bei Fehler (err erhaelt
 * dann eine kurze deutsche Fehlermeldung). */
int flux_browser_render_open(const char *url, int viewport_w,
                             char *resolved_url, size_t url_cap,
                             char *title, size_t title_cap,
                             char *err, size_t err_cap);

/* Navigiert zu Link Nummer `idx` (0-basiert, siehe
 * flux_browser_render_link_at) der zuletzt gerenderten Seite. */
int flux_browser_render_click(int idx, int viewport_w,
                              char *resolved_url, size_t url_cap,
                              char *title, size_t title_cap,
                              char *err, size_t err_cap);

/* Gesamthoehe des aktuell gerenderten Dokuments in Pixeln (fuer die
 * Scroll-Begrenzung in main.c). 0 wenn keine Seite geladen. */
int flux_browser_render_height(void);

/* Zeichnet den sichtbaren Ausschnitt der aktuellen Seite in fb, zwischen
 * Bildschirm-Y top (einschliesslich) und bottom (ausschliesslich),
 * beginnend bei Dokument-Y scroll_y. */
void flux_browser_render_draw(flux_fb_t *fb, int top, int bottom, int scroll_y);

/* Trefferpruefung: Bildschirmkoordinate (x,y) -> Link-Index (0-basiert)
 * oder -1. top/scroll_y wie bei flux_browser_render_draw(). */
int flux_browser_render_link_at(int x, int y, int top, int scroll_y);

#ifdef __cplusplus
}
#endif

#endif
