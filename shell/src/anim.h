/* anim.h -- winzige, abhaengigkeitsfreie Animationshilfen.
 *
 * "Premium-Feeling" entsteht meist nicht durch eine Animations-Bibliothek,
 * sondern durch *Easing*: ein Wert (Position, Groesse, Deckkraft) laeuft
 * nicht linear, sondern weich beschleunigt/abgebremst von A nach B. Diese
 * Header-only-Funktionen liefern genau das -- passend zur bestehenden
 * Frame-Schleife (jeder Frame ruft mit einem Fortschritt t in [0,1] auf).
 *
 * Beispiel (Icon faehrt beim Erscheinen weich auf Endgroesse):
 *   float t = flux_anim_clamp01((now_ms - start_ms) / 250.0f);
 *   int size = (int)(target_size * flux_ease_out_back(t));
 */
#ifndef FLUX_ANIM_H
#define FLUX_ANIM_H

#include <stdint.h>
#include <time.h>

/* Monotone Zeit in Millisekunden -- Basis fuer jede zeitgesteuerte Animation. */
static inline uint64_t flux_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}

static inline float flux_anim_clamp01(float t) {
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

/* Lineare Interpolation a..b mit Faktor t in [0,1]. */
static inline float flux_lerp(float a, float b, float t) { return a + (b - a) * t; }

/* --- Easing-Kurven (alle erwarten t in [0,1], liefern ~[0,1]) ---------- */

/* Weich abbremsend -- der Allrounder fuer "einfahren/erscheinen". */
static inline float flux_ease_out_cubic(float t) {
    float u = 1.0f - flux_anim_clamp01(t);
    return 1.0f - u * u * u;
}

/* Weich beschleunigend -- fuer "verschwinden/wegfahren". */
static inline float flux_ease_in_cubic(float t) {
    t = flux_anim_clamp01(t);
    return t * t * t;
}

/* Beides -- weiches Ein- und Ausschwingen. */
static inline float flux_ease_in_out_cubic(float t) {
    t = flux_anim_clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
}

/* Leichtes Ueberschwingen am Ende -- gibt Icons/Buttons ein "Pop". */
static inline float flux_ease_out_back(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = flux_anim_clamp01(t) - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

/* Pulsieren 0..1..0 (z.B. Aufnahme-Indikator, Akzentpunkt). period_ms = volle Periode. */
static inline float flux_pulse(uint64_t now_ms, uint32_t period_ms) {
    if (period_ms == 0) return 0.0f;
    float phase = (float)(now_ms % period_ms) / (float)period_ms; /* 0..1 */
    /* Dreieckskurve 0->1->0, danach geglaettet. */
    float tri = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
    return flux_ease_in_out_cubic(tri);
}

/* Shimmer: ein wandernder Lichtpunkt 0..1, der zyklisch von links nach
 * rechts laeuft -- fuer "lebendige" Lade-Zustaende (Skeleton/Denke-Chip).
 * Gibt die normierte Position [0,1] des Highlights zur Zeit now_ms. */
static inline float flux_shimmer(uint64_t now_ms, uint32_t period_ms) {
    if (period_ms == 0) return 0.0f;
    return (float)(now_ms % period_ms) / (float)period_ms;
}

/* --- Standard-Bewegungsdauern (Flux Motion System) --------------------
 * Alle Interaktionen bleiben im Fenster 150-350 ms: schnell genug, um
 * direkt zu wirken, lang genug, um "weich" und ruhig zu erscheinen. */
#define FLUX_MOTION_FAST_MS   150   /* Tap-Feedback, Glow              */
#define FLUX_MOTION_BASE_MS   250   /* Standard: Erscheinen/Fade-in    */
#define FLUX_MOTION_SLOW_MS   350   /* Bildschirmwechsel, groesse Karten*/

#endif
