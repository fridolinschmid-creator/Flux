#ifndef FLUX_ALARM_H
#define FLUX_ALARM_H

/* Prueft faellige Alarme (/tmp/flux_alarms.txt) und abgelaufene Timer
 * (/tmp/flux_timers.txt). Faellige Eintraege werden nach
 * /tmp/flux_alarm_trigger.txt angehaengt und aus der Quelldatei entfernt.
 * Gibt die Anzahl der ausgeloesten Alarme/Timer zurueck (0 = nichts). */
int flux_alarm_check(void);

#endif
