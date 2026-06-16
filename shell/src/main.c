/* flux-shell -- der einzige "App"-Prozess, der am Anfang laeuft.
 * Kein Homescreen mit Icon-Grid: man entsperrt direkt in den
 * KI-Assistenten. Zeichnet auf den Framebuffer, fragt fluxaid
 * ueber den Unix-Socket.
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
    int have_input = (flux_input_open(&in) == 0);
    if (!have_input)
        fprintf(stderr, "flux-shell: keine Tastatur gefunden, nur Uhr wird angezeigt.\n");

    flux_screen_t screen = FLUX_SCREEN_LOCK;
    char input_buf[256] = {0};
    char answer_buf[8192] = {0};

    flux_ui_draw_lock(&fb);

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        if (have_input) {
            FD_SET(flux_input_fd(&in), &rfds);
            maxfd = flux_input_fd(&in);
        }
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ready = have_input ? select(maxfd + 1, &rfds, NULL, NULL, &tv) : (sleep(1), 0);

        if (ready <= 0) {
            /* Kein Input -- nur die Uhr auf dem Lockscreen weiterlaufen lassen. */
            if (screen == FLUX_SCREEN_LOCK)
                flux_ui_draw_lock(&fb);
            continue;
        }

        char ch = 0;
        char kind = flux_input_poll(&in, &ch);
        if (!kind) continue;

        if (screen == FLUX_SCREEN_LOCK) {
            if (kind == 'E') {
                screen = FLUX_SCREEN_ASSISTANT;
                input_buf[0] = '\0';
                answer_buf[0] = '\0';
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            }
            continue;
        }

        /* FLUX_SCREEN_ASSISTANT */
        if (kind == 'c') {
            size_t len = strlen(input_buf);
            if (len + 1 < sizeof(input_buf)) {
                input_buf[len] = ch;
                input_buf[len + 1] = '\0';
            }
            flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
        } else if (kind == 'B') {
            size_t len = strlen(input_buf);
            if (len > 0) input_buf[len - 1] = '\0';
            flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
        } else if (kind == 'E') {
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
