/* ui.h -- die zwei Bildschirme des Prototyps: Lockscreen und
 * KI-Assistent (Flux hat keinen App-Grid-Homescreen -- der
 * Assistent IST der Homescreen, das ist die eigentliche
 * Abgrenzung zu Android/iOS).
 *
 * Touch-first: der Assistent zeichnet eine eigene Bildschirm-
 * tastatur, weil ein Telefon nicht von einer Hardware-Tastatur
 * ausgehen darf (siehe input.h). flux_ui_kbd_hit() ist die
 * einzige Quelle der Wahrheit fuer "wo ist welche Taste" -- main.c
 * nutzt sie, um Tap-Koordinaten in Tasten umzurechnen.
 */
#ifndef FLUX_UI_H
#define FLUX_UI_H

#include "fb.h"

typedef enum { FLUX_SCREEN_LOCK, FLUX_SCREEN_ASSISTANT } flux_screen_t;

void flux_ui_draw_lock(flux_fb_t *fb);
void flux_ui_draw_assistant(flux_fb_t *fb, const char *input, const char *answer, int thinking);

/* Y-Koordinate, ab der die Bildschirmtastatur beginnt -- braucht
 * main.c nicht, aber draw_assistant nutzt es, um den Antwortbereich
 * nicht unter der Tastatur verschwinden zu lassen. Oeffentlich, falls
 * spaeter weitere Bildschirme dieselbe Tastatur einbetten. */
int flux_ui_kbd_top(const flux_fb_t *fb);

/* Bildschirmkoordinate (x, y) -> Taste, falls innerhalb der
 * Bildschirmtastatur. Gibt 1 bei Treffer, sonst 0. *out_ch ist bei
 * Sondertasten (Backspace/Enter) undefiniert -- die Flags entscheiden. */
int flux_ui_kbd_hit(const flux_fb_t *fb, int x, int y,
                     char *out_ch, int *out_backspace, int *out_enter);

#endif
