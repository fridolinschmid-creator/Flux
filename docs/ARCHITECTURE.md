# Flux -- Architektur

## Designprinzip

Jede Schicht macht genau eine Sache und macht sie schnell. Keine
Abstraktion, die nicht durch eine konkrete, schon vorhandene Anforderung
gerechtfertigt ist (siehe Roadmap fuer das, was bewusst NICHT jetzt
gebaut wurde).

## Komponenten

### `flux-shell` (`shell/`)
Einziger UI-Prozess. Zustandsmaschine (`flux_screen_t`, `shell/src/ui.h`)
mit sieben Bildschirmen, aber weiterhin keinem App-Grid -- das ist
beabsichtigt, nicht unvollstaendig:

- `FLUX_SCREEN_LOCK`: Uhrzeit, Datum, Hinweis zum Entsperren.
- `FLUX_SCREEN_PIN`: nur erreicht, wenn in den Einstellungen ein PIN-Hash
  gesetzt ist (`flux_config_get("pin_hash", ...)`, `shell/src/main.c`).
  Ohne PIN entsperrt der Wisch direkt in den Assistenten -- ehrlicher
  "noch nicht eingerichtet"-Default statt erzwungener Huerde beim
  Erststart.
- `FLUX_SCREEN_ASSISTANT`: der eigentliche Homescreen. Eingabezeile +
  letzte Antwort, Mikrofon-Knopf (siehe README, aktuell Platzhalter),
  Schnellzugriff-Leiste fuer Einstellungen/Dateien.
- `FLUX_SCREEN_CONFIRM`: Bestaetigungs-Dialog, wenn `fluxaid` einen
  `ACTION:`-Vorschlag (Mail/SMS/Anruf) liefert (`shell/src/action.c`).
  Drei Knoepfe: Senden, Abbrechen, Bearbeiten. Die KI fuehrt nie direkt
  etwas aus -- erst ein Tap auf "Senden" loest die `X:`-Anfrage aus.
- `FLUX_SCREEN_EDIT_BODY`: Text vor dem Senden bearbeiten (von "Bearbeiten"
  aus erreichbar), wiederverwendet auch fuer das Bearbeiten einzelner
  Einstellungswerte -- gleiche Bildschirmtastatur, gleicher Code.
- `FLUX_SCREEN_SETTINGS` / `FLUX_SCREEN_FILES`: ueber die Schnellzugriff-
  Leiste oder Tippen/Sprechen von "Einstellungen"/"Dateien" erreichbar,
  client-seitig in `main.c` ohne Roundtrip zu `fluxaid` umgeschaltet
  (gleiche "lokale Intents zuerst"-Haltung wie bei `actions.c`).

Jede Liste/Tastatur hat genau eine Geometrie-Funktion, die sowohl vom
Zeichnen als auch vom Hit-Testing genutzt wird (`build_kbd_geom`,
`build_pin_geom`, `build_confirm_buttons`, `build_list_rows` in
`shell/src/ui.c`) -- sonst driften Darstellung und Tap-Erkennung
irgendwann auseinander.

Zeichnet direkt auf `/dev/fb0` (`shell/src/fb.c`). Zwei Puffer:
`back` (woandersbeschrieben) und `prev` (zuletzt tatsaechlich auf den
Bildschirm kopiert). `flux_fb_present()` vergleicht zeilenweise und
kopiert nur Aenderungen -- das ist der gesamte Trick hinter "flüssig
ohne GPU".

Texte werden mit `stb_easy_font.h` (Public Domain, Sean Barrett) als
Vektor-Quads erzeugt und dann in den Backbuffer gerastert
(`flux_fb_text`). Keine eigene Bitmap-Font-Tabelle, kein TTF-Renderer --
fuer Systemtexte reicht das, fuer spaeteres App-Rendering nicht.

Icons sind echte **Vektor-Grafiken**: Lucide-SVGs (MIT) werden mit dem
Single-Header-Rasterizer NanoSVG (zlib) einmal pro Groesse in einen
RGBA-Puffer gerastert, gecacht und als eingefaerbte Maske ueber
`flux_fb_blit_mask` gezeichnet (`shell/src/icons.c`). Dieselbe GPU-lose
Software-Raster-Logik wie beim Text -- Details in `docs/ICONS.md`.

