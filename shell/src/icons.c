/* icons.c -- siehe icons.h. Rastert Lucide-SVGs mit NanoSVG. */
#include "icons.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* NanoSVG einmal hier als Implementierung einbinden (Single-Header). */
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

/* Gemeinsamer SVG-Rahmen. Wichtig: die Praesentationsattribute stehen auf
 * einer <g>-Gruppe, NICHT auf dem <svg>-Wurzelelement -- NanoSVG erbt
 * stroke/fill nur von <g>/Form-Elementen, nicht vom <svg>-Tag. stroke ist
 * fix weiss; die eigentliche Farbe kommt spaeter ueber die Blit-Maske. */
#define SVG_HEAD \
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">" \
    "<g fill=\"none\" stroke=\"#ffffff\" stroke-width=\"2\" " \
    "stroke-linecap=\"round\" stroke-linejoin=\"round\">"
#define SVG_TAIL "</g></svg>"

/* Nur der Form-Inhalt jedes Lucide-Icons (24x24-Viewbox, currentColor
 * entfaellt -- siehe SVG_HEAD). 1:1 aus dem Lucide-Repo uebernommen. */
static const char *ICON_BODY[FLUX_ICON_COUNT] = {
    [FLUX_ICON_SETTINGS] =
        "<path d=\"M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 "
        "2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 "
        "2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 "
        "2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a"
        "2.34 2.34 0 0 0 3.319-1.915\"/><circle cx=\"12\" cy=\"12\" r=\"3\"/>",
    [FLUX_ICON_FOLDER] =
        "<path d=\"M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 "
        "2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z\"/>",
    [FLUX_ICON_CALENDAR] =
        "<path d=\"M8 2v4\"/><path d=\"M16 2v4\"/><rect width=\"18\" height=\"18\" "
        "x=\"3\" y=\"4\" rx=\"2\"/><path d=\"M3 10h18\"/>",
    [FLUX_ICON_USER] =
        "<path d=\"M19 21v-2a4 4 0 0 0-4-4H9a4 4 0 0 0-4 4v2\"/>"
        "<circle cx=\"12\" cy=\"7\" r=\"4\"/>",
    [FLUX_ICON_MIC] =
        "<path d=\"M12 19v3\"/><path d=\"M19 10v2a7 7 0 0 1-14 0v-2\"/>"
        "<rect x=\"9\" y=\"2\" width=\"6\" height=\"13\" rx=\"3\"/>",
    [FLUX_ICON_COPY] =
        "<rect width=\"14\" height=\"14\" x=\"8\" y=\"8\" rx=\"2\" ry=\"2\"/>"
        "<path d=\"M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2\"/>",
    [FLUX_ICON_CLIPBOARD] =
        "<rect width=\"8\" height=\"4\" x=\"8\" y=\"2\" rx=\"1\" ry=\"1\"/>"
        "<path d=\"M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2\"/>",
    [FLUX_ICON_CHECK] =
        "<path d=\"M20 6 9 17l-5-5\"/>",
    [FLUX_ICON_SEND] =
        "<path d=\"M3.714 3.048a.498.498 0 0 0-.683.627l2.843 7.627a2 2 0 0 1 0 1.396l"
        "-2.842 7.627a.498.498 0 0 0 .682.627l18-8.5a.5.5 0 0 0 0-.904z\"/>"
        "<path d=\"M6 12h16\"/>",
    [FLUX_ICON_SEARCH] =
        "<path d=\"m21 21-4.34-4.34\"/><circle cx=\"11\" cy=\"11\" r=\"8\"/>",
    [FLUX_ICON_WIFI] =
        "<path d=\"M12 20h.01\"/><path d=\"M2 8.82a15 15 0 0 1 20 0\"/>"
        "<path d=\"M5 12.859a10 10 0 0 1 14 0\"/><path d=\"M8.5 16.429a5 5 0 0 1 7 0\"/>",
    [FLUX_ICON_BATTERY] =
        "<path d=\"M22 14v-4\"/><rect x=\"2\" y=\"6\" width=\"16\" height=\"12\" rx=\"2\"/>",
    [FLUX_ICON_LOCK] =
        "<rect width=\"18\" height=\"11\" x=\"3\" y=\"11\" rx=\"2\" ry=\"2\"/>"
        "<path d=\"M7 11V7a5 5 0 0 1 10 0v4\"/>",
    [FLUX_ICON_SPARKLES] =
        "<path d=\"M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 "
        "1.051a1 1 0 0 1 0 1.966l-5.558 1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1"
        "-1.966 0l-1.051-5.558a2 2 0 0 0-1.594-1.594l-5.558-1.051a1 1 0 0 1 0-1.966l5.558"
        "-1.051a2 2 0 0 0 1.594-1.594z\"/><path d=\"M20 2v4\"/><path d=\"M22 4h-4\"/>"
        "<circle cx=\"4\" cy=\"20\" r=\"2\"/>",
    [FLUX_ICON_KEY] =
        "<path d=\"M2.586 17.414A2 2 0 0 0 2 18.828V21a1 1 0 0 0 1 1h3a1 1 0 0 0 1-1v-1a1 "
        "1 0 0 1 1-1h1a1 1 0 0 0 1-1v-1a1 1 0 0 1 1-1h.172a2 2 0 0 0 1.414-.586l.814-.814a"
        "6.5 6.5 0 1 0-4-4z\"/><circle cx=\"16.5\" cy=\"7.5\" r=\".5\" fill=\"#ffffff\"/>",
    [FLUX_ICON_CPU] =
        "<path d=\"M12 20v2\"/><path d=\"M12 2v2\"/><path d=\"M17 20v2\"/><path d=\"M17 2v2\"/>"
        "<path d=\"M2 12h2\"/><path d=\"M2 17h2\"/><path d=\"M2 7h2\"/><path d=\"M20 12h2\"/>"
        "<path d=\"M20 17h2\"/><path d=\"M20 7h2\"/><path d=\"M7 20v2\"/><path d=\"M7 2v2\"/>"
        "<rect x=\"4\" y=\"4\" width=\"16\" height=\"16\" rx=\"2\"/>"
        "<rect x=\"8\" y=\"8\" width=\"8\" height=\"8\" rx=\"1\"/>",
    [FLUX_ICON_MAIL] =
        "<path d=\"m22 7-8.991 5.727a2 2 0 0 1-2.009 0L2 7\"/>"
        "<rect x=\"2\" y=\"4\" width=\"20\" height=\"16\" rx=\"2\"/>",
    [FLUX_ICON_PALETTE] =
        "<path d=\"M12 22a1 1 0 0 1 0-20 10 9 0 0 1 10 9 5 5 0 0 1-5 5h-2.25a1.75 1.75 0 0 "
        "0-1.4 2.8l.3.4a1.75 1.75 0 0 1-1.4 2.8z\"/>"
        "<circle cx=\"13.5\" cy=\"6.5\" r=\".5\" fill=\"#ffffff\"/>"
        "<circle cx=\"17.5\" cy=\"10.5\" r=\".5\" fill=\"#ffffff\"/>"
        "<circle cx=\"6.5\" cy=\"12.5\" r=\".5\" fill=\"#ffffff\"/>"
        "<circle cx=\"8.5\" cy=\"7.5\" r=\".5\" fill=\"#ffffff\"/>",
    [FLUX_ICON_CLOCK] =
        "<circle cx=\"12\" cy=\"12\" r=\"10\"/><path d=\"M12 6v6l4 2\"/>",
    [FLUX_ICON_VOLUME] =
        "<path d=\"M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 "
        "1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 "
        "11 19.298z\"/><path d=\"M16 9a5 5 0 0 1 0 6\"/>"
        "<path d=\"M19.364 18.364a9 9 0 0 0 0-12.728\"/>",
    [FLUX_ICON_BELL_RING] =
        "<path d=\"M10.268 21a2 2 0 0 0 3.464 0\"/>"
        "<path d=\"M22 8c0-2.3-.8-4.3-2-6\"/>"
        "<path d=\"M3.262 15.326A1 1 0 0 0 4 17h16a1 1 0 0 0 .74-1.673C19.41 13.956 18 12.499 "
        "18 8A6 6 0 0 0 6 8c0 4.499-1.411 5.956-2.738 7.326\"/>"
        "<path d=\"M4 2C2.8 3.7 2 5.7 2 8\"/>",
    [FLUX_ICON_PHONE] =
        "<path d=\"M13.832 16.568a1 1 0 0 0 1.213-.303l.355-.465A2 2 0 0 1 17 15h3a2 2 0 0 1 2 "
        "2v3a2 2 0 0 1-2 2A18 18 0 0 1 2 4a2 2 0 0 1 2-2h3a2 2 0 0 1 2 2v3a2 2 0 0 1-.8 1.6l"
        "-.468.351a1 1 0 0 0-.292 1.233 14 14 0 0 0 6.392 6.384\"/>",
    [FLUX_ICON_PHONE_OFF] =
        "<path d=\"M10.1 13.9a14 14 0 0 0 3.732 2.668 1 1 0 0 0 1.213-.303l.355-.465A2 2 0 0 1 "
        "17 15h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2 18 18 0 0 1-12.728-5.272\"/>"
        "<path d=\"M22 2 2 22\"/>"
        "<path d=\"M4.76 13.582A18 18 0 0 1 2 4a2 2 0 0 1 2-2h3a2 2 0 0 1 2 2v3a2 2 0 0 1-.8 "
        "1.6l-.468.351a1 1 0 0 0-.292 1.233 14 14 0 0 0 .244.473\"/>",
    [FLUX_ICON_PHONE_CALL] =
        "<path d=\"M13 2a9 9 0 0 1 9 9\"/><path d=\"M13 6a5 5 0 0 1 5 5\"/>"
        "<path d=\"M13.832 16.568a1 1 0 0 0 1.213-.303l.355-.465A2 2 0 0 1 17 15h3a2 2 0 0 1 2 "
        "2v3a2 2 0 0 1-2 2A18 18 0 0 1 2 4a2 2 0 0 1 2-2h3a2 2 0 0 1 2 2v3a2 2 0 0 1-.8 1.6l"
        "-.468.351a1 1 0 0 0-.292 1.233 14 14 0 0 0 6.392 6.384\"/>",
    [FLUX_ICON_CHEVRON_LEFT] =
        "<path d=\"m15 18-6-6 6-6\"/>",
    [FLUX_ICON_CHEVRON_RIGHT] =
        "<path d=\"m9 18 6-6-6-6\"/>",
    [FLUX_ICON_PLUS] =
        "<path d=\"M5 12h14\"/><path d=\"M12 5v14\"/>",
};

