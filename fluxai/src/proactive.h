#ifndef FLUX_PROACTIVE_H
#define FLUX_PROACTIVE_H

#include <stddef.h>

/* Checks memory.txt and calendar.txt for upcoming events.
 * If something relevant is found, calls the AI and writes a short
 * notification to /tmp/flux_proactive.txt which the lockscreen reads.
 * Runs at most once per hour (guarded by /tmp/flux_proactive_last). */
void flux_proactive_check(const char *api_key, const char *model);

/* Liest Akkustand (0-100) und Ladezustand (*charging = 1 wenn ladend/voll).
 * Gibt 1 zurueck, wenn ein Akku-Sensor lesbar war, sonst 0. */
int flux_proactive_read_battery(int *pct, int *charging);

/* Erkennt grobe Schlechtwetter-Stichworte (Regen/Schnee/Gewitter) im
 * Wetter-String und gibt einen kurzen Hinweis zurueck, sonst NULL. */
const char *flux_proactive_weather_hint(const char *weather);

/* Sammelt alle Kalenderzeilen aus /etc/flux/calendar.txt, die mit "today"
 * (Format "YYYY-MM-DD") beginnen, als "- <Rest der Zeile>\n" nach out.
 * out ist bei leerem Ergebnis "" (kein Termin oder Datei fehlt). */
void flux_calendar_today(const char *today, char *out, size_t out_cap);

/* Liest die erste Zeile aus /tmp/flux_weather.txt (getrimmt) nach out.
 * out ist "" wenn die Cache-Datei fehlt oder leer ist. */
void flux_weather_read(char *out, size_t out_cap);

#endif
