#ifndef FLUX_PROACTIVE_H
#define FLUX_PROACTIVE_H

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

#endif
