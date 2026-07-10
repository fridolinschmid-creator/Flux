# fluxweb -- echter JS-faehiger Browser fuer FluxOS (WPE WebKit)

## Warum ein eigener Prozess

`flux-shell` zeichnet in einer eigenen `select()`-Schleife direkt auf
`/dev/fb0` (siehe `shell/src/main.c`/`fb.c`). WPE WebKit braucht dagegen
einen laufenden **GLib-Main-Loop** (fuer Netzwerk, JavaScript, Rendering-
Callbacks) und ein eigenes EGL/GBM-Kontext -- das laesst sich nicht einfach
in die bestehende Event-Schleife der Shell einklinken, ohne deren
Zeichen-Timing (das "kein Full-Redraw pro Frame"-Prinzip aus `fb.c`) zu
gefaehrden.

Deshalb: ein zweiter Hintergrundprozess `fluxweb`, analog zu `fluxaid`
(eigener Unix-Socket, eigenes Mini-Protokoll -- siehe
`common/flux_webview_protocol.h`). `fluxweb` rendert eine Seite off-screen
(WPE "exportable" Backend, kein eigenes Fenster/Compositor noetig) und legt
jeden fertigen Frame als rohe RGBA-Datei in ein Tmpfs. `flux-shell` liest
diese Datei und blittet sie mit dem bereits vorhandenen
`flux_fb_blit_rgba()` (`fb.h`) in den eigenen Backbuffer -- fuer die Shell
sieht ein WPE-Frame aus wie jedes andere Bild.

## Status -- was hier WIRKLICH verifiziert ist und was nicht

**Ehrlich, damit hier niemand von einem "fertigen Browser" ausgeht:**

- Buildroot-Paketauswahl (`build/raspberrypi5/flux-pi5.fragment`):
  `BR2_PACKAGE_WPEWEBKIT` + Mesa3D/V3D-Treiber-Optionen sind gegen den
  echten Buildroot-2024.02.x-Quellbaum geprueft (Config.in-Dateien
  abgerufen, Optionsnamen stimmen). **Nicht geprueft**: ob die komplette
  Abhaengigkeitskette (Dutzende Pakete: JavaScriptCore, Cairo, ICU,
  Wayland, libepoxy, ...) tatsaechlich fehlerfrei durchbaut -- das geht
  nur mit einem echten Buildroot-Checkout und dauert vermutlich sehr
  lange (Stunden) beim ersten Mal.
- `src/main.c` (Socket-Server, Protokoll-Parsing, GLib-Loop-Grundgeruest):
  reines C gegen GLib/POSIX, **auf dem Host kompilierbar und
  eigenstaendig testbar** (mit `make` hier in der Sandbox geprueft --
  siehe unten).
- `src/webview.c` (die eigentlichen WPE/libwpe-Aufrufe): **NICHT
  kompiliert/getestet** -- in dieser Sandbox ist `wpewebkit`/`libwpe`
  nicht installiert, es gibt keine Header zum Gegenpruefen. Die Funktions-
  namen (`wpe_view_backend_exportable_fdo_egl_create`,
  `webkit_web_view_new_with_backend`, ...) stammen aus meinem Wissen ueber
  die oeffentliche WPE-API, sind aber **nicht verifiziert**. Jede
  Funktion hat einen `/* VERIFY */`-Kommentar an der Stelle, wo beim
  echten Build auf dem Pi 5 am ehesten etwas nicht passt.

## Naechste Schritte auf dem Pi 5

1. `./build/build-pi5.sh` -- baut jetzt auch WPE WebKit mit. Einplanen:
   das ist ein SEHR viel groesserer Build als bisher (JavaScriptCore
   allein braucht typischerweise 30-60+ Minuten selbst auf potenter
   Hardware). Bei Fehlern in der Abhaengigkeitsaufloesung: im Buildroot-
   Checkout `make menuconfig` oeffnen, `WPE WebKit` suchen, verbleibende
   "needs X" Kommentare von Hand aufloesen.
2. `pkg-config --cflags --libs wpe-webkit-2.0 wpe-2.0` auf dem fertigen
   Sysroot pruefen -- das sagt, ob die Pakete tatsaechlich da sind und
   liefert die echten Header-Pfade zum Gegenpruefen von `webview.c`.
3. `webview.c` gegen die echten Header kompilieren, `/* VERIFY */`-Stellen
   korrigieren.
4. Danach: `flux-shell`-Seite anbinden (neuer IPC-Client analog zu
   `shell/src/ipc.c`, siehe TODO in `shell/src/browser_render.h`) --
   bewusst noch NICHT in diesem Schritt gemacht, um nicht auf einer noch
   unbestaetigten API weiterzubauen.

## Bauen (Host, nur der Socket-Server-Teil)

```
cd fluxweb && make          # baut nur main.c (kein WPE), zum Testen
                             # des Protokoll-/Prozessgeruests
```

`make CROSS_COMPILE=aarch64-linux-gnu- WPE=1` (auf dem Pi-5-Sysroot mit
echtem `pkg-config`) baut inklusive `webview.c` -- siehe Makefile.
