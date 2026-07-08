/* tools.h -- lokale KI-Tools fuer fluxaid.
 * Die KI ruft Tools ueber das native Tool-Use der jeweiligen API auf
 * (Anthropic tool_use bzw. OpenAI tool_calls). Jedes Tool nimmt genau ein
 * String-Argument "arg" entgegen; das Ergebnis wird als tool_result/tool-
 * Nachricht in den naechsten API-Aufruf zurueckgegeben.
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

/* Gibt eine kompakte Beschreibung aller Tools zurueck (fuer System-Prompt).
 * Wird seit der Umstellung auf natives Tool-Calling nicht mehr in den
 * System-Prompt eingebettet, bleibt aber als Referenz/Fallback erhalten. */
const char *flux_tools_description(void);

/* Liefert die Tools als JSON-Array im Anthropic-Format:
 *   [{"name":"...","description":"...","input_schema":{...}}, ...]
 * Jedes Tool nimmt genau ein String-Argument "arg" entgegen.
 * Der Rueckgabewert zeigt auf einen statischen Puffer (nicht freigeben).
 * Wird fuer den nativen Tool-Use der Provider verwendet (provider.c). */
const char *flux_tools_json_schema(void);

/* Wie flux_tools_json_schema(), aber im OpenAI-Function-Format:
 *   [{"type":"function","function":{"name","description","parameters":{...}}}, ...]
 * Fuer DeepSeek/NVIDIA (FMT_OPENAI). Statischer Puffer (nicht freigeben). */
const char *flux_tools_openai_schema(void);

#endif
