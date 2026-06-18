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
    FLUX_SCREEN_FILE_VIEWER,
    FLUX_SCREEN_NOTIFY,      /* Benachrichtigungs-Overlay (Wisch nach unten) */
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

/* last_q: die zuletzt gestellte Frage (Nutzer-Blase, rechts); leer wenn
 * noch nichts gefragt wurde. input: aktuell tippender Text im Eingabefeld.
 * answer: letzte KI-Antwort (KI-Blase, links). thinking: Lade-Animation. */
void flux_ui_draw_assistant(flux_fb_t *fb, const char *last_q,
                              const char *input, const char *answer, int thinking);

int flux_ui_kbd_top(const flux_fb_t *fb);
int flux_ui_kbd_hit(const flux_fb_t *fb, int x, int y,
                     char *out_ch, int *out_backspace, int *out_enter);

/* Schnellzugriff-Leiste unter der Statusbar: 0 = kein Treffer,
 * 1 = "Einstellungen", 2 = "Dateien". */
int flux_ui_quickrow_hit(const flux_fb_t *fb, int x, int y);

/* Mikrofon-Knopf rechts neben der Eingabezeile. */
int flux_ui_mic_hit(const flux_fb_t *fb, int x, int y);

/* Kopieren- und Einfuegen-Knoepfe in der Eingabeleiste.
 * [C] kopiert den aktuellen Eingabetext in die Zwischenablage,
 * [V] fuegt ihn wieder ein -- Touch-first, kein langer Druck noetig. */
int flux_ui_copy_hit(const flux_fb_t *fb, int x, int y);
int flux_ui_paste_hit(const flux_fb_t *fb, int x, int y);

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
 * 0 sein (leeres Verzeichnis). path wird oben angezeigt.
 * selected_idx: markierter Eintrag (-1 = keiner). */

void flux_ui_draw_files(flux_fb_t *fb, const char *path, const char **names,
                         const char **metas, int n, int truncated, int selected_idx);

/* ---- Datei-Betrachter -----------------------------------------------
 * Zeigt den Textinhalt einer Datei an. scroll_y gibt die erste sichtbare
 * Zeile an (fuer vertikales Scrollen). */

void flux_ui_draw_file_viewer(flux_fb_t *fb, const char *path,
                               const char *content, int scroll_line);

/* Gibt 1 wenn der "Zurueck"-Bereich getroffen, 0 sonst.
 * scroll_delta gibt Anzahl Zeilen hoch (<0) oder runter (>0) an. */
int flux_ui_viewer_hit(const flux_fb_t *fb, int x, int y, int *scroll_delta, int *back);

/* ---- Loeschen-Knopf in der Dateien-Ansicht -------------------------
 * Sichtbar wenn selected_idx >= 0. Gibt 1 wenn der Loeschen-Knopf
 * getroffen wurde. */
int flux_ui_files_delete_hit(const flux_fb_t *fb, int x, int y);

/* "Neuer Ordner"-Knopf oben rechts im Dateibrowser. */
int flux_ui_files_new_btn_hit(const flux_fb_t *fb, int x, int y);

/* ---- Benachrichtigungs-Overlay ------------------------------------- */

/* Zeigt Uhrzeit, Batterie, WLAN, Wetter, Alarme und Erinnerungen.
 * Wird durch Wisch nach unten auf dem Assistenten-Bildschirm geoeffnet. */
void flux_ui_draw_notify(flux_fb_t *fb);

/* Gibt 1 wenn der Bildschirm per Tap geschlossen werden soll. */
int flux_ui_notify_hit(const flux_fb_t *fb, int x, int y);

/* ---- Farbthema ----------------------------------------------------- */

/* Setzt die Akzentfarbe fuer alle Bildschirme.
 * Vordefinierte Werte: 0x4FD1C5 (Teal), 0x3B82F6 (Blau),
 * 0xA855F7 (Lila), 0xF97316 (Orange), 0x22C55E (Gruen). */
void flux_ui_set_accent(uint32_t rgb);

#endif
