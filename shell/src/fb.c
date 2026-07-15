#include "fb.h"

/* Echtes TrueType-Rendering statt des frueheren Bitmap-Fonts
 * (stb_easy_font sah "nach Code"/Terminal aus). stb_truetype rastert
 * eine moderne Sans-Serif (Instrument Sans, SIL OFL) -- passt zur
 * framebufferbasierten, GPU-losen Architektur: jedes Glyph wird einmal
 * pro (Codepoint, Pixelgroesse) gerastert, gecacht und alpha-geblittet. */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font_data.h"   /* eingebettete TTF-Bytes: flux_font_ttf[] */

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#if defined(__linux__)
#include <linux/fb.h>
#endif

int flux_fb_open(flux_fb_t *fb, const char *device) {
#if !defined(__linux__)
    (void)fb;
    (void)device;
    return -1;
#else
    memset(fb, 0, sizeof(*fb));
    fb->fd = open(device, O_RDWR);
    if (fb->fd < 0)
        return -1;

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(fb->fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        close(fb->fd);
        return -1;
    }

    fb->width     = vinfo.xres;
    fb->height    = vinfo.yres;
    fb->bpp       = vinfo.bits_per_pixel;
    fb->stride_px = finfo.line_length / (fb->bpp / 8);
    fb->screensize = (size_t)finfo.line_length * vinfo.yres;

    fb->mmio = mmap(NULL, fb->screensize, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fb->fd, 0);
    if (fb->mmio == MAP_FAILED) {
        close(fb->fd);
        return -1;
    }

    size_t pixels = (size_t)fb->stride_px * fb->height;
    fb->back = calloc(pixels, sizeof(uint32_t));
    fb->prev = calloc(pixels, sizeof(uint32_t));
    if (!fb->back || !fb->prev) {
        flux_fb_close(fb);
        return -1;
    }
    /* prev mit ungueltigem Wert vorbelegen -> erster present() zeichnet alles */
    memset(fb->prev, 0xFF, pixels * sizeof(uint32_t));
    return 0;
#endif
}

void flux_fb_close(flux_fb_t *fb) {
    if (fb->back) free(fb->back);
    if (fb->prev) free(fb->prev);
    if (fb->mmio && fb->mmio != MAP_FAILED) munmap(fb->mmio, fb->screensize);
    if (fb->fd >= 0) close(fb->fd);
}

/* Mock-Framebuffer fuer Host-Tests / Screenshots ohne /dev/fb0. */
int flux_fb_open_null(flux_fb_t *fb, int width, int height) {
    memset(fb, 0, sizeof(*fb));
    fb->fd       = -1;
    fb->mmio     = NULL;
    fb->width    = width;
    fb->height   = height;
    fb->bpp      = 32;
    fb->stride_px = width;
    fb->screensize = (size_t)width * height * 4;
    size_t pixels = (size_t)width * height;
    fb->back = calloc(pixels, sizeof(uint32_t));
    fb->prev = calloc(pixels, sizeof(uint32_t));
    if (!fb->back || !fb->prev) { flux_fb_close(fb); return -1; }
    memset(fb->prev, 0xFF, pixels * sizeof(uint32_t));
    return 0;
}

static inline void put_back(flux_fb_t *fb, int x, int y, uint32_t rgb) {
    if ((unsigned)x >= (unsigned)fb->width || (unsigned)y >= (unsigned)fb->height)
        return;
    fb->back[y * fb->stride_px + x] = rgb;
}

void flux_fb_set_px(flux_fb_t *fb, int x, int y, uint32_t rgb) {
    put_back(fb, x, y, rgb);
}

void flux_fb_clear(flux_fb_t *fb, uint32_t rgb) {
    flux_fb_fill_rect(fb, 0, 0, fb->width, fb->height, rgb);
}

void flux_fb_fill_rect(flux_fb_t *fb, int x, int y, int w, int h, uint32_t rgb) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            put_back(fb, i, j, rgb);
}

