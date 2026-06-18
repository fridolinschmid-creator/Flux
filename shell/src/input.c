#include "input.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <linux/input.h>

/* Keycode -> ASCII (Kleinbuchstaben/Ziffern, deutsches Layout
 * ignoriert -- fuer den Prototyp reicht US-QWERTY). */
static char keymap[256];

static void build_keymap(void) {
    memset(keymap, 0, sizeof(keymap));
    static const struct { int code; char ch; } table[] = {
        {KEY_A,'a'},{KEY_B,'b'},{KEY_C,'c'},{KEY_D,'d'},{KEY_E,'e'},
        {KEY_F,'f'},{KEY_G,'g'},{KEY_H,'h'},{KEY_I,'i'},{KEY_J,'j'},
        {KEY_K,'k'},{KEY_L,'l'},{KEY_M,'m'},{KEY_N,'n'},{KEY_O,'o'},
        {KEY_P,'p'},{KEY_Q,'q'},{KEY_R,'r'},{KEY_S,'s'},{KEY_T,'t'},
        {KEY_U,'u'},{KEY_V,'v'},{KEY_W,'w'},{KEY_X,'x'},{KEY_Y,'y'},
        {KEY_Z,'z'},
        {KEY_0,'0'},{KEY_1,'1'},{KEY_2,'2'},{KEY_3,'3'},{KEY_4,'4'},
        {KEY_5,'5'},{KEY_6,'6'},{KEY_7,'7'},{KEY_8,'8'},{KEY_9,'9'},
        {KEY_SPACE,' '},{KEY_MINUS,'-'},{KEY_DOT,'.'},{KEY_COMMA,','},
        {KEY_SLASH,'/'},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        keymap[table[i].code] = table[i].ch;
}

static int device_has_evbit(int fd, int bit) {
    unsigned long evbits = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits) < 0)
        return 0;
    return (evbits & (1UL << bit)) != 0;
}

static int device_has_code(int fd, int ev_type, int code, int max_code) {
    unsigned long bits[((max_code) / (8 * sizeof(unsigned long))) + 1];
    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(ev_type, sizeof(bits)), bits) < 0)
        return 0;
    size_t word = code / (8 * sizeof(unsigned long));
    size_t bit  = code % (8 * sizeof(unsigned long));
    return (bits[word] >> bit) & 1UL;
}

/* Sucht unter /dev/input/eventN je ein Tastatur- und ein Touch/
 * Pointer-Geraet. Tastatur: hat EV_KEY, aber kein EV_ABS (sonst waere
 * es z.B. ein Touchpad). Touch/Pointer: hat EV_ABS mit ABS_X/ABS_Y --
 * deckt sowohl virtio-tablet (Maus-artig, BTN_LEFT) als auch einen
 * echten Touchscreen (BTN_TOUCH) ab, beide liefern dieselben
 * EV_ABS-Koordinaten. */
int flux_input_open(flux_input_t *in, int screen_w, int screen_h) {
    memset(in, 0, sizeof(*in));
    build_keymap();
    in->kbd_fd = -1;
    in->touch_fd = -1;
    in->screen_w = screen_w;
    in->screen_h = screen_h;

    char path[64];
    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        int has_key = device_has_evbit(fd, EV_KEY);
        int has_abs = device_has_evbit(fd, EV_ABS);
        int has_xy  = has_abs &&
                      device_has_code(fd, EV_ABS, ABS_X, ABS_MAX) &&
                      device_has_code(fd, EV_ABS, ABS_Y, ABS_MAX);

        if (has_xy && in->touch_fd < 0) {
            struct input_absinfo ax, ay;
            if (ioctl(fd, EVIOCGABS(ABS_X), &ax) == 0 &&
                ioctl(fd, EVIOCGABS(ABS_Y), &ay) == 0) {
                in->touch_fd   = fd;
                in->abs_min_x  = ax.minimum;
                in->abs_max_x  = ax.maximum;
                in->abs_min_y  = ay.minimum;
                in->abs_max_y  = ay.maximum;
                continue;
            }
        }
        if (has_key && !has_abs && in->kbd_fd < 0) {
            in->kbd_fd = fd;
            continue;
        }
        close(fd);
    }
    return (in->kbd_fd >= 0 || in->touch_fd >= 0) ? 0 : -1;
}

void flux_input_close(flux_input_t *in) {
    if (in->kbd_fd >= 0) close(in->kbd_fd);
    if (in->touch_fd >= 0) close(in->touch_fd);
}

