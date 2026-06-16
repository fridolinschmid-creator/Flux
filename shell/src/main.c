/* flux-shell -- der einzige "App"-Prozess, der am Anfang laeuft.
 * Kein Homescreen mit Icon-Grid: man entsperrt direkt in den
 * KI-Assistenten. Zeichnet auf den Framebuffer, fragt fluxaid
 * ueber den Unix-Socket. Bedienung touch-first (Wischen, eigene
 * Bildschirmtastatur) -- eine Hardware-Tastatur funktioniert
 * weiterhin, ist aber nicht mehr Voraussetzung (siehe input.h).
 *
 * Mehrschritt-Dialoge (Kontakt anlegen, Mail schreiben) leben komplett
 * hier als eigene Zustandsmaschine -- fluxaid bleibt absichtlich
 * zustandslos (ein Request pro Verbindung, siehe flux_protocol.h).
 */
#include "fb.h"
#include "input.h"
#include "ipc.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/select.h>

typedef enum {
    FLUX_DIALOG_NONE,
    FLUX_DIALOG_CONTACT_NAME,
    FLUX_DIALOG_CONTACT_PHONE,
    FLUX_DIALOG_EMAIL_TO,
    FLUX_DIALOG_EMAIL_SUBJECT,
    FLUX_DIALOG_EMAIL_BODY,
} flux_dialog_t;

/* Bewusst simple Substring-Erkennung, kein NLP -- gleiches Prinzip wie
 * die lokalen Intents in fluxai/src/actions.c. */
static int detect_contact_create(const char *s) {
    if (!strcasestr(s, "kontakt")) return 0;
    return strcasestr(s, "anleg") || strcasestr(s, "erstell") ||
           strcasestr(s, "speicher") || strcasestr(s, "neu");
}

static int detect_email_create(const char *s) {
    if (!strcasestr(s, "mail")) return 0;
    return strcasestr(s, "schreib") || strcasestr(s, "sende") || strcasestr(s, "schick");
}

/* Erwartet sinngemaess "ruf <name> an" / "rufe <name> an". */
static int detect_call(const char *s, char *name_out, size_t name_cap) {
    const char *p = strcasestr(s, "ruf");
    if (!p) return 0;
    p += 3;
    if (*p == 'e') p++; /* "rufe" */
    while (*p == ' ') p++;
    if (!*p) return 0;

    const char *an = strcasestr(p, " an");
    size_t len = an ? (size_t)(an - p) : strlen(p);
    while (len > 0 && p[len - 1] == ' ') len--; /* Randleerzeichen kappen */
    if (len == 0 || len >= name_cap) return 0;

    memcpy(name_out, p, len);
    name_out[len] = '\0';
    return 1;
}

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

    flux_dialog_t dialog = FLUX_DIALOG_NONE;
    char pending_contact_name[128] = {0};
    char pending_email_to[256] = {0};
    char pending_email_subject[256] = {0};

    char call_name[256] = {0};
    char call_phone[64] = {0};

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

        if (screen == FLUX_SCREEN_CALL) {
            /* Jeder Tap oder Enter legt auf -- der simulierte Anruf hat
             * keine eigene Tastatur, also keine Tap-Geometrie zu pruefen. */
            if (ev.type == FLUX_EV_TAP || ev.type == FLUX_EV_ENTER) {
                screen = FLUX_SCREEN_ASSISTANT;
                snprintf(answer_buf, sizeof(answer_buf), "Aufgelegt (%s).", call_name);
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

            if (dialog == FLUX_DIALOG_CONTACT_NAME) {
                snprintf(pending_contact_name, sizeof(pending_contact_name), "%s", input_buf);
                input_buf[0] = '\0';
                dialog = FLUX_DIALOG_CONTACT_PHONE;
                snprintf(answer_buf, sizeof(answer_buf),
                         "Welche Telefonnummer soll \"%s\" bekommen?", pending_contact_name);
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            } else if (dialog == FLUX_DIALOG_CONTACT_PHONE) {
                char phone[64];
                snprintf(phone, sizeof(phone), "%s", input_buf);
                input_buf[0] = '\0';
                dialog = FLUX_DIALOG_NONE;
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 1);
                flux_ipc_add_contact(pending_contact_name, phone, answer_buf, sizeof(answer_buf));
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            } else if (dialog == FLUX_DIALOG_EMAIL_TO) {
                snprintf(pending_email_to, sizeof(pending_email_to), "%s", input_buf);
                input_buf[0] = '\0';
                dialog = FLUX_DIALOG_EMAIL_SUBJECT;
                snprintf(answer_buf, sizeof(answer_buf), "Betreff fuer die Mail an %s?", pending_email_to);
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            } else if (dialog == FLUX_DIALOG_EMAIL_SUBJECT) {
                snprintf(pending_email_subject, sizeof(pending_email_subject), "%s", input_buf);
                input_buf[0] = '\0';
                dialog = FLUX_DIALOG_EMAIL_BODY;
                snprintf(answer_buf, sizeof(answer_buf), "Was soll in der Mail stehen?");
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            } else if (dialog == FLUX_DIALOG_EMAIL_BODY) {
                char body[2048];
                snprintf(body, sizeof(body), "%s", input_buf);
                input_buf[0] = '\0';
                dialog = FLUX_DIALOG_NONE;
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 1);
                flux_ipc_send_email(pending_email_to, pending_email_subject, body,
                                     answer_buf, sizeof(answer_buf));
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            } else if (detect_contact_create(input_buf)) {
                input_buf[0] = '\0';
                dialog = FLUX_DIALOG_CONTACT_NAME;
                snprintf(answer_buf, sizeof(answer_buf), "Wie soll der Kontakt heissen?");
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            } else if (detect_email_create(input_buf)) {
                input_buf[0] = '\0';
                dialog = FLUX_DIALOG_EMAIL_TO;
                snprintf(answer_buf, sizeof(answer_buf), "An welche E-Mail-Adresse?");
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            } else if (detect_call(input_buf, call_name, sizeof(call_name))) {
                input_buf[0] = '\0';
                char lookup[256];
                if (flux_ipc_find_contact(call_name, lookup, sizeof(lookup))) {
                    char *tab = strchr(lookup, '\t');
                    if (tab) {
                        *tab = '\0';
                        snprintf(call_name, sizeof(call_name), "%s", lookup);
                        snprintf(call_phone, sizeof(call_phone), "%s", tab + 1);
                        screen = FLUX_SCREEN_CALL;
                        flux_ui_draw_call(&fb, call_name, call_phone);
                    }
                } else {
                    snprintf(answer_buf, sizeof(answer_buf), "%s", lookup);
                    flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
                }
            } else {
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 1); /* "Denke nach..." sofort zeigen */
                flux_ipc_ask(input_buf, answer_buf, sizeof(answer_buf));
                input_buf[0] = '\0';
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            }
        }
    }

    flux_input_close(&in);
    flux_fb_close(&fb);
    return 0;
}
