/* weather_anim.c -- siehe weather_anim.h.
 *
 * Alle Partikelpositionen sind reine Funktionen von (now_ms, Index) --
 * kein Malloc, kein persistenter Zustand, kein Init/Deinit noetig. Das
 * haelt main.c einfach: es kann flux_weather_anim_draw() jederzeit mit der
 * aktuellen Zeit aufrufen, ohne vorher etwas anzulegen oder aufzuraeumen.
 */
#include "weather_anim.h"
#include "anim.h"

#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"

#include <string.h>
#include <math.h>

#define RAIN_DROPS   7
#define SNOW_FLAKES  9

weather_cond_t flux_weather_classify(const char *desc) {
    char low[256];
    int i;
    for (i = 0; desc[i] && i < 255; i++)
        low[i] = (desc[i] >= 'A' && desc[i] <= 'Z') ? desc[i] + 32 : desc[i];
    low[i] = '\0';
    if (strstr(low, "thunder") || strstr(low, "storm") || strstr(low, "gewitter"))
        return WCOND_STORMY;
    if (strstr(low, "snow")   || strstr(low, "sleet") || strstr(low, "schnee"))
        return WCOND_SNOWY;
    if (strstr(low, "rain")   || strstr(low, "drizzle") || strstr(low, "regen"))
        return WCOND_RAINY;
    if (strstr(low, "fog")    || strstr(low, "mist") || strstr(low, "nebel"))
        return WCOND_FOGGY;
    if (strstr(low, "overcast") || strstr(low, "bedeckt"))
        return WCOND_CLOUDY;
    if (strstr(low, "cloud") || strstr(low, "wolke") || strstr(low, "partly"))
        return WCOND_PARTLY_CLOUDY;
    if (strstr(low, "sun") || strstr(low, "clear") || strstr(low, "sonn") || strstr(low, "klar"))
        return WCOND_SUNNY;
    return WCOND_UNKNOWN;
}

int flux_weather_anim_active(weather_cond_t cond) {
    return cond != WCOND_UNKNOWN;
}

/* Wolkenkoerper: abgerundeter Rumpf + zwei "Buckel" oben, mittig im oberen
 * Drittel des Widgets. Gemeinsame Basis fuer alle bewoelkten Zustaende. */
static void draw_cloud(flux_fb_t *fb, int x, int y, int w, int cloud_h, uint32_t col) {
    int body_y = y + cloud_h / 3;
    flux_fb_fill_rect_rounded(fb, x, body_y, w, cloud_h - cloud_h / 3, (cloud_h - cloud_h/3) / 2, col);
    flux_fb_fill_circle(fb, x + w / 3,     body_y + 2, cloud_h / 3, col);
    flux_fb_fill_circle(fb, x + w * 2 / 3, body_y,     cloud_h / 3 + 2, col);
}

static void draw_rain(flux_fb_t *fb, int x, int y, int w, int h, uint64_t now_ms,
                      uint32_t cloud_col, int n_drops, int fast) {
    int cloud_h = h * 2 / 5;
    draw_cloud(fb, x, y, w, cloud_h, cloud_col);

    int drop_top = y + cloud_h - 2;
    int drop_range = h - cloud_h + 2;
    uint32_t period = fast ? 550 : 850;
    for (int i = 0; i < n_drops; i++) {
        float xfrac = (i + 0.5f) / n_drops;
        float seed = (float)((i * 37) % 100) / 100.0f;
        float t = (float)((now_ms / period + (uint64_t)(seed * 1000)) % 1000) / 1000.0f;
        int dy = drop_top + (int)(t * drop_range);
        int dx = x + (int)(xfrac * w);
        int drop_h = 7;
        /* Deckkraft nimmt zum Ende hin ab -- wirkt wie ein Auftreffen statt
         * eines harten Verschwindens am Rand. */
        uint8_t alpha = (t > 0.7f) ? (uint8_t)(255.0f * (1.0f - t) / 0.3f) : 255;
        flux_fb_blend_rect(fb, dx, dy, 2, drop_h, 0x5599DD, alpha);
    }
}

static void draw_sun(flux_fb_t *fb, int x, int y, int w, int h, uint64_t now_ms) {
    int cx = x + w / 2, cy = y + h / 2;
    int r = (h < w ? h : w) / 5;
    float pulse = flux_pulse(now_ms, 2600);
    int ray_len = r + (int)(pulse * (r / 2));
    uint32_t sun = 0xFFCC33;
    for (int k = 0; k < 8; k++) {
        double a = k * 3.14159265 / 4.0;
        int x0 = cx + (int)(cos(a) * (r + 3));
        int y0 = cy + (int)(sin(a) * (r + 3));
        int x1 = cx + (int)(cos(a) * (r + 3 + ray_len));
        int y1 = cy + (int)(sin(a) * (r + 3 + ray_len));
        int steps = 8;
        for (int s = 0; s <= steps; s++) {
            float f = (float)s / steps;
            int px = x0 + (int)((x1 - x0) * f);
            int py = y0 + (int)((y1 - y0) * f);
            flux_fb_fill_rect(fb, px - 1, py - 1, 2, 2, sun);
        }
    }
    flux_fb_fill_circle(fb, cx, cy, r, sun);
}

