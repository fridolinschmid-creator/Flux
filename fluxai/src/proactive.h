#ifndef FLUX_PROACTIVE_H
#define FLUX_PROACTIVE_H

/* Checks memory.txt and calendar.txt for upcoming events.
 * If something relevant is found, calls the AI and writes a short
 * notification to /tmp/flux_proactive.txt which the lockscreen reads.
 * Runs at most once per hour (guarded by /tmp/flux_proactive_last). */
void flux_proactive_check(const char *api_key, const char *model);

#endif
