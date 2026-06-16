# Flux -- Architektur

## Designprinzip

Jede Schicht macht genau eine Sache und macht sie schnell. Keine
Abstraktion, die nicht durch eine konkrete, schon vorhandene Anforderung
gerechtfertigt ist (siehe Roadmap fuer das, was bewusst NICHT jetzt
gebaut wurde).

## Komponenten

### `flux-shell` (`shell/`)
Einziger UI-Prozess. Zustandsmaschine mit zwei Bildschirmen:

- `FLUX_SCREEN_LOCK`: Uhrzeit, Datum, Hinweis zum Entsperren.
- `FLUX_SCREEN_ASSISTANT`: Eingabezeile + letzte Antwort. Es gibt keinen
  dritten Zustand "App-Liste" -- das ist beabsichtigt, nicht
  unvollstaendig.

Zeichnet direkt auf `/dev/fb0` (`shell/src/fb.c`). Zwei Puffer:
`back` (woandersbeschrieben) und `prev` (zuletzt tatsaechlich auf den
Bildschirm kopiert). `flux_fb_present()` vergleicht zeilenweise und
kopiert nur Aenderungen -- das ist der gesamte Trick hinter "flüssig
ohne GPU".

Texte werden mit `stb_easy_font.h` (Public Domain, Sean Barrett) als
Vektor-Quads erzeugt und dann in den Backbuffer gerastert
(`flux_fb_text`). Keine eigene Bitmap-Font-Tabelle, kein TTF-Renderer --
fuer Systemtexte reicht das, fuer spaeteres App-Rendering nicht.

Eingaben kommen ueber `/dev/input/eventN` (`shell/src/input.c`),
generisch ueber `EVIOCGBIT` erkannt, nicht hart auf eine PS/2-Tastatur
verdrahtet. Touch-Events (`EV_ABS`) tauchen am selben Layer auf einem
echten Touchscreen auf -- die Eingabeabstraktion ist dafuer vorbereitet,
aber noch nicht implementiert (siehe Roadmap Punkt 4).

### `fluxaid` (`fluxai/`)
System-Daemon, kein App-Prozess, startet vor der Shell. Hoert auf einem
Unix-Domain-Socket (`/run/flux/fluxai.sock`), ein Request pro
Verbindung, zeilenbasiertes Mini-Protokoll (`common/flux_protocol.h`) --
bewusst kein JSON-RPC, kein HTTP-Server im eigenen OS-Daemon.

Anfragereihenfolge (`fluxai/src/main.c`):
1. `actions.c`: lokale Intents (Uhrzeit, Datum, Akku, Uptime). Trifft
   ein Intent zu, verlaesst die Frage das Geraet nie.
2. `provider.c`: nur falls (1) nichts gefunden hat. Ruft die Anthropic
   Messages API mit dem in `FLUX_AI_API_KEY` hinterlegten Schluessel des
   Nutzers auf. Ohne Schluessel: ehrliche Fehlermeldung statt Absturz
   oder erfundener Antwort.

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