static void draw_snow(flux_fb_t *fb, int x, int y, int w, int h, uint64_t now_ms,
                      uint32_t cloud_col) {
    int cloud_h = h * 2 / 5;
    draw_cloud(fb, x, y, w, cloud_h, cloud_col);

    int fall_top = y + cloud_h - 2;
    int fall_range = h - cloud_h + 2;
    for (int i = 0; i < SNOW_FLAKES; i++) {
        float xfrac = (i + 0.5f) / SNOW_FLAKES;
        /* Modulo 977 (statt z.B. 1000/N) sorgt bei kleinem N fuer echte
         * Durchmischung statt einer Reihe monoton wachsender Werte, die
         * sonst mit xfrac mitliefe und alle Flocken auf eine sichtbare
         * Diagonale zwaenge. */
        float seed = (float)((i * 173 + 91) % 977) / 977.0f;
        float t = (float)((now_ms / 1800 + (uint64_t)(seed * 1000)) % 1000) / 1000.0f;
        float sway = stb_perlin_noise3((float)i * 3.1f, (float)now_ms * 0.0006f, 0.0f, 0, 0, 0);
        int fx = x + (int)(xfrac * w) + (int)(sway * 6.0f);
        int fy = fall_top + (int)(t * fall_range);
        flux_fb_fill_circle(fb, fx, fy, 2, 0xEAF3FF);
    }
}

static void draw_cloud_drift(flux_fb_t *fb, int x, int y, int w, int h, uint64_t now_ms,
                             uint32_t col) {
    int cloud_h = h * 2 / 5;
    float drift = stb_perlin_noise3((float)now_ms * 0.0003f, 0.0f, 0.0f, 0, 0, 0);
    draw_cloud(fb, x + (int)(drift * 8.0f), y, w, cloud_h, col);
    /* Sanfte Nebel-/Bedeckt-Streifen darunter, leicht schwebend. */
    for (int i = 0; i < 3; i++) {
        int by = y + cloud_h + 6 + i * 8;
        float band = stb_perlin_noise3((float)i * 5.0f, (float)now_ms * 0.0004f, 0.0f, 0, 0, 0);
        int bx = x + 4 + (int)(band * 6.0f);
        int bw = w - 8 - (i % 2) * 10;
        if (bw > 0) flux_fb_blend_rect(fb, bx, by, bw, 3, col, 140);
    }
}

void flux_weather_anim_draw(flux_fb_t *fb, int x, int y, int w, int h,
                             weather_cond_t cond, uint64_t now_ms) {
    switch (cond) {
    case WCOND_SUNNY:
        draw_sun(fb, x, y, w, h, now_ms);
        break;
    case WCOND_PARTLY_CLOUDY:
        draw_sun(fb, x, y, w, h, now_ms);
        draw_cloud(fb, x, y + h / 4, w * 3 / 4, h * 2 / 5, 0xA8B8C8);
        break;
    case WCOND_CLOUDY:
        draw_cloud_drift(fb, x, y, w, h, now_ms, 0xA8B8C8);
        break;
    case WCOND_FOGGY:
        draw_cloud_drift(fb, x, y, w, h, now_ms, 0x889AA8);
        break;
    case WCOND_RAINY:
        draw_rain(fb, x, y, w, h, now_ms, 0x8393A8, RAIN_DROPS, 0);
        break;
    case WCOND_SNOWY:
        draw_snow(fb, x, y, w, h, now_ms, 0x8393A8);
        break;
    case WCOND_STORMY: {
        draw_rain(fb, x, y, w, h, now_ms, 0x51606E, RAIN_DROPS + 2, 1);
        /* Kurzer Blitz alle ~3.4s statt eines Dauerzustands -- wirkt
         * lebendig statt aufdringlich. */
        uint64_t phase = now_ms % 3400;
        if (phase < 120) {
            int cx = x + w / 2;
            int cy = y + h * 3 / 5;
            flux_fb_fill_rect(fb, cx - 3, cy,      6, 10, 0xFFE066);
            flux_fb_fill_rect(fb, cx - 8, cy + 10, 10, 6,  0xFFE066);
            flux_fb_fill_rect(fb, cx,     cy + 14, 6,  10, 0xFFE066);
        }
        break;
    }
    case WCOND_UNKNOWN:
    default:
        /* Keine Daten -- keine erfundene Animation zeigen (Ehrlichkeitsprinzip). */
        flux_fb_draw_ring(fb, x + w / 2, y + h / 2, (h < w ? h : w) / 4, 2, 0x2D3748);
        break;
    }
}
