/* browser.h -- Text-Browser-Tool: die KI kann Webseiten abrufen und ueber
 * nummerierte Links autonom weiternavigieren, ohne JavaScript/CSS-Layout/
 * Bilder. Siehe browser.c fuer die technische Begruendung (kein X11/GPU
 * auf dem Zielsystem -- ein echter grafischer Browser ist ein separates,
 * deutlich groesseres Vorhaben).
 */
#ifndef FLUX_BROWSER_H
#define FLUX_BROWSER_H

#include <stddef.h>

/* Ruft `url` ab, wandelt den HTML-Inhalt in lesbaren Text um und haengt
 * eine nummerierte Linkliste an. Merkt sich die aufgeloesten Link-Ziele
 * fuer einen folgenden flux_browser_click()-Aufruf. */
int flux_browser_open(const char *url, char *out, size_t cap);

/* Navigiert zu Link Nummer `arg` aus der zuletzt via flux_browser_open()
 * geladenen Seite (arg ist die Zahl als Text, z.B. "3"). */
int flux_browser_click(const char *arg, char *out, size_t cap);

#endif
