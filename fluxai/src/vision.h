/* vision.h -- KI-Bildanalyse via Anthropic Vision API.
 * Konvertiert PPM → JPEG (ImageMagick), base64-kodiert und schickt das Bild
 * an die Anthropic Messages-API. Beschreibt Inhalt und (falls erkennbar) Ort.
 */
#ifndef FLUX_VISION_H
#define FLUX_VISION_H

#include <stddef.h>

/* Analysiert eine PPM-Bilddatei mit der Anthropic Vision API.
 * ppm_path: Pfad zur PPM-Datei.
 * out/out_cap: Ausgabepuffer fuer die KI-Antwort.
 * api_key: Anthropic-API-Schluessel (darf NULL sein, dann aus Config/Env).
 * Gibt 1 bei Erfolg. */
int flux_vision_analyze(const char *ppm_path, char *out, size_t out_cap,
                        const char *api_key);

#endif
