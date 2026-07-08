/* specialists.h -- Spezialisten-Erkenner fuer feinkoernige Identitaet.
 *
 * Architektur-These: Ein generisches VLM (siehe vision.c / image_analyze) ist
 * gut darin zu sagen "was fuer ein Ding" zu sehen ist (eine Pflanze, ein Logo),
 * aber unzuverlaessig bei der GENAUEN Identitaet (exakte Pflanzenart, exakte
 * Marke). Dafuer gibt es spezialisierte Erkenner: das VLM liefert die grobe
 * Beschreibung, diese Funktionen die exakte Identitaet, web_search die Fakten,
 * und das Reasoning-Modell fuegt alles zusammen.
 *
 * Beide Funktionen verhalten sich EHRLICH: ist der jeweilige API-Key nicht
 * konfiguriert, geben sie 0 zurueck und schreiben eine klare deutsche Meldung,
 * statt etwas zu erfinden.
 */
#ifndef FLUX_SPECIALISTS_H
#define FLUX_SPECIALISTS_H

#include <stddef.h>

/* Bestimmt die exakte Pflanzenart per Pl@ntNet-API.
 * image_path: Pfad zur Bilddatei (muss unter /home/user/ liegen).
 * out/out_cap: Ausgabepuffer fuer die deutsche Zusammenfassung.
 * Gibt 1 bei erfolgreicher Erkennung zurueck, 0 sonst (inkl. fehlendem
 * Config-Key plantnet_key -- dann steht eine ehrliche Meldung in out). */
int flux_plant_identify(const char *image_path, char *out, size_t out_cap);

/* Erkennt Logos/Marken und liest Text (OCR) per Google Cloud Vision API.
 * image_path: Pfad zur Bilddatei (muss unter /home/user/ liegen).
 * out/out_cap: Ausgabepuffer fuer die deutsche Zusammenfassung.
 * Gibt 1 bei erfolgreicher Erkennung zurueck, 0 sonst (inkl. fehlendem
 * Config-Key gvision_key -- dann steht eine ehrliche Meldung in out). */
int flux_logo_detect(const char *image_path, char *out, size_t out_cap);

#endif
