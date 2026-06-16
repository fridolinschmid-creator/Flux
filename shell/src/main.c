/* flux-shell -- der einzige "App"-Prozess, der am Anfang laeuft.
 * Kein Homescreen mit Icon-Grid: man entsperrt direkt in den
 * KI-Assistenten (optional hinter einem PIN-Code). Zeichnet auf den
 * Framebuffer, fragt fluxaid ueber den Unix-Socket. Bedienung
 * touch-first (Wischen, eigene Bildschirmtastatur) -- eine
 * Hardware-Tastatur funktioniert weiterhin, ist aber nicht mehr
 * Voraussetzung (siehe input.h).
 *
 * Einstellungen und Dateien sind ueber den Assistenten erreichbar
 * (Schnellzugriff-Knoepfe oder Tippen/Sprechen von "Einstellungen"/
 * "Dateien"), nicht ueber ein zweites App-Grid -- siehe ui.h.
 *
 * Will die KI eine Mail/SMS/einen Anruf ausloesen, antwortet fluxaid
 * mit einem ACTION:-Block (siehe fluxai/src/provider.c). Dieser wird
 * hier geparst (action.h) und fuehrt NIE direkt etwas aus -- es gibt
 * immer erst den Bestaetigungs-Dialog (FLUX_SCREEN_CONFIRM).
 */
#include "fb.h"
#include "input.h"
#include "ipc.h"
#include "ui.h"
#include "action.h"
#include "../../common/flux_protocol.h"
#include "../../common/flux_config.h"
#include "../../common/flux_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/select.h>

#define FLUX_PIN_LEN     4
#define FLUX_FILES_MAX   12
#define FLUX_SETTINGS_N  7

typedef enum { EDIT_NONE = 0, EDIT_ACTION_BODY, EDIT_SETTING_FIELD } edit_target_t;

/* ---- Einstellungen: Feldliste -------------------------------------
 * main.c maskiert Geheimnisse, bevor sie an ui.c gehen (siehe ui.h) --
 * ui.c/draw_settings weiss nichts von der Konfigurationsdatei. */

static const char *setting_keys[FLUX_SETTINGS_N] = {
    "pin_hash", "smtp_host", "smtp_port", "smtp_user", "smtp_pass", "smtp_from", "api_key",
};
static const char *setting_labels[FLUX_SETTINGS_N] = {
    "PIN-Code", "SMTP-Server", "SMTP-Port", "SMTP-Benutzer",
    "SMTP-Passwort", "Absender-Adresse", "Cloud-API-Key",
};
static const int setting_secret[FLUX_SETTINGS_N] = { 1, 0, 0, 0, 1, 0, 1 };

static char setting_values_buf[FLUX_SETTINGS_N][200];
static const char *setting_values[FLUX_SETTINGS_N];

static void load_settings_values(void) {
    for (int i = 0; i < FLUX_SETTINGS_N; i++) {
        char raw[200] = {0};
        flux_config_get(setting_keys[i], raw, sizeof(raw));
        if (strcmp(setting_keys[i], "pin_hash") == 0) {
            snprintf(setting_values_buf[i], sizeof(setting_values_buf[0]), "%s",
                      raw[0] ? "gesetzt" : "nicht gesetzt");
        } else if (setting_secret[i]) {
            snprintf(setting_values_buf[i], sizeof(setting_values_buf[0]), "%s",
                      raw[0] ? "********" : "(nicht gesetzt)");
        } else {
            snprintf(setting_values_buf[i], sizeof(setting_values_buf[0]), "%s",
                      raw[0] ? raw : "(nicht gesetzt)");
        }
        setting_values[i] = setting_values_buf[i];
    }
}

static void apply_setting_edit(int index, const char *value) {
    if (strcmp(setting_keys[index], "pin_hash") == 0) {
        if (value[0] == '\0') {
            flux_config_set("pin_hash", "");
        } else {
            char hash[65];
            flux_sha256_hex(value, hash);
            flux_config_set("pin_hash", hash);
        }
    } else {
        flux_config_set(setting_keys[index], value);
    }
}

