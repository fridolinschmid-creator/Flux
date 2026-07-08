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
    FLUX_SCREEN_MEMORY,       /* KI-Gedaechtnis-Liste */
    FLUX_SCREEN_MEETING,      /* Meeting-Mitschrift (Audio-Transkription) */
    FLUX_SCREEN_SEARCH,       /* Semantische KI-Suche ueber alles */
    FLUX_SCREEN_WIFI,         /* WLAN-Netze scannen und verbinden */
    FLUX_SCREEN_JOURNAL,       /* Tages-Journal Eintraege (Liste + Betrachter) */
    FLUX_SCREEN_VOICE_ENROLL,  /* Stimme einlernen fuer zweiten Faktor */
    FLUX_SCREEN_VOICE_VERIFY,  /* Stimm-Verifizierung nach PIN (zweiter Faktor) */
    FLUX_SCREEN_ALARM_APP,     /* Wecker/Timer-Verwaltungs-Screen */
    FLUX_SCREEN_ALARM,         /* Vollbild-Alarm (Wecker klingelt) */
    FLUX_SCREEN_CALL,          /* Vollbild-Anruf (annehmen/auflegen) */
    FLUX_SCREEN_HABITS,        /* Nutzungsgewohnheiten (habits.txt) */
} flux_screen_t;

typedef enum {
    FLUX_CONFIRM_NONE = 0,
    FLUX_CONFIRM_CANCEL,
    FLUX_CONFIRM_SEND,
    FLUX_CONFIRM_EDIT,          /* Stift-Knopf (Bearbeiten der Nachricht) */
    FLUX_CONFIRM_EDIT_TO,       /* Tap auf den Empfaenger */
    FLUX_CONFIRM_EDIT_SUBJECT,  /* Tap auf den Betreff */
    FLUX_CONFIRM_EDIT_BODY,     /* Tap auf den Nachrichtentext */
} flux_confirm_hit_t;

/* ---- Lockscreen / PIN ------------------------------------------- */

void flux_ui_draw_lock(flux_fb_t *fb);

/* Schaltet den Lockscreen-Mikrofon-Chip "Zum Entsperren sprechen" ein/aus.
 * main.c setzt das nur, wenn eine Stimme eingelernt ist, der Toggle
 * voice_unlock_lock aktiv ist UND KEINE PIN gesetzt ist -- die Stimme darf
 * eine PIN nie ersetzen (Sicherheit vor Bequemlichkeit). */
void flux_ui_set_lock_voice_hint(int on);

/* Hit-Test fuer den Lockscreen-Mikrofon-Chip. Gibt 1 nur bei sichtbarem
 * Chip und Treffer zurueck (sonst 0 -- der Wisch bleibt unberuehrt). */
int flux_ui_lock_voice_hit(const flux_fb_t *fb, int x, int y);

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

/* Eingangsanimation der Schnellzugriff-Knoepfe: 'shown' Knoepfe voll,
 * der naechste auf grow_pct (0-100). Standard 4/100 (alle sichtbar). */
void flux_ui_set_quick_reveal(int shown, int grow_pct);

/* Vorschlags-Chip im leeren Assistenten-Zustand: gibt 1..4 zurueck
 * (Mail/Wecker/Suche/Termin) oder 0. Nur im leeren Zustand auswerten. */
int flux_ui_suggest_hit(const flux_fb_t *fb, int x, int y);

/* ---- Tastatur-Sichtbarkeit (Assistent) ----------------------------- *
 * Die Tastatur ist auf dem Assistenten ein-/ausblendbar: Tippen aufs
 * Feld zeigt sie, Wischen verbirgt sie. */
void flux_ui_set_kbd_open(int open);
int  flux_ui_kbd_is_open(void);

/* Tap auf das Eingabefeld "Schreib etwas..." (oeffnet die Tastatur). */
int flux_ui_input_field_hit(const flux_fb_t *fb, int x, int y);

/* Bis zu 3 Autovervollstaendigungs-Vorschlaege fuer den zuletzt getippten
 * Wortanfang in `input`. out[i] zeigt auf statischen Speicher. */
int flux_ui_kbd_words(const char *input, const char *out[3]);