int flux_input_add_fds(flux_input_t *in, void *rfds_fd_set) {
    fd_set *rfds = (fd_set *)rfds_fd_set;
    int maxfd = -1;
    if (in->kbd_fd >= 0) {
        FD_SET(in->kbd_fd, rfds);
        if (in->kbd_fd > maxfd) maxfd = in->kbd_fd;
    }
    if (in->touch_fd >= 0) {
        FD_SET(in->touch_fd, rfds);
        if (in->touch_fd > maxfd) maxfd = in->touch_fd;
    }
    return maxfd;
}

static int scale_coord(int raw, int min, int max, int screen_dim) {
    if (max <= min) return 0;
    long v = (long)(raw - min) * screen_dim / (max - min);
    if (v < 0) v = 0;
    if (v >= screen_dim) v = screen_dim - 1;
    return (int)v;
}

static flux_event_t poll_kbd(flux_input_t *in) {
    flux_event_t out;
    memset(&out, 0, sizeof(out));
    struct input_event ev;
    if (read(in->kbd_fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev))
        return out;
    if (ev.type != EV_KEY || ev.value != 1) /* nur "Taste gedrueckt" */
        return out;

    if (ev.code == KEY_ENTER || ev.code == KEY_KPENTER) {
        out.type = FLUX_EV_ENTER;
    } else if (ev.code == KEY_BACKSPACE) {
        out.type = FLUX_EV_BACKSPACE;
    } else if (ev.code < (int)sizeof(keymap) && keymap[ev.code]) {
        out.type = FLUX_EV_CHAR;
        out.ch = keymap[ev.code];
    }
    return out;
}

/* Ab wie viel Pixel Aufwaerts-Bewegung beim Loslassen ein Wisch statt
 * ein Tipp ist (Bildschirm-Pixel, nach der Skalierung). */
#define SWIPE_UP_THRESHOLD_PX 60

static flux_event_t poll_touch(flux_input_t *in) {
    flux_event_t out;
    memset(&out, 0, sizeof(out));
    struct input_event ev;
    if (read(in->touch_fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev))
        return out;

    if (ev.type == EV_ABS) {
        if (ev.code == ABS_X)
            in->cur_x = scale_coord(ev.value, in->abs_min_x, in->abs_max_x, in->screen_w);
        else if (ev.code == ABS_Y)
            in->cur_y = scale_coord(ev.value, in->abs_min_y, in->abs_max_y, in->screen_h);
        return out;
    }

    if (ev.type == EV_KEY && (ev.code == BTN_TOUCH || ev.code == BTN_LEFT)) {
        if (ev.value == 1) {
            in->touch_down = 1;
            in->down_x = in->cur_x;
            in->down_y = in->cur_y;
        } else if (ev.value == 0 && in->touch_down) {
            in->touch_down = 0;
            int up_dy = in->down_y - in->cur_y;
            int dx    = in->cur_x - in->down_x;
            if (up_dy > SWIPE_UP_THRESHOLD_PX && abs(dx) < SWIPE_UP_THRESHOLD_PX * 2) {
                out.type = FLUX_EV_SWIPE_UP;
            } else if (up_dy < -SWIPE_UP_THRESHOLD_PX && abs(dx) < SWIPE_UP_THRESHOLD_PX * 2) {
                out.type = FLUX_EV_SWIPE_DOWN;
            } else if (dx < -SWIPE_UP_THRESHOLD_PX && abs(up_dy) < SWIPE_UP_THRESHOLD_PX * 2) {
                out.type = FLUX_EV_SWIPE_LEFT;
            } else if (dx > SWIPE_UP_THRESHOLD_PX && abs(up_dy) < SWIPE_UP_THRESHOLD_PX * 2) {
                out.type = FLUX_EV_SWIPE_RIGHT;
            } else {
                out.type = FLUX_EV_TAP;
                out.x = in->cur_x;
                out.y = in->cur_y;
            }
        }
    }
    return out;
}

flux_event_t flux_input_poll(flux_input_t *in) {
    flux_event_t none;
    memset(&none, 0, sizeof(none));

    fd_set rfds;
    FD_ZERO(&rfds);
    int maxfd = flux_input_add_fds(in, &rfds);
    if (maxfd < 0) return none;

    struct timeval tv = {0, 0};
    if (select(maxfd + 1, &rfds, NULL, NULL, &tv) <= 0)
        return none;

    if (in->kbd_fd >= 0 && FD_ISSET(in->kbd_fd, &rfds))
        return poll_kbd(in);
    if (in->touch_fd >= 0 && FD_ISSET(in->touch_fd, &rfds))
        return poll_touch(in);

    return none;
}