/* --- Raster-Cache: pro (Icon,Groesse) ein fertiger RGBA-Puffer ---------- */
typedef struct { int id; int size; uint8_t *rgba; } icon_entry_t;
#define CACHE_MAX 128
static icon_entry_t   s_cache[CACHE_MAX];
static int            s_cache_n = 0;
static NSVGrasterizer *s_rast   = NULL;

static uint8_t *rasterize(int id, int size) {
    if (!s_rast) {
        s_rast = nsvgCreateRasterizer();
        if (!s_rast) return NULL;
    }
    if (!ICON_BODY[id]) return NULL;

    /* nsvgParse veraendert den Puffer in-place -> lokale, beschreibbare Kopie. */
    char buf[2048];
    int n = snprintf(buf, sizeof(buf), "%s%s%s", SVG_HEAD, ICON_BODY[id], SVG_TAIL);
    if (n < 0 || n >= (int)sizeof(buf)) return NULL;

    NSVGimage *img = nsvgParse(buf, "px", 96.0f);
    if (!img) return NULL;

    uint8_t *rgba = calloc((size_t)size * size, 4);
    if (rgba) {
        float scale = (float)size / 24.0f;   /* Lucide-Viewbox ist 24x24 */
        nsvgRasterize(s_rast, img, 0, 0, scale, rgba, size, size, size * 4);
    }
    nsvgDelete(img);
    return rgba;
}

