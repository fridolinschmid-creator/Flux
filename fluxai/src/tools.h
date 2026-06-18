/* tools.h -- lokale KI-Tools fuer fluxaid.
 * Die KI antwortet mit "TOOL:<name>\nARG:<wert>" um ein Tool aufzurufen.
 * Das Ergebnis wird als Kontext in einen zweiten API-Aufruf injiziert.
 * Kein Netzwerk-Roundtrip ausser bei "weather" (wttr.in, kein API-Key).
 */
#ifndef FLUX_TOOLS_H
#define FLUX_TOOLS_H

#include <stddef.h>

/* Fuehrt ein benanntes Tool aus.
 * name: z.B. "weather", "file_read", "calculate", ...
 * arg:  Argument-String (toolspezifisch, siehe unten)
 * out/out_cap: Ergebnis-Puffer
 * Gibt 1 wenn bekanntes Tool, 0 wenn unbekannt. */
int flux_tool_exec(const char *name, const char *arg,
                   char *out, size_t out_cap);

/* Gibt eine kompakte Beschreibung aller Tools zurueck (fuer System-Prompt). */
const char *flux_tools_description(void);

#endif
