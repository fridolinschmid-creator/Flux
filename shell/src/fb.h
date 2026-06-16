/* fb.h -- direkter Linux-Framebuffer-Zugriff mit Double-Buffering.
 * Ziel: jede Zeichenoperation landet zuerst im Backbuffer im RAM,
 * erst flux_fb_present() kopiert die GEAENDERTEN Zeilen nach /dev/fb0.
 * Das ist die Voraussetzung dafuer, dass die Shell auch auf schwacher
 * Hardware ruckelfrei bleibt -- kein Full-Redraw pro Frame.
 */
#ifndef FLUX_FB_H
#define FLUX_FB_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int      fd;
    uint8_t *mmio;       /* echter Framebuffer (mmap auf /dev/fb0) */
    uint32_t *back;      /* Backbuffer im RAM, gleiche Groesse */
    uint32_t *prev;      /* letzter praesentierter Frame, fuer Dirty-Check */
    int      width, height, stride_px, bpp;
    size_t   screensize;
} flux_fb_t;

int  flux_fb_open(flux_fb_t *fb, const char *device);
void flux_fb_close(flux_fb_t *fb);

void flux_fb_clear(flux_fb_t *fb, uint32_t rgb);
void flux_fb_set_px(flux_fb_t *fb, int x, int y, uint32_t rgb);
void flux_fb_fill_rect(flux_fb_t *fb, int x, int y, int w, int h, uint32_t rgb);
void flux_fb_blend_rect(flux_fb_t *fb, int x, int y, int w, int h, uint32_t rgb, uint8_t alpha);
void flux_fb_text(flux_fb_t *fb, int x, int y, const char *s, uint32_t rgb, int scale);
int  flux_fb_text_width(const char *s, int scale);

/* Kopiert nur Zeilen, die sich seit dem letzten Aufruf geaendert haben. */
void flux_fb_present(flux_fb_t *fb);

#endif
