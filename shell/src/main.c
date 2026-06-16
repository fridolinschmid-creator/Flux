/* flux-shell -- der einzige "App"-Prozess, der am Anfang laeuft.
 * Kein Homescreen mit Icon-Grid: man entsperrt direkt in den
 * KI-Assistenten. Zeichnet auf den Framebuffer, fragt fluxaid
 * ueber den Unix-Socket. Bedienung touch-first (Wischen, eigene
 * Bildschirmtastatur) -- eine Hardware-Tastatur funktioniert
 * weiterhin, ist aber nicht mehr Voraussetzung (siehe input.h).
 */
#include "fb.h"
#include "input.h"
#include "ipc.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

int main(void) {
    flux_fb_t fb;
    if (flux_fb_open(&fb, "/dev/fb0") != 0) {
        fprintf(stderr, "flux-shell: /dev/fb0 nicht verfuegbar.\n");
        return 1;
    }

    flux_input_t in;
    int have_input = (flux_input_open(&in, fb.width, fb.height) == 0);
    if (!have_input)
        fprintf(stderr, "flux-shell: keine Eingabegeraete gefunden, nur Uhr wird angezeigt.\n");

    flux_screen_t screen = FLUX_SCREEN_LOCK;
    char input_buf[256] = {0};
    char answer_buf[8192] = {0};

    flux_ui_draw_lock(&fb);

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = have_input ? flux_input_add_fds(&in, &rfds) : -1;
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ready = (maxfd >= 0) ? select(maxfd + 1, &rfds, NULL, NULL, &tv) : (sleep(1), 0);

        if (ready <= 0) {
            /* Kein Input -- nur die Uhr auf dem Lockscreen weiterlaufen lassen. */
            if (screen == FLUX_SCREEN_LOCK)
                flux_ui_draw_lock(&fb);
            continue;
        }

        flux_event_t ev = flux_input_poll(&in);
        if (ev.type == FLUX_EV_NONE) continue;

        if (screen == FLUX_SCREEN_LOCK) {
            if (ev.type == FLUX_EV_ENTER || ev.type == FLUX_EV_SWIPE_UP) {
                screen = FLUX_SCREEN_ASSISTANT;
                input_buf[0] = '\0';
                answer_buf[0] = '\0';
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            }
            continue;
        }

        /* FLUX_SCREEN_ASSISTANT -- Taps auf die Bildschirmtastatur
         * werden hier in dieselben logischen Events wie eine
         * Hardware-Tastatur uebersetzt, danach folgt ein einziger
         * gemeinsamer Verarbeitungspfad. */
        flux_event_type_t kind = ev.type;
        char ch = ev.ch;

        if (kind == FLUX_EV_TAP) {
            char tap_ch = 0;
            int tap_backspace = 0, tap_enter = 0;
            if (!flux_ui_kbd_hit(&fb, ev.x, ev.y, &tap_ch, &tap_backspace, &tap_enter))
                continue; /* Tap ausserhalb der Tastatur -- ignorieren */
            if (tap_backspace) kind = FLUX_EV_BACKSPACE;
            else if (tap_enter) kind = FLUX_EV_ENTER;
            else { kind = FLUX_EV_CHAR; ch = tap_ch; }
        } else if (kind == FLUX_EV_SWIPE_UP) {
            continue; /* auf dem Assistenten-Bildschirm ohne Bedeutung */
        }

        if (kind == FLUX_EV_CHAR) {
            size_t len = strlen(input_buf);
            if (len + 1 < sizeof(input_buf)) {
                input_buf[len] = ch;
                input_buf[len + 1] = '\0';
            }
            flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
        } else if (kind == FLUX_EV_BACKSPACE) {
            size_t len = strlen(input_buf);
            if (len > 0) input_buf[len - 1] = '\0';
            flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
        } else if (kind == FLUX_EV_ENTER) {
            if (input_buf[0] == '\0') continue;
            flux_ui_draw_assistant(&fb, input_buf, answer_buf, 1); /* "Denke nach..." sofort zeigen */
            flux_ipc_ask(input_buf, answer_buf, sizeof(answer_buf));
            input_buf[0] = '\0';
            flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
        }
    }

    flux_input_close(&in);
    flux_fb_close(&fb);
    return 0;
}