static const uint8_t *get_icon(int id, int size) {
    for (int i = 0; i < s_cache_n; i++)
        if (s_cache[i].id == id && s_cache[i].size == size)
            return s_cache[i].rgba;

    uint8_t *r = rasterize(id, size);
    if (!r) return NULL;

    if (s_cache_n < CACHE_MAX) {
        s_cache[s_cache_n].id = id;
        s_cache[s_cache_n].size = size;
        s_cache[s_cache_n].rgba = r;
        s_cache_n++;
    } else {
        /* Cache voll: aeltesten Eintrag (Index 0) verdraengen. */
        free(s_cache[0].rgba);
        memmove(&s_cache[0], &s_cache[1], sizeof(icon_entry_t) * (CACHE_MAX - 1));
        s_cache[CACHE_MAX - 1].id = id;
        s_cache[CACHE_MAX - 1].size = size;
        s_cache[CACHE_MAX - 1].rgba = r;
    }
    return r;
}

void flux_icon_draw(flux_fb_t *fb, flux_icon_t id, int cx, int cy, int size, uint32_t rgb) {
    if (id < 0 || id >= FLUX_ICON_COUNT || size <= 0) return;
    const uint8_t *rgba = get_icon((int)id, size);
    if (!rgba) return;
    flux_fb_blit_mask(fb, cx - size / 2, cy - size / 2, size, size, rgba, rgb);
}

void flux_icon_cleanup(void) {
    for (int i = 0; i < s_cache_n; i++) free(s_cache[i].rgba);
    s_cache_n = 0;
    if (s_rast) { nsvgDeleteRasterizer(s_rast); s_rast = NULL; }
}
