/* action.h -- parst einen Aktionsvorschlag der KI ("ACTION:..." in der
 * Antwort von fluxaid, siehe fluxai/src/provider.c System-Prompt) und
 * baut daraus die Bestaetigungs-Anfrage ("X:...", siehe
 * common/flux_protocol.h), die nach Tap auf "Senden" rausgeht.
 *
 * Der Ablauf in main.c: Frage -> Antwort von fluxaid -> falls
 * flux_action_parse() erfolgreich ist, statt der Antwort einen
 * Bestaetigungs-Dialog zeigen (FLUX_SCREEN_CONFIRM) -- die KI fuehrt
 * nie direkt etwas aus.
 */
#ifndef FLUX_ACTION_H
#define FLUX_ACTION_H

#include <stddef.h>

typedef enum {
    FLUX_ACTION_NONE = 0,
    FLUX_ACTION_MAIL,
    FLUX_ACTION_SMS,
    FLUX_ACTION_CALL,
} flux_action_type_t;

typedef struct {
    flux_action_type_t type;
    char to[256];
    char subject[256];
    char body[4096];
} flux_action_t;

const char *flux_action_type_label(flux_action_type_t type);

/* Gibt 1 zurueck, wenn answer ein Aktionsvorschlag war (und befuellt
 * out), sonst 0 (answer ist dann eine normale Textantwort). */
int flux_action_parse(const char *answer, flux_action_t *out);

/* Baut die "X:"-Ausfuehr-Anfrage fuer flux_ipc_send_raw(). */
void flux_action_build_request(const flux_action_t *a, char *out, size_t out_cap);

#endif
