/* notification.h -- Hintergrund-Benachrichtigungsdienst fuer Flux OS.
 *
 * Laeuft als pthread in fluxaid. Prueft alle 15 Minuten Kalender,
 * Memory, Batterie und Wetter und schreibt ausstehende Hinweise
 * nach /tmp/flux_notifications.txt (max. 10 Eintraege, neueste zuerst).
 *
 * Ehrlich: ohne Kalender-/Memory-Daten gibt es keine Hinweise.
 */
#ifndef FLUX_NOTIFICATION_H
#define FLUX_NOTIFICATION_H

/* Startet den Benachrichtigungs-Thread. */
void flux_notification_start(void);

/* Schreibt sofort eine Benachrichtigung (fuer Tests / direkte Trigger). */
void flux_notification_push(const char *msg);

/* Liest die aktuelle Anzahl ungelesener Benachrichtigungen. */
int flux_notification_count(void);

#define FLUX_NOTIF_FILE "/tmp/flux_notifications.txt"

#endif /* FLUX_NOTIFICATION_H */