/* Treffer in der Vorschlags-/Werkzeugleiste ueber der Tastatur. */
typedef enum {
    FLUX_STRIP_NONE = 0,
    FLUX_STRIP_WORD,    /* Autovervollstaendigungs-Wort (Index via word_idx) */
    FLUX_STRIP_COPY,
    FLUX_STRIP_PASTE,
    FLUX_STRIP_VOICE,
} flux_strip_hit_t;
flux_strip_hit_t flux_ui_strip_hit(const flux_fb_t *fb, int x, int y, int *word_idx);

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
/* has_subject: 1 wenn eine Betreff-Zeile angezeigt wird (nur bei Mail) --
 * noetig, damit der Hit-Test die antippbaren Zeilen richtig zuordnet. */
flux_confirm_hit_t flux_ui_confirm_hit(const flux_fb_t *fb, int x, int y, int has_subject);

/* Bestaetigungs-Dialog fuer eine Einstellungsaenderung (KI will eine
 * Systemeinstellung setzen). desc ist eine fertige deutsche Beschreibung
 * (z.B. "Helligkeit -> 50%"), key/value die technischen Werte. */
void flux_ui_draw_confirm_setting(flux_fb_t *fb, const char *desc,
                                  const char *key, const char *value);
flux_confirm_hit_t flux_ui_confirm_setting_hit(const flux_fb_t *fb, int x, int y);

/* ---- Text bearbeiten (vor dem Senden einer Aktion) ---------------- */

void flux_ui_draw_edit_body(flux_fb_t *fb, const char *body);

/* Setzt den Titel der Bearbeiten-Maske (NULL/leer -> "Text bearbeiten"). */
void flux_ui_set_edit_title(const char *title);

/* ---- Einstellungen -------------------------------------------------
 * labels/values sind Anzeige-Strings (main.c maskiert Geheimnisse vor
 * dem Aufruf, ui.c weiss nichts ueber die Konfigurationsdatei). */

void flux_ui_draw_settings(flux_fb_t *fb, const char **labels, const char **values, int n);

/* Symbol-IDs fuer die Einstellungs-Zeilen (links neben dem Text). */
enum {
    FLUX_SICON_NONE = 0, FLUX_SICON_LOCK, FLUX_SICON_AI, FLUX_SICON_KEY,
    FLUX_SICON_CHIP, FLUX_SICON_MAIL, FLUX_SICON_WIFI, FLUX_SICON_SEARCH,
    FLUX_SICON_THEME, FLUX_SICON_CLOCK, FLUX_SICON_SPEAKER,
};

/* Setzt das Symbol-Array fuer die Einstellungen (parallel zu labels). */
void flux_ui_set_setting_icons(const int *icons);

/* Zwischenframe der Einstellungs-Eingangsanimation: rows 0..shown-1 voll,
 * die hereinkommende Zeile 'shown' um slide_px versetzt, ihr Symbol auf
 * grow_pct (0-100) skaliert. dir: +1 von rechts, -1 von links. */
void flux_ui_draw_settings_reveal(flux_fb_t *fb, const char **labels,
                                  const char **values, int n, int shown,
                                  int slide_px, int grow_pct, int dir);
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

/* ---- Wecker/Timer-App ---------------------------------------------- */

#define ALARM_APP_ENTRY_MAX 15

typedef struct {
    int       is_timer;   /* 0 = Wecker, 1 = Timer */
    char      label[64];  /* Beschreibung */
    char      sub[32];    /* "07:30" (Wecker) oder "4:32 min" (Timer) */
    char      key[24];    /* Loeschschluessel: "YYYY-MM-DD HH:MM" oder UNIX_TS-String */
    long long timer_ts;   /* 0 bei Weckern */
} alarm_app_entry_t;

/* Zeichnet den Wecker/Timer-Screen.
 * entries[0..n_alarms-1] = Wecker, entries[n_alarms..n_alarms+n_timers-1] = Timer. */
void flux_ui_draw_alarm_app(flux_fb_t *fb,
                             const alarm_app_entry_t *entries,
                             int n_alarms, int n_timers);

/* Gibt globalen Index des getippten Loeschen-Buttons (0-based) oder -1. */
int flux_ui_alarm_app_delete_hit(const flux_fb_t *fb,
                                  int n_alarms, int n_timers, int x, int y);

/* Gibt Preset-Sekunden (300/600/1800/3600) bei Treffer, sonst 0. */
int flux_ui_alarm_app_preset_hit(const flux_fb_t *fb, int n_alarms, int x, int y);

