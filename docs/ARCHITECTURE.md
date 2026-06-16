# Flux -- Architektur

## Designprinzip

Jede Schicht macht genau eine Sache und macht sie schnell. Keine
Abstraktion, die nicht durch eine konkrete, schon vorhandene Anforderung
gerechtfertigt ist (siehe Roadmap fuer das, was bewusst NICHT jetzt
gebaut wurde).

## Komponenten

### `flux-shell` (`shell/`)
Einziger UI-Prozess. Zustandsmaschine mit drei Bildschirmen:

- `FLUX_SCREEN_LOCK`: Uhrzeit, Datum, Hinweis zum Entsperren.
- `FLUX_SCREEN_ASSISTANT`: Eingabezeile + letzte Antwort. Es gibt keinen
  eigenen Zustand "App-Liste" -- das ist beabsichtigt, nicht
  unvollstaendig.
- `FLUX_SCREEN_CALL`: simulierter Anruf-Bildschirm (siehe unten).

Bildformat ist Hochformat (1080x2400, wie ein echtes Handy), nicht das
Querformat eines Desktop-Fensters -- das zieht sich durch Tastatur- und
Antwort-Layout (`flux_ui_kbd_top`, `draw_wrapped`).

Mehrschritt-Dialoge (Kontakt anlegen, Mail schreiben) leben komplett im
Shell-Prozess als eigene `flux_dialog_t`-Zustandsmaschine
(`shell/src/main.c`) -- `fluxaid` bekommt erst beim letzten Schritt einen
einzigen fertigen Request (`C:`/`M:`) und bleibt damit ohne eigenes
Dialog-Gedaechtnis, ein Request pro Verbindung wie bisher.

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

Vier Request-Typen, alle einzeilig, Felder Tab-getrennt:

- `Q:<frage>` -- stellt eine Frage (Uhrzeit/Akku/... oder KI).
- `C:<name>\t<telefonnummer>` -- legt einen Kontakt an oder
  ueberschreibt ihn (gleicher Name, gross/klein-unabhaengig).
- `F:<name>` -- sucht einen Kontakt (Teilstring-Match auf den Namen).
- `M:<empfaenger>\t<betreff>\t<text>` -- verschickt eine E-Mail.

Antwort immer `A:<antwort>\nEND\n` oder `ERR:<meldung>\nEND\n`.

Anfragereihenfolge fuer `Q:` (`fluxai/src/actions.c` vor `provider.c`):
1. `actions.c`: lokale Intents (Uhrzeit, Datum, Akku, Uptime,
   Kontaktliste). Trifft ein Intent zu, verlaesst die Frage das Geraet
   nie.
2. `provider.c`: nur falls (1) nichts gefunden hat.
   a. Lokales Modell zuerst, falls `FLUX_AI_LOCAL_URL` gesetzt ist
      (Ollama oder LM Studio, beide ueber die OpenAI-kompatible
      `/v1/chat/completions`-API). Nicht erreichbar oder Antwort nicht
      lesbar -> stillschweigender Fallback auf die Cloud, kein
      Verhaltensunterschied wenn die Variable nicht gesetzt ist.
   b. Cloud-Fallback: Anthropic Messages API mit dem in
      `FLUX_AI_API_KEY` hinterlegten Schluessel des Nutzers. Ohne
      Schluessel: ehrliche Fehlermeldung statt Absturz oder erfundener
      Antwort.

Kontakte (`fluxai/src/contacts.c`) liegen Tab-getrennt in
`/var/lib/flux/contacts.tsv`, exklusiv von `fluxaid` verwaltet -- die
Shell sieht die Datei nie direkt, nur ueber `C:`/`F:`.

E-Mail (`fluxai/src/email.c`) verschickt per libcurl/SMTP mit
erzwungenem TLS (`CURLOPT_USE_SSL = CURLUSESSL_ALL`, nie Klartext-
Zugangsdaten). Konfiguration ueber `FLUX_SMTP_URL`, `FLUX_SMTP_FROM`
(beide Pflicht) sowie optional `FLUX_SMTP_USER`/`FLUX_SMTP_PASS`. Ohne
Konfiguration: ehrliche Fehlermeldung, kein simulierter Versand.

### Simulierter Anruf
QEMU hat kein Modem/SIM -- ein echter Anruf ist nicht moeglich. Statt
das vorzutaeuschen, zeigt `FLUX_SCREEN_CALL` (`shell/src/ui.c`,
`flux_ui_draw_call`) explizit "SIMULIERTER ANRUF" und den Hinweis
"Simulation -- kein Modem/SIM in QEMU vorhanden". Gleiches
Ehrlichkeitsprinzip wie die Akku-Fehlermeldung.

### Kernel/Rootfs (`build/`)
Linux-Kernel + Toolchain + Basissystem kommen von Buildroot. Die
Defconfig liegt versioniert im Repo unter
`build/configs/flux_aarch64_virt_defconfig` (abgeleitet von Buildroots
eigenem `qemu_aarch64_virt_defconfig`) und wird von `build/build.sh` vor
dem Build in den Buildroot-Checkout kopiert -- ohne das waere der Build
aus einem frischen Buildroot-Checkout nicht reproduzierbar, weil
Buildroot benannte Defconfigs nur im eigenen `configs/`-Verzeichnis
sucht. Sie enthaelt u.a. `BR2_PACKAGE_OPENSSL` +
`BR2_PACKAGE_LIBCURL_OPENSSL`, damit das cross-kompilierte libcurl ein
echtes TLS-Backend hat (sonst kein https fuer den Cloud-Fallback, kein
smtps/STARTTLS fuer den Mailversand).

`build/overlay/` ersetzt nur `/etc/inittab` und liefert die beiden
eigenen Binaries aus -- alles andere (Mounten von `/proc`/`/sys`,
Netzwerk per DHCP, ext4-Rootfs) ist Buildroot-Standard und wurde nicht
neu erfunden.

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