/* ---- Dateien: einfacher Read-Only-Browser --------------------------
 * Kein Loeschen/Umbenennen -- ein erster, sicherer Schritt (siehe
 * ui.c-Kommentar bei flux_ui_draw_files). */

static char files_path[1024] = "/";
static char file_names_buf[FLUX_FILES_MAX][256];
static char file_metas_buf[FLUX_FILES_MAX][32];
static int  file_is_dir[FLUX_FILES_MAX];
static const char *file_names[FLUX_FILES_MAX];
static const char *file_metas[FLUX_FILES_MAX];
static int  file_n = 0;
static int  file_truncated = 0;

static void format_size(off_t size, char *out, size_t cap) {
    if (size < 1024) snprintf(out, cap, "%lld B", (long long)size);
    else if (size < 1024 * 1024) snprintf(out, cap, "%.1f KB", size / 1024.0);
    else snprintf(out, cap, "%.1f MB", size / (1024.0 * 1024.0));
}

static void load_files(const char *path) {
    file_n = 0;
    file_truncated = 0;
    int has_parent = strcmp(path, "/") != 0;
    int capacity = FLUX_FILES_MAX - (has_parent ? 1 : 0);

    struct dirent **entries;
    int total = scandir(path, &entries, NULL, alphasort);
    if (total < 0) total = 0;

    if (has_parent) {
        snprintf(file_names_buf[file_n], sizeof(file_names_buf[0]), "..");
        snprintf(file_metas_buf[file_n], sizeof(file_metas_buf[0]), "Ordner");
        file_is_dir[file_n] = 1;
        file_n++;
    }

    int real_count = 0;
    for (int i = 0; i < total; i++) {
        const char *name = entries[i]->d_name;
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
            if (real_count < capacity) {
                char full[1280];
                snprintf(full, sizeof(full), "%s/%s", path, name);
                struct stat st;
                int is_dir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
                snprintf(file_names_buf[file_n], sizeof(file_names_buf[0]), "%s", name);
                if (is_dir) snprintf(file_metas_buf[file_n], sizeof(file_metas_buf[0]), "Ordner");
                else format_size(st.st_size, file_metas_buf[file_n], sizeof(file_metas_buf[0]));
                file_is_dir[file_n] = is_dir;
                file_n++;
            }
            real_count++;
        }
        free(entries[i]);
    }
    free(entries);
    if (real_count > capacity) file_truncated = 1;

    for (int i = 0; i < file_n; i++) {
        file_names[i] = file_names_buf[i];
        file_metas[i] = file_metas_buf[i];
    }
}

static void files_go_parent(void) {
    char *slash = strrchr(files_path, '/');
    if (slash && slash != files_path) *slash = '\0';
    else strcpy(files_path, "/");
}

