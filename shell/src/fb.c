#include "fb.h"
#include "stb_easy_font.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

int flux_fb_open(flux_fb_t *fb, const char *device) {
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

/* stb_easy_font liefert pro Buchstabenstrich ein Quad (4 Vertices a
 * 16 Byte: float x, float y, float z, uint8 color[4]). Wir rastern
 * jedes Quad als gefuelltes Rechteck in den Backbuffer.
 * Unterstuetzt UTF-8: deutsche Umlaute werden als Basiszeichen + Punkte gerendert. */

/* Rasterniert alle Quads eines einzelnen ASCII-Zeichens; gibt Zeichenbreite zurueck. */
static int render_ascii_char(flux_fb_t *fb, int x, int y, char c, uint32_t rgb, int scale) {
    char s[2] = {c, 0};
    char vbuf[4096];
    int num_quads = stb_easy_font_print(0, 0, s, NULL, vbuf, sizeof(vbuf));
    int weight = scale + (scale >= 3 ? 1 : 0);
    for (int q = 0; q < num_quads; q++) {
        float qx[4], qy[4];
        for (int v = 0; v < 4; v++) {
            const char *base = vbuf + (size_t)(q * 4 + v) * 16;
            float fx, fy;
            memcpy(&fx, base, 4);
            memcpy(&fy, base + 4, 4);
            qx[v] = fx; qy[v] = fy;
        }
        float minx=qx[0], maxx=qx[0], miny=qy[0], maxy=qy[0];
        for (int v = 1; v < 4; v++) {
            if (qx[v] < minx) minx = qx[v];
            if (qx[v] > maxx) maxx = qx[v];
            if (qy[v] < miny) miny = qy[v];
            if (qy[v] > maxy) maxy = qy[v];
        }
        int rw = (int)((maxx - minx) * scale);
        int rh = (int)((maxy - miny) * scale);
        if (rw < weight) rw = weight;
        if (rh < weight) rh = weight;
        flux_fb_fill_rect(fb, x + (int)(minx * scale), y + (int)(miny * scale), rw, rh, rgb);
    }
    return stb_easy_font_width(s) * scale;
}

/* Zwei Punkte ueber einem Buchstaben (Umlaut-Diaeresis). */
static void draw_diaeresis(flux_fb_t *fb, int x, int char_w, int y, int scale, uint32_t rgb) {
    int dot = (scale <= 2) ? 2 : scale;
    int dot_y = y - dot - 1;
    int third = char_w / 3;
    flux_fb_fill_rect(fb, x + third - dot / 2,     dot_y, dot, dot, rgb);
    flux_fb_fill_rect(fb, x + 2 * third - dot / 2, dot_y, dot, dot, rgb);
}

/* Dekodiert ein UTF-8-Codepoint (erstes Zeichen); gibt Basiszeichen und Flags zurueck.
 * Returns Anzahl verbrauchter Bytes. base_char gesetzt, dots=1 fuer Umlaut, dbl=1 fuer ss (ß). */
static int utf8_decode_german(const unsigned char *p, char *base_char, int *dots, int *dbl) {
    *dots = 0; *dbl = 0; *base_char = '?';
    if (p[0] < 0x80) { *base_char = (char)p[0]; return 1; }
    if (p[0] == 0xC3 && p[1]) {
        switch (p[1]) {
            case 0xA4: *base_char='a'; *dots=1; return 2;  /* ä */
            case 0xB6: *base_char='o'; *dots=1; return 2;  /* ö */
            case 0xBC: *base_char='u'; *dots=1; return 2;  /* ü */
            case 0x84: *base_char='A'; *dots=1; return 2;  /* Ä */
            case 0x96: *base_char='O'; *dots=1; return 2;  /* Ö */
            case 0x9C: *base_char='U'; *dots=1; return 2;  /* Ü */
            case 0x9F: *base_char='s'; *dbl=1;  return 2;  /* ß -> ss */
            case 0xA9: *base_char='e';           return 2;  /* é */
            default: break;
        }
    }
    /* Unbekannte Multibyte-Sequenz: ueberspringen */
    if ((p[0] & 0xE0) == 0xC0) return 2;
    if ((p[0] & 0xF0) == 0xE0) return 3;
    if ((p[0] & 0xF8) == 0xF0) return 4;
    return 1;
}

void flux_fb_text(flux_fb_t *fb, int x, int y, const char *s, uint32_t rgb, int scale) {
    int cx = x;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        char base; int dots, dbl;
        int consumed = utf8_decode_german(p, &base, &dots, &dbl);
        if (base != '?') {
            int cw = render_ascii_char(fb, cx, y, base, rgb, scale);
            if (dots) draw_diaeresis(fb, cx, cw, y, scale, rgb);
            cx += cw;
            if (dbl) cx += render_ascii_char(fb, cx, y, base, rgb, scale);
        }
        p += consumed;
    }
}

int flux_fb_text_width(const char *s, int scale) {
    int w = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        char base; int dots, dbl;
        int consumed = utf8_decode_german(p, &base, &dots, &dbl);
        if (base != '?') {
            char buf[2] = {base, 0};
            int cw = stb_easy_font_width(buf) * scale;
            w += cw;
            if (dbl) w += cw;
        }
        p += consumed;
    }
    return w;
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
    for (int y = 0; y < fb->height; y++) {
        uint32_t *back_row = fb->back + (size_t)y * fb->stride_px;
        uint32_t *prev_row = fb->prev + (size_t)y * fb->stride_px;
        if (memcmp(back_row, prev_row, (size_t)fb->width * sizeof(uint32_t)) == 0)
            continue; /* Zeile unveraendert -> ueberspringen */

        if (fb->bpp == 32) {
            memcpy(fb->mmio + (size_t)y * row_bytes, back_row, row_bytes);
        } else { /* 16-bit RGB565 Fallback */
            uint16_t *dst = (uint16_t *)(fb->mmio + (size_t)y * row_bytes);
            for (int x = 0; x < fb->width; x++) {
                uint32_t p = back_row[x];
                uint16_t r = (p >> 19) & 0x1F, g = (p >> 10) & 0x3F, b = (p >> 3) & 0x1F;
                dst[x] = (uint16_t)((r << 11) | (g << 5) | b);
            }
        }
        memcpy(prev_row, back_row, (size_t)fb->width * sizeof(uint32_t));
    }
}