/* Zeigt einen prominenten Alarm/Timer-Alert (voller Bildschirm, roter Akzent).
 * msg ist der Ausloesetext (z.B. "Wecker: Aufstehen (07:00)"). */
void flux_ui_draw_alarm_alert(flux_fb_t *fb, const char *msg);

/* Gibt 1 wenn der Bildschirm per Tap geschlossen werden soll. */
int flux_ui_notify_hit(const flux_fb_t *fb, int x, int y);

/* Zeichnet ein Fehler-Banner (rot) ueber den aktuellen Bildschirm. top_y =
 * obere Kante (fuer die Slide-up-Animation von unten). Hoehe via
 * flux_ui_error_toast_height(). Der Aufrufer komponiert es ueber den
 * gesicherten Backbuffer (siehe animate_error_toast in main.c). */
void flux_ui_draw_error_toast(flux_fb_t *fb, const char *msg, int top_y);
int  flux_ui_error_toast_height(const flux_fb_t *fb);

/* ---- Kalender -------------------------------------------------------
 * Monatsgitter. today_day: heutiger Tag (1-31, 0=unbekannt).
 * selected_day: markierter Tag (0=keiner). event_strs: Ereignis-Strings
 * fuer den ausgewaehlten Tag (aus /etc/flux/calendar.txt). */

void flux_ui_draw_calendar(flux_fb_t *fb, int year, int month,
                            int today_day, int selected_day,
                            const char **event_strs, int n_events);

/* Gibt 1 bei Treffer. Setzt *day (Roh-Zellenindex) bei Tageszellen,
 * *prev_month / *next_month bei den Chevron-Knoepfen, *today_btn beim
 * "Heute"-Knopf, *add_btn beim Plus-Knopf. */
int flux_ui_calendar_hit(const flux_fb_t *fb, int x, int y,
                          int *day, int *prev_month, int *next_month,
                          int *today_btn, int *add_btn);

/* ---- Kontakte -------------------------------------------------------
 * names/details parallel (z.B. "+49 151 ...  ich@mail.de").
 * selected_idx: -1 = keiner. Nutzt dieselbe Listen-Infrastruktur wie
 * Einstellungen/Dateien. */

void flux_ui_draw_contacts(flux_fb_t *fb, const char **names,
                            const char **details, int n, int selected_idx);

/* ---- Fotogalerie (iOS-"Mediathek"-Stil) ----------------------------- *
 * Randloses 3-Spalten-Raster mit echten, center-gecroppten Thumbnails,
 * fixem Header (grosser Titel + Filter/Auswaehlen) und schwebender
 * Segment-Leiste "Jahre|Monate|Alle" + Such-Knopf unten. Das Raster
 * scrollt vertikal (gallery_scroll), Header/Bottom-Leiste sind fix.
 *
 * Kantenlaenge einer Thumbnail-Kachel in Pixeln. main.c dekodiert die
 * .ppm einmalig auf genau diese Groesse (center-crop) und cached sie. */
#define FLUX_GALLERY_TILE 159   /* (480 - 2*1px Fugen) / 3 = 159 */

/* Filter-Segmente. Nur "Alle" zeigt real alle Fotos; "Monate"/"Jahre"
 * gruppieren nach echten Aufnahmedaten via Datums-Trennueberschriften. */
typedef enum {
    FLUX_GAL_JAHRE = 0,
    FLUX_GAL_MONATE = 1,
    FLUX_GAL_ALLE   = 2,
} flux_gallery_filter_t;

/* Liefert den fertigen, TILE*TILE grossen RGB32-Kachelpuffer fuer das
 * Foto `name` oder NULL, falls (noch) nicht dekodierbar (z.B. .jpg) ->
 * dann zeichnet die UI eine ehrliche Platzhalter-Kachel. Der Aufrufer
 * (main.c) besitzt und cached den Puffer; die UI liest ihn nur. */
typedef const uint32_t *(*flux_gallery_thumb_fn)(const char *name, void *user);

/* Zeichnet das Foto-Raster. names/dates parallel (dates: "TT.MM.JJJJ"
 * oder ""). selected_idx: -1 = keiner (Auswahlmodus). scroll: Anzahl
 * gescrollter Rasterzeilen (>=0). filter: siehe oben. select_mode: 1 =
 * Auswahl-Stub aktiv. thumb_fn/user: Thumbnail-Provider. */
void flux_ui_draw_gallery(flux_fb_t *fb, const char **names, const char **dates,
                           int n, int selected_idx, int scroll,
                           int filter, int select_mode,
                           flux_gallery_thumb_fn thumb_fn, void *user);

