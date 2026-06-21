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
int  flux_fb_open_null(flux_fb_t *fb, int width, int height); /* Mock ohne /dev/fb0 */
void flux_fb_close(flux_fb_t *fb);

void flux_fb_clear(flux_fb_t *fb, uint32_t rgb);
void flux_fb_set_px(flux_fb_t *fb, int x, int y, uint32_t rgb);
void flux_fb_fill_rect(flux_fb_t *fb, int x, int y, int w, int h, uint32_t rgb);
void flux_fb_blend_rect(flux_fb_t *fb, int x, int y, int w, int h, uint32_t rgb, uint8_t alpha);
void flux_fb_text(flux_fb_t *fb, int x, int y, const char *s, uint32_t rgb, int scale);
int  flux_fb_text_width(const char *s, int scale);

/* Kopiert nur Zeilen, die sich seit dem letzten Aufruf geaendert haben. */
void flux_fb_present(flux_fb_t *fb);

/* ---- Erweiterte Zeichen-Primitive (Premium-Design) ------------------- */

/* Abgerundetes Rechteck (Software-Rasterung der Ecken). r = Eckenradius. */
void flux_fb_fill_rect_rounded(flux_fb_t *fb, int x, int y, int w, int h, int r, uint32_t col);

/* Vertikaler Farbverlauf: c_top oben nach c_bot unten. */
void flux_fb_fill_gradient_v(flux_fb_t *fb, int x, int y, int w, int h, uint32_t c_top, uint32_t c_bot);

/* Horizontaler Farbverlauf: c_left links nach c_right rechts. */
void flux_fb_fill_gradient_h(flux_fb_t *fb, int x, int y, int w, int h, uint32_t c_left, uint32_t c_right);

/* Abgerundetes Rechteck mit vertikalem Farbverlauf. */
void flux_fb_fill_gradient_v_rounded(flux_fb_t *fb, int x, int y, int w, int h, int r, uint32_t c_top, uint32_t c_bot);

/* Gefuellter Kreis (fuer Avatare, Indikatoren). */
void flux_fb_fill_circle(flux_fb_t *fb, int cx, int cy, int r, uint32_t col);

/* Kreisring (Outline). */
void flux_fb_draw_ring(flux_fb_t *fb, int cx, int cy, int r, int thick, uint32_t col);

/* 1px-Linien. */
void flux_fb_hline(flux_fb_t *fb, int x, int y, int w, uint32_t col);
void flux_fb_vline(flux_fb_t *fb, int x, int y, int h, uint32_t col);

/* Text mit Schatten (+2px Versatz in dunklerem Ton). */
void flux_fb_text_shadow(flux_fb_t *fb, int x, int y, const char *s, uint32_t col, int scale);

#endif
