/* icons.h -- vektorbasierte UI-Icons fuer Flux.
 *
 * Hintergrund: Die Icons waren bisher von Hand in C gezeichnet (Kreise,
 * Striche). Stattdessen rendern wir jetzt echte SVG-Icons aus dem
 * Open-Source-Set "Lucide" (MIT) mit dem Single-Header-Rasterizer
 * NanoSVG (zlib). Das passt zur framebufferbasierten Architektur:
 * jedes Icon wird einmal pro (Symbol, Groesse) in einen RGBA-Puffer
 * gerastert, gecacht und dann als eingefaerbte Maske geblittet --
 * keine GPU, kein Compositor noetig.
 *
 * Neue Icons hinzufuegen: SVG-Pfad-Inhalt aus dem Lucide-Repo in die
 * ICON_BODY-Tabelle in icons.c eintragen und hier ein Enum ergaenzen.
 */
#ifndef FLUX_ICONS_H
#define FLUX_ICONS_H

#include "fb.h"

typedef enum {
    FLUX_ICON_SETTINGS = 0,  /* Zahnrad        (Lucide: settings)        */
    FLUX_ICON_FOLDER,        /* Ordner         (Lucide: folder)          */
    FLUX_ICON_CALENDAR,      /* Kalender       (Lucide: calendar)        */
    FLUX_ICON_USER,          /* Person         (Lucide: user)            */
    FLUX_ICON_MIC,           /* Mikrofon       (Lucide: mic)             */
    FLUX_ICON_COPY,          /* Kopieren       (Lucide: copy)            */
    FLUX_ICON_CLIPBOARD,     /* Einfuegen      (Lucide: clipboard)       */
    FLUX_ICON_CHECK,         /* Haken          (Lucide: check)           */
    FLUX_ICON_SEND,          /* Senden         (Lucide: send-horizontal) */
    FLUX_ICON_SEARCH,        /* Lupe           (Lucide: search)          */
    FLUX_ICON_WIFI,          /* WLAN           (Lucide: wifi)            */
    FLUX_ICON_BATTERY,       /* Akku           (Lucide: battery)         */
    FLUX_ICON_LOCK,          /* Schloss        (Lucide: lock)            */
    FLUX_ICON_SPARKLES,      /* KI/Funken      (Lucide: sparkles)        */
    FLUX_ICON_KEY,           /* Schluessel     (Lucide: key-round)       */
    FLUX_ICON_CPU,           /* Chip/Modell    (Lucide: cpu)             */
    FLUX_ICON_MAIL,          /* E-Mail         (Lucide: mail)            */
    FLUX_ICON_PALETTE,       /* Farbthema      (Lucide: palette)         */
    FLUX_ICON_CLOCK,         /* Uhr/Auto-Sperre(Lucide: clock)           */
    FLUX_ICON_VOLUME,        /* Lautsprecher   (Lucide: volume-2)        */
    FLUX_ICON_BELL_RING,     /* Wecker klingelt(Lucide: bell-ring)       */
    FLUX_ICON_PHONE,         /* Hoerer (annehmen)(Lucide: phone)         */
    FLUX_ICON_PHONE_OFF,     /* Auflegen       (Lucide: phone-off)       */
    FLUX_ICON_PHONE_CALL,    /* Anruf laeuft   (Lucide: phone-call)      */
    FLUX_ICON_CHEVRON_LEFT,  /* Pfeil links    (Lucide: chevron-left)    */
    FLUX_ICON_CHEVRON_RIGHT, /* Pfeil rechts   (Lucide: chevron-right)   */
    FLUX_ICON_PLUS,          /* Plus/Hinzufuegen(Lucide: plus)           */
    FLUX_ICON_SLIDERS,       /* Filter/Regler  (Lucide: sliders-horizontal)*/
    FLUX_ICON_IMAGE,         /* Bild-Platzhalter(Lucide: image)          */
    FLUX_ICON_CAMERA,        /* Kamera         (Lucide: camera)          */
    FLUX_ICON_X,             /* Schliessen/Abbrechen (Lucide: x)         */
    FLUX_ICON_PENCIL,        /* Bearbeiten     (Lucide: pencil)          */
    FLUX_ICON_MESSAGE_CIRCLE,/* Nachricht/SMS  (Lucide: message-circle)  */
    FLUX_ICON_GLOBE,         /* Web/Suche      (Lucide: globe)           */
    FLUX_ICON_FILE,          /* Datei          (Lucide: file-text)       */
    FLUX_ICON_TRASH,         /* Loeschen       (Lucide: trash-2)         */
    FLUX_ICON_COUNT
} flux_icon_t;

/* Zeichnet das Icon zentriert auf (cx,cy), Kantenlaenge `size` Pixel,
 * eingefaerbt mit `rgb`. Rasterisiert beim ersten Aufruf pro
 * (Icon,Groesse) und cached das Ergebnis (LRU). Bei Fehlern (z.B. kein
 * Speicher) zeichnet es nichts -- der Aufrufer muss nichts pruefen. */
void flux_icon_draw(flux_fb_t *fb, flux_icon_t id, int cx, int cy, int size, uint32_t rgb);

/* Gibt den Raster-Cache und den Rasterizer frei (optional, beim Beenden). */
void flux_icon_cleanup(void);

#endif
