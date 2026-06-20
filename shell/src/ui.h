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
    FLUX_SCREEN_NOTIFY,       /* Benachrichtigungs-Overlay (Wisch nach unten) */
    FLUX_SCREEN_CALENDAR,    /* Kalenderansicht */
    FLUX_SCREEN_CONTACTS,    /* Kontaktliste */
    FLUX_SCREEN_GALLERY,     /* Fotogalerie */
    FLUX_SCREEN_IMAGE_VIEWER, /* Einzelbild-Betrachter mit KI-Analyse */
    FLUX_SCREEN_MEMORY,      /* KI-Gedaechtnis-Liste */
    FLUX_SCREEN_MEETING,     /* Meeting-Mitschrift (Audio-Transkription) */
    FLUX_SCREEN_SEARCH,      /* Semantische KI-Suche ueber alles */
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

/* Gibt 1 wenn die untere Leiste oder der Scroll-Bereich getroffen wurde.
 * scroll_delta gibt Anzahl Zeilen hoch (<0) oder runter (>0) an.
 * *back: "Zurueck" getroffen. *ai: "KI fragen"-Knopf getroffen (darf NULL
 * sein -- dann zaehlt die ganze untere Leiste als Zurueck). */
int flux_ui_viewer_hit(const flux_fb_t *fb, int x, int y,
                       int *scroll_delta, int *back, int *ai);

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

/* ---- Kalender -------------------------------------------------------
 * Monatsgitter. today_day: heutiger Tag (1-31, 0=unbekannt).
 * selected_day: markierter Tag (0=keiner). event_strs: Ereignis-Strings
 * fuer den ausgewaehlten Tag (aus /etc/flux/calendar.txt). */

void flux_ui_draw_calendar(flux_fb_t *fb, int year, int month,
                            int today_day, int selected_day,
                            const char **event_strs, int n_events);

/* Gibt 1 bei Treffer. Setzt *day (1-31) bei Tagszellen,
 * *prev_month / *next_month bei den Navigationspfeilen. */
int flux_ui_calendar_hit(const flux_fb_t *fb, int x, int y,
                          int *day, int *prev_month, int *next_month);

/* ---- Kontakte -------------------------------------------------------
 * names/details parallel (z.B. "+49 151 ...  ich@mail.de").
 * selected_idx: -1 = keiner. Nutzt dieselbe Listen-Infrastruktur wie
 * Einstellungen/Dateien. */

void flux_ui_draw_contacts(flux_fb_t *fb, const char **names,
                            const char **details, int n, int selected_idx);

/* ---- Fotogalerie ---------------------------------------------------- */

/* Zeigt eine Liste von Fotonamen mit Datumsangaben.
 * names/dates parallel. selected_idx: -1 = kein. */
void flux_ui_draw_gallery(flux_fb_t *fb, const char **names, const char **dates,
                           int n, int selected_idx);

/* Gibt 1 wenn der Kamera-Aufnahme-Knopf getroffen. */
int flux_ui_gallery_camera_hit(const flux_fb_t *fb, int x, int y);

/* ---- Bild-Betrachter ------------------------------------------------ */

/* Zeigt ein skaliertes Bild (bereits auf img_w x img_h skaliert als RGB32).
 * ai_caption: KI-Beschreibung (leer = noch nicht analysiert).
 * analyzing: 1 = Analyse laeuft (Lade-Indikator). */
void flux_ui_draw_image_viewer(flux_fb_t *fb, const char *filename,
                                const uint32_t *pixels, int img_w, int img_h,
                                const char *ai_caption, int analyzing);

/* Hit-Test fuer den Bild-Betrachter.
 * Setzt *back, *analyze oder *del auf 1 bei Treffer. */
int flux_ui_image_viewer_hit(const flux_fb_t *fb, int x, int y,
                              int *back, int *analyze, int *del);

/* ---- Tap-Ripple-Animation ------------------------------------------
 * Zeichnet einen Rahmen des Expanding-Ring-Effekts bei (cx,cy).
 * frame: 0 (klein) bis 4 (gross+verblasst). Ruft flux_fb_present()
 * NICHT auf -- Aufrufer kuemmert sich darum. */

