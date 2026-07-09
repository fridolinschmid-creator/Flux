/* weather_anim.h -- animiertes Wetter-Widget fuer den Assistant-Screen.
 *
 * Zeigt den Wetterzustand aus classify_weather() nicht mehr nur als
 * statisches Icon, sondern als kleine, ruhige Endlos-Animation (fallende
 * Regentropfen unter einer Wolke, pulsierende Sonnenstrahlen, driftende
 * Schneeflocken). Bewusst auf ein festes Rechteck begrenzt: main.c ruft
 * flux_weather_anim_draw() in einer eigenen Zeitschleife auf, waehrend der
 * Rest des Screens unveraendert bleibt -- flux_fb_present()'s Zeilen-Diff
 * (siehe fb.c) bleibt dadurch billig, weil nur die Widget-Zeilen dirty
 * werden statt des ganzen Screens.
 */
#ifndef FLUX_WEATHER_ANIM_H
#define FLUX_WEATHER_ANIM_H

#include "fb.h"
#include <stdint.h>

typedef enum {
    WCOND_SUNNY = 0, WCOND_PARTLY_CLOUDY, WCOND_CLOUDY,
    WCOND_RAINY, WCOND_SNOWY, WCOND_STORMY, WCOND_FOGGY, WCOND_UNKNOWN,
} weather_cond_t;

/* Ordnet einen freien Wetterbeschreibungstext (z.B. von wttr.in) einem
 * groben Zustand zu (Schlagwort-Suche, Deutsch+Englisch). */
weather_cond_t flux_weather_classify(const char *desc);

/* Zeichnet die Animation fuer `cond` in das Rechteck (x,y,w,h). now_ms
 * treibt die Bewegung (flux_now_ms()). UNKNOWN zeichnet bewusst nichts
 * Erfundenes -- nur eine neutrale Platzhalter-Wolke (Ehrlichkeitsprinzip:
 * kein Fake-Wetter ohne Daten). */
void flux_weather_anim_draw(flux_fb_t *fb, int x, int y, int w, int h,
                             weather_cond_t cond, uint64_t now_ms);

/* True, wenn `cond` eine laufende Animation braucht (fuer main.c: nur dann
 * lohnt sich der schnellere Redraw-Takt). SUNNY/RAINY/SNOWY/STORMY/CLOUDY/
 * FOGGY animieren, UNKNOWN nicht. */
int flux_weather_anim_active(weather_cond_t cond);

#endif
