#ifndef FLUX_JOURNAL_H
#define FLUX_JOURNAL_H

/* Generates a daily journal entry based on today's conversations,
 * calendar events, and memory. Writes to /home/user/Journal/YYYY-MM-DD.md.
 * Runs at most once per day, between 22:00 and 23:59.
 * Called from fluxaid's main loop alongside proactive checks. */
void flux_journal_check(const char *api_key, const char *model);

#endif
