#ifndef FLUX_HABITS_H
#define FLUX_HABITS_H

/* Logs a screen visit with timestamp to /etc/flux/habits.txt.
 * Called from fluxaid whenever a Q&A happens.
 * screen_name: e.g. "assistant", "calendar", "contacts", "files" */
void flux_habits_log(const char *screen_name, const char *topic);

/* Generates a personalised morning briefing (07:00-09:00, once per day).
 * Reads habits.txt + calendar.txt + memory.txt and asks the AI for a
 * warm, helpful "Guten Morgen" message (2-3 sentences).
 * Writes to /tmp/flux_proactive.txt (shared with proactive.c). */
void flux_habits_morning_briefing(const char *api_key, const char *model);

/* Returns 1 if the morning briefing should run now. */
int flux_habits_should_brief(void);

#endif