/* Anzahl scrollbarer Rasterzeilen (>= 0) fuer den gegebenen Zustand --
 * zum Clampen des Scroll-Offsets in main.c. */
int flux_ui_gallery_max_scroll(const flux_fb_t *fb, const char **dates,
                               int n, int filter);

/* Ergebnis eines Galerie-Taps. Genau ein Feld wird gesetzt. */
typedef struct {
    int tile;     /* >=0: Index der getroffenen Kachel, sonst -1 */
    int filter;   /* 1: Filter/Menue-Knopf (Header rechts)        */
    int select;   /* 1: "Auswaehlen"-Pille (Header rechts)        */
    int segment;  /* 0/1/2: Segment Jahre/Monate/Alle, sonst -1   */
    int search;   /* 1: Such-Knopf                                */
    int camera;   /* 1: Kamera-Knopf (Header links)               */
    /* --- nur im Such-Modus gesetzt --- */
    int back;       /* 1: Zurueck-Pfeil (Such-Modus verlassen)    */
    int ki;         /* 1: "KI"-Knopf (semantische Suche starten)  */
    char ch;        /* Tastatur-Zeichen (sonst 0)                 */
    int backspace;  /* 1: Ruecktaste                              */
    int enter;      /* 1: Eingabe-/OK-Taste                       */
} flux_gallery_hit_t;

/* Such-Modus der Galerie (Lupe): Suchleiste + Tastatur statt Header/Leiste.
 * active=1 schaltet ihn ein; query ist der aktuelle Suchtext (Anzeige). */
void flux_ui_gallery_set_search(int active, const char *query);

/* Hit-Test, exakt spiegelbildlich zu flux_ui_draw_gallery (gleiche
 * Geometrie-Helfer). Gibt 1 zurueck, wenn irgendetwas getroffen wurde;
 * *out beschreibt was. scroll/filter muessen mit dem Zeichenzustand
 * uebereinstimmen. */
int flux_ui_gallery_hit(const flux_fb_t *fb, int x, int y, const char **dates,
                        int n, int scroll, int filter, flux_gallery_hit_t *out);

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

/* Zeichnet den animierten Aufnahme-Indikator (eigenstaendiges Vollbild:
 * Mikrofon + pulsierende Ringe + laufende Wellenform).
 * elapsed_s: Aufnahmedauer in Sekunden (Timer). frame: Animationszaehler
 * (mit jedem Redraw erhoehen). Ruft flux_fb_present() auf. */
void flux_ui_draw_voice_overlay(flux_fb_t *fb, int elapsed_s, int frame);

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

/* ---- Animierte Aktions-Symbole ------------------------------------- *
 * Vollbild-Overlay mit einem grossen animierten Symbol statt Textmeldung
 * (Mail senden, Anruf, WLAN-Suche, Erfolg/Fehler). frame zaehlt hoch und
 * treibt die Animation; caption ist eine optionale kleine Beschriftung
 * (z.B. der Empfaenger) -- NULL fuer keine. */
typedef enum {
    FLUX_ANIM_MAIL,
    FLUX_ANIM_SMS,
    FLUX_ANIM_CALL,
    FLUX_ANIM_SCAN,
    FLUX_ANIM_OK,
    FLUX_ANIM_FAIL,
} flux_anim_kind_t;

void flux_ui_draw_action_anim(flux_fb_t *fb, flux_anim_kind_t kind,
                              int frame, const char *caption);

/* ---- WLAN ----------------------------------------------------------- *
 * names/metas parallel (SSID + "Signal 80% - gesichert"). current: aktuell
 * verbundene SSID (leer = nicht verbunden). scanning: 1 waehrend des Scans.
 * unavailable: 1 wenn kein WLAN-Interface/wpa_cli vorhanden. */
void flux_ui_draw_wifi(flux_fb_t *fb, const char *current, const char **names,
                       const char **metas, int n, int scanning, int unavailable);

/* ---- Journal ------------------------------------------------------- */

/* Zeigt Liste aller Journal-Eintraege (Dateinamen ohne .txt, neueste zuerst).
 * names/n: Array von Datumstrings. selected: markierter Eintrag (-1 = keiner). */
void flux_ui_draw_journal(flux_fb_t *fb, const char **names, int n,
                          int scroll, int selected);