void flux_ui_draw_ripple(flux_fb_t *fb, int cx, int cy, int frame);

/* ---- Globaler KI-Kontext-Overlay ----------------------------------- */

/* Zeichnet den halbtransparenten KI-Overlay ueber den aktuellen Screen.
 * context_label: z.B. "Datei: notizen.txt" oder "Kalender: Juni 2026".
 * input_text:    aktuelle Eingabe des Nutzers (leer = Platzhalter).
 * result_text:   KI-Antwort (leer = noch keine Antwort). */
void flux_ui_draw_ai_overlay(flux_fb_t *fb, const char *context_label,
                              const char *input_text, const char *result_text);

/* Hit-Test: gibt 1 bei Treffer.
 * *cancel: Abbrechen-Button oder Tap ausserhalb.
 * *submit: "Fragen"-Button.
 * *save_result: "Als Datei speichern"-Button. */
int flux_ui_ai_overlay_hit(const flux_fb_t *fb, int x, int y,
                            int *cancel, int *submit, int *save_result);

/* ---- Semantische KI-Suche ------------------------------------------ */

/* Suchbildschirm: ein Eingabefeld + KI-Ergebnisliste.
 * query: aktueller Suchbegriff (leer = Platzhalter anzeigen).
 * results: Array von Ergebnis-Strings (Source: Text), n Eintraege.
 * searching: 1 waehrend die KI sucht (Lade-Animation). */
void flux_ui_draw_search(flux_fb_t *fb, const char *query,
                          const char **results, int n, int searching);

/* Hit-Test: gibt 1 bei Treffer.
 * *back: Zurueck-Knopf getroffen.
 * *result_idx: Ergebnis-Zeile getroffen (-1 = keins). */
int flux_ui_search_hit(const flux_fb_t *fb, int x, int y,
                       int *back, int *result_idx);

/* ---- Spracheingabe-Overlay ----------------------------------------- */

/* Zeichnet den Aufnahme-Indikator ueber dem aktuellen Screen.
 * elapsed_s: Aufnahmedauer in Sekunden (fuer Timer).
 * Ruft flux_fb_present() auf. */
void flux_ui_draw_voice_overlay(flux_fb_t *fb, int elapsed_s);

/* ---- Meeting-Mitschrift -------------------------------------------- */

/* Zeigt den Meeting-Bildschirm.
 * recording: 1 wenn Aufnahme laeuft, 0 wenn bereit/beendet.
 * elapsed_s: vergangene Aufnahmezeit in Sekunden.
 * transcript: bisheriger Transkriptionstext (leer = noch keine Transkription).
 * status_msg: Statustext unter dem Knopf (z.B. "Aufnahme laeuft..." oder
 *             "Transkription nicht verfuegbar"). */
void flux_ui_draw_meeting(flux_fb_t *fb, int recording, int elapsed_s,
                           const char *transcript, const char *status_msg);

/* Hit-Test: setzt *rec_btn=1 fuer den Aufnahme-Knopf, *save_btn=1 fuer
 * "Speichern", *back_btn=1 fuer Zurueck. */
int flux_ui_meeting_hit(const flux_fb_t *fb, int x, int y,
                        int *rec_btn, int *save_btn, int *back_btn);

/* ---- KI-Gedaechtnis ------------------------------------------------- */

/* Zeigt alle Eintraege aus /etc/flux/memory.txt in einer scrollbaren Liste.
 * entries: Array von Strings (bereits geladen). n: Anzahl Eintraege.
 * scroll: erster sichtbarer Eintrag (fuer vertikales Scrollen). */
void flux_ui_draw_memory(flux_fb_t *fb, const char **entries, int n, int scroll);

/* ---- Farbthema ----------------------------------------------------- */

/* Setzt die Akzentfarbe fuer alle Bildschirme.
 * Vordefinierte Werte: 0x4FD1C5 (Teal), 0x3B82F6 (Blau),
 * 0xA855F7 (Lila), 0xF97316 (Orange), 0x22C55E (Gruen). */
void flux_ui_set_accent(uint32_t rgb);

#endif
