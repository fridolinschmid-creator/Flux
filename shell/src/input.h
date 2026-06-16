/* input.h -- Eingabe ueber den Linux-Evdev-Layer: Tastatur UND
 * Touch/Pointer (virtio-tablet in QEMU, ein echter Touchscreen auf
 * Hardware liefert dieselben EV_ABS/BTN_TOUCH-Events). Die Shell ist
 * damit nicht an eine PC-Tastatur gebunden -- Tippen geht per
 * Hardware-Tastatur ODER per Bildschirmtastatur (siehe ui.c).
 */
#ifndef FLUX_INPUT_H
#define FLUX_INPUT_H

typedef enum {
    FLUX_EV_NONE = 0,
    FLUX_EV_CHAR,        /* ch gesetzt */
    FLUX_EV_BACKSPACE,
    FLUX_EV_ENTER,
    FLUX_EV_TAP,         /* x, y gesetzt (Bildschirmkoordinaten) */
    FLUX_EV_SWIPE_UP,    /* schneller Wisch nach oben (z.B. zum Entsperren) */
} flux_event_type_t;

typedef struct {
    flux_event_type_t type;
    char ch;
    int  x, y;
} flux_event_t;

typedef struct {
    int kbd_fd;    /* -1, wenn keine Tastatur gefunden wurde */
    int touch_fd;  /* -1, wenn kein Touch/Pointer-Geraet gefunden wurde */

    int abs_min_x, abs_max_x, abs_min_y, abs_max_y;
    int screen_w, screen_h;

    int touch_down;        /* gerade gedrueckt? */
    int down_x, down_y;    /* Position beim Aufsetzen */
    int cur_x, cur_y;      /* letzte bekannte Position */
} flux_input_t;

/* Sucht Tastatur- und Touch/Pointer-Geraete unter /dev/input/eventN.
 * screen_w/h werden gebraucht, um Touch-Rohkoordinaten (die in einem
 * geraeteabhaengigen Wertebereich liegen) auf Bildschirmpixel
 * umzurechnen. Gibt 0 zurueck, wenn mindestens ein Geraet gefunden
 * wurde. */
int  flux_input_open(flux_input_t *in, int screen_w, int screen_h);
void flux_input_close(flux_input_t *in);

/* Traegt alle offenen Eingabe-Fds in rfds ein, gibt die hoechste
 * Fd-Nummer zurueck (oder -1, wenn keine Geraete offen sind). */
int flux_input_add_fds(flux_input_t *in, void *rfds_fd_set);

/* Liest ein anstehendes Event. type == FLUX_EV_NONE, wenn nichts da war. */
flux_event_t flux_input_poll(flux_input_t *in);

#endif
