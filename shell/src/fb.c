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
 * jedes Quad als gefuelltes Rechteck in den Backbuffer. */
void flux_fb_text(flux_fb_t *fb, int x, int y, const char *s, uint32_t rgb, int scale) {
    char vbuf[64 * 1024];
    int num_quads = stb_easy_font_print(0, 0, (char *)s, NULL, vbuf, sizeof(vbuf));

    for (int q = 0; q < num_quads; q++) {
        float qx[4], qy[4];
        for (int v = 0; v < 4; v++) {
            const char *base = vbuf + (size_t)(q * 4 + v) * 16;
            float fx, fy;
            memcpy(&fx, base, 4);
            memcpy(&fy, base + 4, 4);
            qx[v] = fx;
            qy[v] = fy;
        }
        float minx = qx[0], maxx = qx[0], miny = qy[0], maxy = qy[0];
        for (int v = 1; v < 4; v++) {
            if (qx[v] < minx) minx = qx[v];
            if (qx[v] > maxx) maxx = qx[v];
            if (qy[v] < miny) miny = qy[v];
            if (qy[v] > maxy) maxy = qy[v];
        }
        int rx = x + (int)(minx * scale);
        int ry = y + (int)(miny * scale);
        int rw = (int)((maxx - minx) * scale);
        int rh = (int)((maxy - miny) * scale);
        /* Strichstaerke: groessere Schrift bekommt etwas mehr Gewicht, damit
         * der duenne stb_easy_font-Strich auf dem Handy gut lesbar bleibt. */
        int weight = scale + (scale >= 3 ? 1 : 0);
        if (rw < weight) rw = weight;
        if (rh < weight) rh = weight;
        flux_fb_fill_rect(fb, rx, ry, rw, rh, rgb);
    }
}

int flux_fb_text_width(const char *s, int scale) {
    return stb_easy_font_width((char *)s) * scale;
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