Eingaben kommen ueber `/dev/input/eventN` (`shell/src/input.c`),
generisch ueber `EVIOCGBIT` erkannt, nicht hart auf eine PS/2-Tastatur
verdrahtet. Tastatur- und Touch/Pointer-Geraet werden parallel offen
gehalten (`flux_input_t` mit `kbd_fd`/`touch_fd`), `select()` wartet auf
beide gleichzeitig. Touch-Rohkoordinaten (`EV_ABS`, `ABS_X`/`ABS_Y`)
werden anhand der vom Geraet gemeldeten Wertebereiche (`EVIOCGABS`) auf
Bildschirmpixel skaliert; `BTN_TOUCH`/`BTN_LEFT` markiert Tap-Beginn/
-Ende. Ein kurzer Tap erzeugt `FLUX_EV_TAP` (Koordinaten), ein schneller
Wisch nach oben `FLUX_EV_SWIPE_UP` (zum Entsperren). In QEMU liefert
`virtio-tablet-pci` genau dieselben Events wie ein echter Touchscreen --
dieselbe Abstraktion deckt beides ab. Die Bildschirmtastatur
(`shell/src/ui.c`, `flux_ui_kbd_hit`/`draw_keyboard`) macht Tastatur-
Hardware optional, nicht nur theoretisch vorbereitet.

### `fluxaid` (`fluxai/`)
System-Daemon, kein App-Prozess, startet vor der Shell. Hoert auf einem
Unix-Domain-Socket (`/run/flux/fluxai.sock`), ein Request pro
Verbindung, zeilenbasiertes Mini-Protokoll (`common/flux_protocol.h`) --
bewusst kein JSON-RPC, kein HTTP-Server im eigenen OS-Daemon.

`fluxaid/src/main.c` unterscheidet zwei Request-Typen: `Q:<frage>`
(bestehend) und `X:<typ>\nTO:...\nSUBJECT:...\nBODY:\n<text>` (neu --
eine bereits vom Nutzer bestaetigte Aktion, siehe unten). Nur `Q:`-
Requests werden beim ersten Zeilenumbruch trunciert; `X:`-Requests
bleiben mehrzeilig erhalten, sonst wuerde die TO:/SUBJECT:/BODY:-
Struktur zerstoert.

Anfragereihenfolge fuer `Q:`:
1. `actions.c`: lokale Intents (Uhrzeit, Datum, Akku, Uptime). Trifft
   ein Intent zu, verlaesst die Frage das Geraet nie.
2. `provider.c`: nur falls (1) nichts gefunden hat. Ruft die Anthropic
   Messages API mit dem in `FLUX_AI_API_KEY` hinterlegten Schluessel des
   Nutzers auf (oder `api_key` aus `/etc/flux/flux.conf`, falls in den
   Einstellungen gesetzt). Ohne Schluessel: ehrliche Fehlermeldung statt
   Absturz oder erfundener Antwort. Der System-Prompt weist das Modell
   an, bei einem klaren Mail/SMS/Anruf-Auftrag mit einem strukturierten
   `ACTION:`-Block statt Freitext zu antworten.

`X:`-Requests gehen direkt an `exec.c`, das anhand des Typs an `mail.c`
(echter SMTP-Versand per libcurl) oder `telephony.c` (SMS/Anruf -- siehe
README, ehrlicher Modem-Stub, austauschbares Backend) weiterleitet.

### Kernel/Rootfs (`build/`)
Linux-Kernel + Toolchain + Basissystem kommen von Buildroot
(`flux_aarch64_virt_defconfig`, abgeleitet von Buildroots eigenem
`qemu_aarch64_virt_defconfig`). `build/overlay/` ersetzt nur `/etc/inittab`
und liefert die beiden eigenen Binaries aus -- alles andere (Mounten von
`/proc`/`/sys`, Netzwerk per DHCP, ext4-Rootfs) ist Buildroot-Standard und
wurde nicht neu erfunden.

## Warum dieser Zuschnitt?

Die drei Komponenten sind unabhaengig testbar:
- `fluxaid` laeuft und antwortet auch ohne Framebuffer (Host-Test per
  Unix-Socket, kein QEMU/Kernel noetig).
- `flux-shell` kompiliert nativ zum Pruefen der Logik; echtes Zeichnen
  braucht `/dev/fb0`, also QEMU oder echte Hardware.
- Kernel/Rootfs sind komplett von Buildroot verwaltet -- kein
  handgeschriebener Boot-Code wie im x86-Vorlaeufer-Projekt, weil ein
  Telefon-faehiger Kernel keine sinnvolle Lernuebung "von Grund auf"
  mehr ist (anders als ein simpler Multiboot-PC-Kernel).