static inline uint8_t blend8(uint8_t bg, uint8_t fg, uint8_t alpha) {
    return (uint8_t)((bg * (255 - alpha) + fg * alpha) / 255);
}

void flux_fb_blend_rect(flux_fb_t *fb, int x, int y, int w, int h, uint32_t rgb, uint8_t alpha) {
    uint8_t fr = (rgb >> 16) & 0xFF, fg = (rgb >> 8) & 0xFF, fbb = rgb & 0xFF;
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if ((unsigned)i >= (unsigned)fb->width || (unsigned)j >= (unsigned)fb->height)
                continue;
            uint32_t bg = fb->back[j * fb->stride_px + i];
            uint8_t br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
            uint32_t out = (blend8(br, fr, alpha) << 16) |
                           (blend8(bgc, fg, alpha) << 8) |
                            blend8(bb, fbb, alpha);
            fb->back[j * fb->stride_px + i] = out;
        }
    }
}

void flux_fb_blit_rgba(flux_fb_t *fb, int x, int y, int w, int h, const uint8_t *rgba) {
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if ((unsigned)py >= (unsigned)fb->height) continue;
        for (int i = 0; i < w; i++) {
            int px = x + i;
            if ((unsigned)px >= (unsigned)fb->width) continue;
            const uint8_t *s = rgba + ((size_t)j * w + i) * 4;
            uint8_t a = s[3];
            if (a == 0) continue;
            uint32_t bg = fb->back[py * fb->stride_px + px];
            uint8_t br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
            uint32_t out = (blend8(br, s[0], a) << 16) |
                           (blend8(bgc, s[1], a) << 8) |
                            blend8(bb, s[2], a);
            fb->back[py * fb->stride_px + px] = out;
        }
    }
}

void flux_fb_blit_mask(flux_fb_t *fb, int x, int y, int w, int h, const uint8_t *rgba, uint32_t tint) {
    uint8_t tr = (tint >> 16) & 0xFF, tg = (tint >> 8) & 0xFF, tb = tint & 0xFF;
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if ((unsigned)py >= (unsigned)fb->height) continue;
        for (int i = 0; i < w; i++) {
            int px = x + i;
            if ((unsigned)px >= (unsigned)fb->width) continue;
            uint8_t a = rgba[(((size_t)j * w + i) * 4) + 3];
            if (a == 0) continue;
            uint32_t bg = fb->back[py * fb->stride_px + px];
            uint8_t br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
            uint32_t out = (blend8(br, tr, a) << 16) |
                           (blend8(bgc, tg, a) << 8) |
                            blend8(bb, tb, a);
            fb->back[py * fb->stride_px + px] = out;
        }
    }
}

/* ---- TrueType-Textrendering (Instrument Sans, eingebettet) -----------
 * y ist weiterhin die OBERKANTE der Textzeile (wie beim alten Bitmap-Font),
 * `scale` bleibt die gewohnte Stufe (2 = Fliesstext, 3 = Titel, ...). So
 * funktioniert das gesamte vorhandene Layout (zentriert ueber
 * flux_fb_text_width) unveraendert weiter -- nur die Glyphen sind jetzt
 * echte, proportionale Sans-Serif-Buchstaben statt Strich-Quads. */

static stbtt_fontinfo g_font;
static int            g_font_ready = -1;  /* -1 uninit, 0 fehlgeschlagen, 1 ok */

static void font_init(void) {
    if (g_font_ready >= 0) return;
    g_font_ready = stbtt_InitFont(&g_font, flux_font_ttf,
                                  stbtt_GetFontOffsetForIndex(flux_font_ttf, 0)) ? 1 : 0;
}

/* scale-Stufe -> Pixelhoehe. Der alte Bitmap-Font war ~7px je Stufe; eine
 * Proportionalschrift wirkt bei gleicher Boxhoehe etwas kleiner, daher ein
 * leicht groesserer Faktor -- empirisch an den Screenshots abgestimmt. */
static float font_px_for_scale(int scale) {
    return (float)scale * 8.0f;
}