static void files_enter(const char *name) {
    size_t len = strlen(files_path);
    if (len > 1) snprintf(files_path + len, sizeof(files_path) - len, "/%s", name);
    else snprintf(files_path, sizeof(files_path), "/%s", name);
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
    char answer_buf[FLUX_MAX_RESPONSE] = {0};
    char edit_buf[4096] = {0};

    char pin_buf[FLUX_PIN_LEN + 1] = {0};
    int  pin_len = 0;
    int  pin_error = 0;

    flux_action_t pending_action;
    memset(&pending_action, 0, sizeof(pending_action));

    edit_target_t edit_target = EDIT_NONE;
    int edit_setting_index = 0;

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
                char stored[128] = {0};
                if (flux_config_get("pin_hash", stored, sizeof(stored)) && stored[0]) {
                    screen = FLUX_SCREEN_PIN;
                    pin_len = 0;
                    pin_error = 0;
                    flux_ui_draw_pin(&fb, pin_len, pin_error);
                } else {
                    screen = FLUX_SCREEN_ASSISTANT;
                    input_buf[0] = '\0';
                    answer_buf[0] = '\0';
                    flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
                }
            }
            continue;
        }

        if (screen == FLUX_SCREEN_PIN) {
            char digit = 0;
            int backspace = 0;
            int hit = 0;
            if (ev.type == FLUX_EV_TAP) {
                hit = flux_ui_pin_hit(&fb, ev.x, ev.y, &digit, &backspace);
            } else if (ev.type == FLUX_EV_CHAR && ev.ch >= '0' && ev.ch <= '9') {
                digit = ev.ch;
                hit = 1;
            } else if (ev.type == FLUX_EV_BACKSPACE) {
                backspace = 1;
                hit = 1;
            }
            if (!hit) continue;

            pin_error = 0;
            if (backspace) {
                if (pin_len > 0) pin_len--;
            } else if (digit && pin_len < FLUX_PIN_LEN) {
                pin_buf[pin_len++] = digit;
            }

            if (pin_len == FLUX_PIN_LEN) {
                pin_buf[pin_len] = '\0';
                char hash[65];
                flux_sha256_hex(pin_buf, hash);
                char stored[128] = {0};
                flux_config_get("pin_hash", stored, sizeof(stored));
                pin_len = 0;
                pin_buf[0] = '\0';
                if (strcmp(hash, stored) == 0) {
                    screen = FLUX_SCREEN_ASSISTANT;
                    input_buf[0] = '\0';
                    answer_buf[0] = '\0';
                    flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
                } else {
                    pin_error = 1;
                    flux_ui_draw_pin(&fb, pin_len, pin_error);
                }
            } else {
                flux_ui_draw_pin(&fb, pin_len, pin_error);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_CONFIRM) {
            if (ev.type != FLUX_EV_TAP) continue;
            flux_confirm_hit_t hit = flux_ui_confirm_hit(&fb, ev.x, ev.y);
            if (hit == FLUX_CONFIRM_CANCEL) {
                screen = FLUX_SCREEN_ASSISTANT;
                snprintf(answer_buf, sizeof(answer_buf), "Abgebrochen.");
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            } else if (hit == FLUX_CONFIRM_EDIT) {
                snprintf(edit_buf, sizeof(edit_buf), "%s", pending_action.body);
                edit_target = EDIT_ACTION_BODY;
                screen = FLUX_SCREEN_EDIT_BODY;
                flux_ui_draw_edit_body(&fb, edit_buf);
            } else if (hit == FLUX_CONFIRM_SEND) {
                char req[FLUX_MAX_LINE];
                flux_action_build_request(&pending_action, req, sizeof(req));
                flux_ipc_send_raw(req, answer_buf, sizeof(answer_buf));
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_EDIT_BODY) {
            flux_event_type_t kind = ev.type;
            char ch = ev.ch;

            if (kind == FLUX_EV_TAP) {
                char tap_ch = 0;
                int tap_backspace = 0, tap_enter = 0;
                if (!flux_ui_kbd_hit(&fb, ev.x, ev.y, &tap_ch, &tap_backspace, &tap_enter))
                    continue;
                if (tap_backspace) kind = FLUX_EV_BACKSPACE;
                else if (tap_enter) kind = FLUX_EV_ENTER;
                else { kind = FLUX_EV_CHAR; ch = tap_ch; }
            } else if (kind == FLUX_EV_SWIPE_UP) {
                continue;
            }

            if (kind == FLUX_EV_CHAR) {
                size_t len = strlen(edit_buf);
                if (len + 1 < sizeof(edit_buf)) {
                    edit_buf[len] = ch;
                    edit_buf[len + 1] = '\0';
                }
                flux_ui_draw_edit_body(&fb, edit_buf);
            } else if (kind == FLUX_EV_BACKSPACE) {
                size_t len = strlen(edit_buf);
                if (len > 0) edit_buf[len - 1] = '\0';
                flux_ui_draw_edit_body(&fb, edit_buf);
            } else if (kind == FLUX_EV_ENTER) {
                if (edit_target == EDIT_ACTION_BODY) {
                    snprintf(pending_action.body, sizeof(pending_action.body), "%s", edit_buf);
                    screen = FLUX_SCREEN_CONFIRM;
                    flux_ui_draw_confirm(&fb, flux_action_type_label(pending_action.type),
                                          pending_action.to, pending_action.subject, pending_action.body);
                } else {
                    apply_setting_edit(edit_setting_index, edit_buf);
                    load_settings_values();
                    screen = FLUX_SCREEN_SETTINGS;
                    flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
                }
            }
            continue;
        }

        if (screen == FLUX_SCREEN_SETTINGS) {
            if (ev.type != FLUX_EV_TAP) continue;
            int idx, back;
            if (!flux_ui_list_hit(&fb, ev.x, ev.y, FLUX_SETTINGS_N, &idx, &back)) continue;
            if (back) {
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            } else {
                edit_target = EDIT_SETTING_FIELD;
                edit_setting_index = idx;
                if (setting_secret[idx]) edit_buf[0] = '\0';
                else flux_config_get(setting_keys[idx], edit_buf, sizeof(edit_buf));
                screen = FLUX_SCREEN_EDIT_BODY;
                flux_ui_draw_edit_body(&fb, edit_buf);
            }
            continue;
        }

        if (screen == FLUX_SCREEN_FILES) {
            if (ev.type != FLUX_EV_TAP) continue;
            int idx, back;
            if (!flux_ui_list_hit(&fb, ev.x, ev.y, file_n, &idx, &back)) continue;
            if (back) {
                screen = FLUX_SCREEN_ASSISTANT;
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            } else if (idx < file_n && file_is_dir[idx]) {
                if (strcmp(file_names_buf[idx], "..") == 0) files_go_parent();
                else files_enter(file_names_buf[idx]);
                load_files(files_path);
                flux_ui_draw_files(&fb, files_path, file_names, file_metas, file_n, file_truncated);
            }
            continue;
        }

        /* FLUX_SCREEN_ASSISTANT -- Quickrow/Mikrofon-Knopf zuerst pruefen,
         * sonst Taps auf die Bildschirmtastatur wie Hardware-Eingaben
         * behandeln (gemeinsamer Verarbeitungspfad). */
        if (ev.type == FLUX_EV_TAP) {
            int quick = flux_ui_quickrow_hit(&fb, ev.x, ev.y);
            if (quick == 1) {
                load_settings_values();
                screen = FLUX_SCREEN_SETTINGS;
                flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
                continue;
            } else if (quick == 2) {
                strcpy(files_path, "/");
                load_files(files_path);
                screen = FLUX_SCREEN_FILES;
                flux_ui_draw_files(&fb, files_path, file_names, file_metas, file_n, file_truncated);
                continue;
            }
            if (flux_ui_mic_hit(&fb, ev.x, ev.y)) {
                snprintf(answer_buf, sizeof(answer_buf),
                         "Kein Mikrofon erkannt -- Spracheingabe ist in dieser Umgebung noch nicht verfuegbar.");
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
                continue;
            }
        }

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

            if (strcasecmp(input_buf, "einstellungen") == 0 || strcasecmp(input_buf, "settings") == 0) {
                input_buf[0] = '\0';
                load_settings_values();
                screen = FLUX_SCREEN_SETTINGS;
                flux_ui_draw_settings(&fb, setting_labels, setting_values, FLUX_SETTINGS_N);
                continue;
            }
            if (strcasecmp(input_buf, "dateien") == 0 || strcasecmp(input_buf, "files") == 0) {
                input_buf[0] = '\0';
                strcpy(files_path, "/");
                load_files(files_path);
                screen = FLUX_SCREEN_FILES;
                flux_ui_draw_files(&fb, files_path, file_names, file_metas, file_n, file_truncated);
                continue;
            }

            flux_ui_draw_assistant(&fb, input_buf, answer_buf, 1); /* "Denke nach..." sofort zeigen */
            flux_ipc_ask(input_buf, answer_buf, sizeof(answer_buf));
            input_buf[0] = '\0';

            if (flux_action_parse(answer_buf, &pending_action)) {
                screen = FLUX_SCREEN_CONFIRM;
                flux_ui_draw_confirm(&fb, flux_action_type_label(pending_action.type),
                                      pending_action.to, pending_action.subject, pending_action.body);
            } else {
                flux_ui_draw_assistant(&fb, input_buf, answer_buf, 0);
            }
        }
    }

    flux_input_close(&in);
    flux_fb_close(&fb);
    return 0;
}
