/* ui.h -- alle Bildschirme von Flux. Kein App-Grid: Lockscreen ->
 * (optional PIN) -> KI-Assistent ist der Homescreen. Einstellungen
 * und Dateien sind ueber den Assistenten erreichbar (Tippen/Sprechen
 * von "Einstellungen"/"Dateien" oder die zwei Schnellzugriff-Knoepfe),
 * nicht ueber ein zweites App-Grid -- das waere der Bruch mit dem
 * eigentlichen Designprinzip von Flux.
 *
 * Touch-first: jede Liste/Tastatur hat eine Geometrie-Funktion fuer
 * Zeichnen UND Hit-Testing, damit beide nie auseinanderlaufen.
 */
#ifndef FLUX_UI_H
#define FLUX_UI_H

#include "fb.h"

typedef enum {
    FLUX_SCREEN_LOCK,
    FLUX_SCREEN_PIN,
    FLUX_SCREEN_ASSISTANT,
    FLUX_SCREEN_CONFIRM,
    FLUX_SCREEN_EDIT_BODY,
    FLUX_SCREEN_SETTINGS,
    FLUX_SCREEN_FILES,
} flux_screen_t;

typedef enum {
    FLUX_CONFIRM_NONE = 0,
    FLUX_CONFIRM_EDIT,
    FLUX_CONFIRM_CANCEL,
    FLUX_CONFIRM_SEND,
} flux_confirm_hit_t;

/* ---- Lockscreen / PIN ------------------------------------------- */

void flux_ui_draw_lock(flux_fb_t *fb);

/* entered: Anzahl bereits eingegebener Ziffern (fuer die Punktanzeige).
 * error: 1, wenn der zuletzt eingegebene Code falsch war. */
void flux_ui_draw_pin(flux_fb_t *fb, int entered, int error);

/* Bildschirmkoordinate -> Zifferntaste des PIN-Pads. out_digit ist bei
 * Backspace undefiniert. Gibt 1 bei Treffer. */
int flux_ui_pin_hit(const flux_fb_t *fb, int x, int y, char *out_digit, int *out_backspace);

/* ---- KI-Assistent (Homescreen) ------------------------------------ */

void flux_ui_draw_assistant(flux_fb_t *fb, const char *input, const char *answer, int thinking);

int flux_ui_kbd_top(const flux_fb_t *fb);
int flux_ui_kbd_hit(const flux_fb_t *fb, int x, int y,
                     char *out_ch, int *out_backspace, int *out_enter);

/* Schnellzugriff-Leiste unter der Statusbar: 0 = kein Treffer,
 * 1 = "Einstellungen", 2 = "Dateien". */
int flux_ui_quickrow_hit(const flux_fb_t *fb, int x, int y);

/* Mikrofon-Knopf rechts neben der Eingabezeile. */
int flux_ui_mic_hit(const flux_fb_t *fb, int x, int y);

/* ---- Bestaetigungs-Dialog (KI will Mail/SMS/Anruf ausloesen) ------ */

void flux_ui_draw_confirm(flux_fb_t *fb, const char *type_label,
                           const char *to, const char *subject, const char *body);
flux_confirm_hit_t flux_ui_confirm_hit(const flux_fb_t *fb, int x, int y);

/* ---- Text bearbeiten (vor dem Senden einer Aktion) ---------------- */

void flux_ui_draw_edit_body(flux_fb_t *fb, const char *body);

/* ---- Einstellungen -------------------------------------------------
 * labels/values sind Anzeige-Strings (main.c maskiert Geheimnisse vor
 * dem Aufruf, ui.c weiss nichts ueber die Konfigurationsdatei). */

void flux_ui_draw_settings(flux_fb_t *fb, const char **labels, const char **values, int n);
/* Gibt 1 zurueck und setzt *out_index bei Treffer auf eine Zeile,
 * oder setzt *out_back auf 1, wenn der Zurueck-Knopf getroffen wurde. */
int flux_ui_list_hit(const flux_fb_t *fb, int x, int y, int n, int *out_index, int *out_back);

/* ---- Dateien --------------------------------------------------------
 * names/metas (z.B. "Ordner" oder "12 KB") parallel zu names, n darf
 * 0 sein (leeres Verzeichnis). path wird oben angezeigt. */

void flux_ui_draw_files(flux_fb_t *fb, const char *path, const char **names,
                         const char **metas, int n, int truncated);

#endif