/* Glyph-Cache: pro (Codepoint, Pixelhoehe) eine 8-bit-Alpha-Maske. */
typedef struct {
    int cp, px, w, h, xoff, yoff, adv;
    unsigned char *bmp;
} glyph_entry_t;
#define GLYPH_CACHE_MAX 512
static glyph_entry_t g_glyphs[GLYPH_CACHE_MAX];
static int           g_glyph_n = 0;

static glyph_entry_t *glyph_get(int cp, int px) {
    for (int i = 0; i < g_glyph_n; i++)
        if (g_glyphs[i].cp == cp && g_glyphs[i].px == px) return &g_glyphs[i];
    if (!g_font_ready) return NULL;

    float sf = stbtt_ScaleForPixelHeight(&g_font, (float)px);
    int adv, lsb; stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&g_font, cp, sf, sf, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    unsigned char *bmp = NULL;
    if (w > 0 && h > 0) {
        bmp = malloc((size_t)w * h);
        if (bmp) stbtt_MakeCodepointBitmap(&g_font, bmp, w, h, w, sf, sf, cp);
        else { w = h = 0; }
    }

    glyph_entry_t *slot;
    if (g_glyph_n < GLYPH_CACHE_MAX) {
        slot = &g_glyphs[g_glyph_n++];
    } else {
        free(g_glyphs[0].bmp);
        memmove(&g_glyphs[0], &g_glyphs[1], sizeof(glyph_entry_t) * (GLYPH_CACHE_MAX - 1));
        slot = &g_glyphs[GLYPH_CACHE_MAX - 1];
    }
    slot->cp = cp; slot->px = px; slot->w = w; slot->h = h;
    slot->xoff = x0; slot->yoff = y0;
    slot->adv = (int)(adv * sf + 0.5f);
    slot->bmp = bmp;
    return slot;
}

/* UTF-8 -> Unicode-Codepoint; gibt Anzahl verbrauchter Bytes zurueck.
 * Echte Umlaute/ß werden jetzt direkt als Glyph gerendert (kein Punkte-Hack). */
static int utf8_next(const unsigned char *p, int *cp) {
    if (p[0] < 0x80) { *cp = p[0]; return 1; }
    if ((p[0] & 0xE0) == 0xC0 && p[1]) {
        *cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); return 2;
    }
    if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
        *cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); return 3;
    }
    if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
        *cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
              ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); return 4;
    }
    *cp = '?'; return 1;
}

/* Blittet eine 8-bit-Alpha-Glyphmaske eingefaerbt in den Backbuffer. */
static void blit_glyph(flux_fb_t *fb, const glyph_entry_t *g, int gx, int gy, uint32_t rgb) {
    if (!g->bmp) return;
    int tr = (rgb >> 16) & 0xff, tg = (rgb >> 8) & 0xff, tb = rgb & 0xff;
    for (int j = 0; j < g->h; j++) {
        int py = gy + j;
        if (py < 0 || py >= fb->height) continue;
        for (int i = 0; i < g->w; i++) {
            int a = g->bmp[j * g->w + i];
            if (!a) continue;
            int pxp = gx + i;
            if (pxp < 0 || pxp >= fb->width) continue;
            uint32_t bg = fb->back[py * fb->stride_px + pxp];
            int br = (bg >> 16) & 0xff, bgc = (bg >> 8) & 0xff, bb = bg & 0xff;
            int rr = (tr * a + br  * (255 - a)) / 255;
            int rg = (tg * a + bgc * (255 - a)) / 255;
            int rb = (tb * a + bb  * (255 - a)) / 255;
            fb->back[py * fb->stride_px + pxp] = ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
        }
    }
}

