# Icons & Animationen in Flux

Bis hierher waren alle Symbole von Hand in C gezeichnet (Kreise, Striche
mit `flux_fb_*`). Das war robust, aber muehsam und sah je nach Symbol
unterschiedlich „gewachsen" aus. Jetzt rendert Flux echte **Vektor-Icons
aus dem Open-Source-Set [Lucide](https://lucide.dev) (MIT)** mit dem
Single-Header-Rasterizer **[NanoSVG](https://github.com/memononen/nanosvg)
(zlib)** -- beides passt zur framebufferbasierten, GPU-losen Architektur
(genau wie das schon vorhandene `stb_easy_font.h`).

## Wie es funktioniert

```
Lucide-SVG-Pfad (in icons.c eingebettet)
        │  NanoSVG: parsen + rastern (einmal pro Icon+Groesse)
        ▼
RGBA-Puffer im Cache  ──►  flux_fb_blit_mask(...)  ──►  Backbuffer
        (Alpha = Deckung)        (faerbt mit Wunschfarbe ein)
```

- **`shell/src/icons.c/.h`** -- die Icon-Tabelle + der Raster-Cache.
  Jedes Icon wird beim ersten Gebrauch in der angeforderten Pixelgroesse
  gerastert und gecacht (LRU, 128 Eintraege). Gezeichnet wird es als
  *einfarbige Maske*: NanoSVG liefert nur die Deckung (Alpha), die Farbe
  kommt beim Blitten dazu -- dasselbe Icon kann so in jeder Akzentfarbe
  erscheinen, ohne neu zu rastern.
- **`shell/src/nanosvg.h` / `nanosvgrast.h`** -- die vendorten
  Single-Header-Libs (unveraendert, zlib-Lizenz).
- **`flux_fb_blit_rgba` / `flux_fb_blit_mask`** (`shell/src/fb.c`) -- die
  zwei neuen Framebuffer-Primitive, die RGBA-Puffer mit Per-Pixel-Alpha
  einblenden. `_rgba` fuer voll eingefaerbte Bilder (spaeter z.B. Lottie),
  `_mask` fuer monochrome Strich-Icons wie Lucide.

## Ein Icon zeichnen

```c
#include "icons.h"

/* zentriert auf (cx,cy), 24 px Kantenlaenge, in der Akzentfarbe */
flux_icon_draw(fb, FLUX_ICON_SETTINGS, cx, cy, 24, COL_ACCENT);
```

Schon angebunden: die Schnellzugriff-Leiste (Einstellungen/Dateien/
Kalender/Kontakte) und die Eingabeleisten-Knoepfe (Mikrofon/Haken/
Kopieren/Einfuegen) in `shell/src/ui.c`.

## Ein neues Icon hinzufuegen

1. SVG bei [lucide.dev](https://lucide.dev) suchen, den **Inhalt** (die
   `<path>`/`<circle>`/`<rect>`-Elemente, ohne den `<svg>`-Rahmen) kopieren.
2. In `icons.c` einen Eintrag in `ICON_BODY[]` ergaenzen und in `icons.h`
   ein passendes `FLUX_ICON_*`-Enum davor. `currentColor` nicht uebernehmen
   -- die Farbe setzt der gemeinsame `SVG_HEAD`-Rahmen (weiss) und spaeter
   der Blit.
3. Fertig -- `flux_icon_draw(fb, FLUX_ICON_NEU, ...)` aufrufen.

## Animationen

Fuer Bewegung braucht es **keine** schwere Bibliothek -- das meiste
„Premium-Gefuehl" ist *Easing* auf Position/Groesse/Deckkraft. Dafuer gibt
es **`shell/src/anim.h`** (header-only, abhaengigkeitsfrei):

```c
#include "anim.h"

uint64_t start = flux_now_ms();
/* ... pro Frame: ... */
float t    = flux_anim_clamp01((flux_now_ms() - start) / 250.0f);
int   size = (int)(24 * flux_ease_out_back(t));   /* Icon „ploppt" auf 24px */
flux_icon_draw(fb, FLUX_ICON_MIC, cx, cy, size, COL_ACCENT);
```

Enthalten: `flux_ease_out_cubic` (Allrounder), `_in_cubic`, `_in_out_cubic`,
`_out_back` (leichtes Ueberschwingen/„Pop"), `flux_pulse` (Pulsieren fuer
Aufnahme-Indikator/Akzentpunkt) sowie `flux_now_ms`/`flux_lerp`.

## Spaeter: echte animierte Icons (Lottie)

Wer aufwaendige Motion-Graphics will (rotierende Loader, morphendes Mikro),
kann **rlottie** (Samsung, Software-Renderer) anbinden: es gibt ARGB-Puffer
aus, die direkt ueber `flux_fb_blit_rgba` in den Framebuffer wandern -- kein
GPU noetig. Bewusst noch nicht eingebaut (schwerere C++-Abhaengigkeit); die
Andockstelle (`flux_fb_blit_rgba`) ist aber schon da.
