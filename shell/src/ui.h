/* ui.h -- die zwei Bildschirme des Prototyps: Lockscreen und
 * KI-Assistent (Flux hat keinen App-Grid-Homescreen -- der
 * Assistent IST der Homescreen, das ist die eigentliche
 * Abgrenzung zu Android/iOS).
 */
#ifndef FLUX_UI_H
#define FLUX_UI_H

#include "fb.h"

typedef enum { FLUX_SCREEN_LOCK, FLUX_SCREEN_ASSISTANT } flux_screen_t;

void flux_ui_draw_lock(flux_fb_t *fb);
void flux_ui_draw_assistant(flux_fb_t *fb, const char *input, const char *answer, int thinking);

#endif