void flux_fb_text_px(flux_fb_t *fb, int x, int y, const char *s, uint32_t rgb, int px) {
    font_init();
    if (!g_font_ready || !s) return;
    float sf = stbtt_ScaleForPixelHeight(&g_font, (float)px);
    int asc, desc, gap; stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
    int baseline = y + (int)(asc * sf + 0.5f);

    float pen = (float)x;
    const unsigned char *p = (const unsigned char *)s;
    int prev = 0;
    while (*p) {
        int cp; p += utf8_next(p, &cp);
        glyph_entry_t *g = glyph_get(cp, px);
        if (!g) continue;
        if (prev) pen += stbtt_GetCodepointKernAdvance(&g_font, prev, cp) * sf;
        blit_glyph(fb, g, (int)(pen + 0.5f) + g->xoff, baseline + g->yoff, rgb);
        pen += g->adv;
        prev = cp;
    }
}

int flux_fb_text_width_px(const char *s, int px) {
    font_init();
    if (!g_font_ready || !s) return 0;
    float sf = stbtt_ScaleForPixelHeight(&g_font, (float)px);
    float w = 0.0f;
    const unsigned char *p = (const unsigned char *)s;
    int prev = 0;
    while (*p) {
        int cp; p += utf8_next(p, &cp);
        int adv, lsb; stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);
        if (prev) w += stbtt_GetCodepointKernAdvance(&g_font, prev, cp) * sf;
        w += adv * sf;
        prev = cp;
    }
    return (int)(w + 0.5f);
}

/* Font-Metriken (Aufstieg/Abstieg in Pixeln bei gegebener Pixelhoehe) --
 * fuer litehtml's create_font()/font_metrics (siehe browser_render.cpp). */
void flux_fb_font_metrics_px(int px, int *ascent, int *descent, int *line_gap) {
    font_init();
    if (!g_font_ready) { *ascent = px; *descent = 0; *line_gap = 0; return; }
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
    float sf = stbtt_ScaleForPixelHeight(&g_font, (float)px);
    *ascent   = (int)(asc  * sf + 0.5f);
    *descent  = (int)(-desc * sf + 0.5f);
    *line_gap = (int)(gap  * sf + 0.5f);
}

void flux_fb_text(flux_fb_t *fb, int x, int y, const char *s, uint32_t rgb, int scale) {
    int px = (int)(font_px_for_scale(scale) + 0.5f);
    flux_fb_text_px(fb, x, y, s, rgb, px);
}

int flux_fb_text_width(const char *s, int scale) {
    int px = (int)(font_px_for_scale(scale) + 0.5f);
    return flux_fb_text_width_px(s, px);
}

/* ---- Erweiterte Primitive --------------------------------------------- */

static inline uint32_t lerp_color(uint32_t a, uint32_t b, int t, int max) {
    if (max <= 0) return a;
    uint8_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    uint8_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    uint8_t r = (uint8_t)(ar + (int)(br - ar) * t / max);
    uint8_t g = (uint8_t)(ag + (int)(bg - ag) * t / max);
    uint8_t bl2 = (uint8_t)(ab + (int)(bb - ab) * t / max);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | bl2;
}

void flux_fb_fill_rect_rounded(flux_fb_t *fb, int x, int y, int w, int h, int r, uint32_t col) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int dx = (i < r) ? (r - i) : (i >= w - r) ? (i - (w - r - 1)) : 0;
            int dy = (j < r) ? (r - j) : (j >= h - r) ? (j - (h - r - 1)) : 0;
            if (dx > 0 && dy > 0 && dx * dx + dy * dy > r * r) continue;
            put_back(fb, x + i, y + j, col);
        }
    }
}

void flux_fb_fill_gradient_v(flux_fb_t *fb, int x, int y, int w, int h, uint32_t c_top, uint32_t c_bot) {
    for (int j = 0; j < h; j++) {
        uint32_t col = lerp_color(c_top, c_bot, j, h - 1);
        for (int i = 0; i < w; i++)
            put_back(fb, x + i, y + j, col);
    }
}

void flux_fb_fill_gradient_h(flux_fb_t *fb, int x, int y, int w, int h, uint32_t c_left, uint32_t c_right) {
    for (int i = 0; i < w; i++) {
        uint32_t col = lerp_color(c_left, c_right, i, w - 1);
        for (int j = 0; j < h; j++)
            put_back(fb, x + i, y + j, col);
    }
}

