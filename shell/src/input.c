#include "input.h"

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
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

static int device_has_keys(int fd) {
    unsigned long evbits = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits) < 0)
        return 0;
    return (evbits & (1 << EV_KEY)) != 0;
}

int flux_input_open(flux_input_t *in) {
    build_keymap();
    in->fd = -1;

    char path[64];
    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        if (device_has_keys(fd)) {
            in->fd = fd;
            return 0;
        }
        close(fd);
    }
    return -1; /* keine Tastatur gefunden, z.B. ohne -device keyboard in QEMU */
}

void flux_input_close(flux_input_t *in) {
    if (in->fd >= 0) close(in->fd);
}

int flux_input_fd(flux_input_t *in) {
    return in->fd;
}

char flux_input_poll(flux_input_t *in, char *ch) {
    struct input_event ev;
    if (read(in->fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev))
        return 0;
    if (ev.type != EV_KEY || ev.value != 1) /* nur "Taste gedrueckt" */
        return 0;

    if (ev.code == KEY_ENTER || ev.code == KEY_KPENTER)
        return 'E';
    if (ev.code == KEY_BACKSPACE)
        return 'B';
    if (ev.code < (int)sizeof(keymap) && keymap[ev.code]) {
        *ch = keymap[ev.code];
        return 'c';
    }
    return 0;
}
