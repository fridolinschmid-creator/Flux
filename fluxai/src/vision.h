/* vision.h -- KI-Bildanalyse, zwei austauschbare Backends.
 * Konvertiert PPM → JPEG (ImageMagick), base64-kodiert und schickt das Bild
 * je nach Config-Key `vision_backend` an:
 *   "cloud" (Default) -- Anthropic Messages-API.
 *   "local"           -- lokaler VLM-Server (OpenAI-kompatibel, `vlm_url`).
 * Beschreibt Inhalt und (falls erkennbar) Ort.
 */
#ifndef FLUX_VISION_H
#define FLUX_VISION_H

#include <stddef.h>

/* Analysiert eine PPM-Bilddatei. Backend (Cloud/Lokal) per Config gewaehlt.
 * ppm_path: Pfad zur PPM-Datei.
 * out/out_cap: Ausgabepuffer fuer die KI-Antwort.
 * api_key: Anthropic-API-Schluessel (nur Cloud-Pfad; darf NULL sein, dann
 *          aus Config/Env). Beim lokalen VLM-Pfad ignoriert.
 * Gibt 1 bei Erfolg. */
int flux_vision_analyze(const char *ppm_path, char *out, size_t out_cap,
                        const char *api_key);

#endif