void flux_fb_fill_gradient_v_rounded(flux_fb_t *fb, int x, int y, int w, int h, int r, uint32_t c_top, uint32_t c_bot) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        uint32_t col = lerp_color(c_top, c_bot, j, h - 1);
        for (int i = 0; i < w; i++) {
            int dx = (i < r) ? (r - i) : (i >= w - r) ? (i - (w - r - 1)) : 0;
            int dy = (j < r) ? (r - j) : (j >= h - r) ? (j - (h - r - 1)) : 0;
            if (dx > 0 && dy > 0 && dx * dx + dy * dy > r * r) continue;
            put_back(fb, x + i, y + j, col);
        }
    }
}

void flux_fb_fill_circle(flux_fb_t *fb, int cx, int cy, int r, uint32_t col) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                put_back(fb, cx + dx, cy + dy, col);
}

void flux_fb_draw_ring(flux_fb_t *fb, int cx, int cy, int r, int thick, uint32_t col) {
    int r_out = r + thick / 2;
    int r_in  = r - thick / 2;
    if (r_in < 0) r_in = 0;
    for (int dy = -r_out; dy <= r_out; dy++) {
        for (int dx = -r_out; dx <= r_out; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r_out * r_out && d2 >= r_in * r_in)
                put_back(fb, cx + dx, cy + dy, col);
        }
    }
}

void flux_fb_hline(flux_fb_t *fb, int x, int y, int w, uint32_t col) {
    for (int i = 0; i < w; i++) put_back(fb, x + i, y, col);
}

void flux_fb_vline(flux_fb_t *fb, int x, int y, int h, uint32_t col) {
    for (int j = 0; j < h; j++) put_back(fb, x, y + j, col);
}

void flux_fb_text_shadow(flux_fb_t *fb, int x, int y, const char *s, uint32_t col, int scale) {
    /* Schattenfarbe: deutlich dunkler als der Vordergrund */
    uint32_t shadow = (uint32_t)(((col >> 16) & 0xFF) * 2 / 10) << 16 |
                      (uint32_t)(((col >>  8) & 0xFF) * 2 / 10) <<  8 |
                      (uint32_t)( (col        & 0xFF) * 2 / 10);
    flux_fb_text(fb, x + 2, y + 2, s, shadow, scale);
    flux_fb_text(fb, x,     y,     s, col,    scale);
}

void flux_fb_present(flux_fb_t *fb) {
    if (!fb->mmio) return; /* Mock-Framebuffer (flux_fb_open_null) -- nichts zu kopieren */
    int row_bytes = fb->width * (fb->bpp / 8);
    /* Zeilenoffset im mmio anhand des Geraete-Strides (line_length), nicht
     * der sichtbaren Breite -- manche Framebuffer padden ihre Zeilen. */
    size_t mmio_stride = (size_t)fb->stride_px * (fb->bpp / 8);
    for (int y = 0; y < fb->height; y++) {
        uint32_t *back_row = fb->back + (size_t)y * fb->stride_px;
        uint32_t *prev_row = fb->prev + (size_t)y * fb->stride_px;
        if (memcmp(back_row, prev_row, (size_t)fb->width * sizeof(uint32_t)) == 0)
            continue; /* Zeile unveraendert -> ueberspringen */

        if (fb->bpp == 32) {
            memcpy(fb->mmio + (size_t)y * mmio_stride, back_row, row_bytes);
        } else { /* 16-bit RGB565 Fallback */
            uint16_t *dst = (uint16_t *)(fb->mmio + (size_t)y * mmio_stride);
            for (int x = 0; x < fb->width; x++) {
                uint32_t p = back_row[x];
                uint16_t r = (p >> 19) & 0x1F, g = (p >> 10) & 0x3F, b = (p >> 3) & 0x1F;
                dst[x] = (uint16_t)((r << 11) | (g << 5) | b);
            }
        }
        memcpy(prev_row, back_row, (size_t)fb->width * sizeof(uint32_t));
    }
}