/* Hit-Test: gibt Index des angetippten Eintrags (-1 = keiner) oder
 * setzt *back auf 1 wenn Zurueck-Leiste getippt. */
int flux_ui_journal_hit(const flux_fb_t *fb, int x, int y,
                        int n, int *back);

/* Klemmt einen Scroll-Wert auf den gueltigen Bereich (dieselbe Geometrie
 * wie flux_ui_draw_journal). main.c MUSS journal_scroll hierdurch
 * schicken, bevor idx + journal_scroll zum Indizieren verwendet wird --
 * sonst kann ein zu weit gescrollter, ungeklemmter Wert eine Out-of-
 * Bounds-Indizierung in journal_names_buf[] erzeugen. */
int flux_ui_journal_clamp_scroll(const flux_fb_t *fb, int n, int scroll);

/* ---- Stimm-Entsperrung (zweiter Faktor nach PIN) ------------------- */

/* Enrollment-Screen: zeigt Aufnahme-Anleitung und Status.
 * phase: 0=Anleitung, 1=Aufnahme laeuft, 2=Erfolg, 3=Fehler.
 * msg: Statustext aus voice_unlock_enroll(). */
void flux_ui_draw_voice_enroll(flux_fb_t *fb, int phase, const char *msg);

/* Verification-Screen: zeigt "Sprechen Sie bitte" und Ergebnis.
 * phase: 0=Warten, 1=Aufnahme, 2=OK, 3=Mismatch/Fehler.
 * msg: Statustext aus voice_unlock_verify(). */
void flux_ui_draw_voice_verify(flux_fb_t *fb, int phase, const char *msg);

/* Hit-Test fuer beide Voice-Screens.
 * Gibt 1 wenn "Aufnehmen"/"Wiederholen", 2 wenn "Ueberspringen"/"PIN", 0 sonst. */
int flux_ui_voice_hit(const flux_fb_t *fb, int x, int y);

/* ---- Farbthema ----------------------------------------------------- */

/* Setzt die Akzentfarbe fuer alle Bildschirme.
 * Vordefinierte Werte: 0x4FD1C5 (Teal), 0x3B82F6 (Blau),
 * 0xA855F7 (Lila), 0xF97316 (Orange), 0x22C55E (Gruen). */
void flux_ui_set_accent(uint32_t rgb);

/* ---- Alarm-Screen -------------------------------------------------- */

/* Vollbild-Alarm. label: z.B. "07:00 Aufstehen". */
void flux_ui_draw_alarm(flux_fb_t *fb, const char *label);

/* ---- Anruf-Screen (Vollbild, wie der Alarm) ------------------------ */

/* Vollbild-Anruf im selben Stil wie der Wecker: grosses Telefon-Symbol,
 * Name + Nummer, unten zwei runde Knoepfe -- gruen "Annehmen" (Hoerer),
 * rot "Auflegen" (durchgestrichener Hoerer).
 * connected=0: klingelt/ruft an (beide Knoepfe). connected=1: Gespraech
 * laeuft (Timer + Aufnahme-Indikator, nur der rote Auflegen-Knopf).
 * elapsed_s: Gespraechsdauer in Sekunden (nur bei connected genutzt). */
void flux_ui_draw_call(flux_fb_t *fb, const char *name, const char *number,
                       int connected, int elapsed_s);

/* Hit-Test fuer den Anruf-Screen:
 *   FLUX_CALL_ACCEPT (1) = gruener Annehmen-Knopf
 *   FLUX_CALL_HANGUP (2) = roter Auflegen-Knopf
 *   0 = daneben getippt */
typedef enum { FLUX_CALL_NONE = 0, FLUX_CALL_ACCEPT, FLUX_CALL_HANGUP } flux_call_hit_t;
flux_call_hit_t flux_ui_call_hit(const flux_fb_t *fb, int x, int y, int connected);

/* ---- Gewohnheiten/Habits ------------------------------------------- */

/* Zeigt eine scrollbare Liste der habits.txt-Zeilen.
 * lines/n: bereits geladene Zeilen. scroll: erste sichtbare Zeile. */
void flux_ui_draw_habits(flux_fb_t *fb, const char **lines, int n, int scroll);

/* Hit-Test: setzt *back=1 bei Zurueck-Leiste, gibt 1 bei Treffer. */
int flux_ui_habits_hit(const flux_fb_t *fb, int x, int y, int *back);

#endif
