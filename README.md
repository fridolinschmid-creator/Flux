# Flux

Ein mobiles Betriebssystem, das auf KI statt auf einem App-Grid aufbaut.
Kein App-Drawer mit hunderten Icons — der KI-Assistent **ist** der
Homescreen. Man entsperrt das Geraet (optional per PIN-Code) und fragt
direkt, statt zu suchen. Einstellungen und ein Dateibrowser sind ueber
den Assistenten erreichbar (zwei Schnellzugriff-Knoepfe oder Tippen/
Sprechen von "Einstellungen"/"Dateien") -- bewusst kein zweites App-Grid.

Will die KI eine Mail, SMS oder einen Anruf ausloesen, fuehrt sie das
**nie direkt aus**: es kommt immer erst ein Apple-artiger Bestaetigungs-
Dialog (Senden/Abbrechen/Bearbeiten), siehe "KI-Aktionen" unten.

## Verifiziert: echter Boot in QEMU (aarch64), touch-first

Kein Mockup -- das ist ein echter Linux/ARM64-Kernel (Buildroot-gebaut),
der in QEMU bootet, bei dem `flux-shell` auf den von `virtio-gpu`
bereitgestellten Framebuffer zeichnet und `fluxaid` ueber den
Unix-Socket antwortet. Bedienung komplett ohne Tastatur moeglich --
per `virtio-tablet` simuliertem Touch (Wisch-Geste + Bildschirm-
tastatur), Hardware-Tastatur funktioniert weiterhin parallel.

> **Zu den Bildern:** Die folgenden Screenshots werden direkt aus dem
> echten UI-Code gerendert (`shell/tools/render_screenshots.c` ueber
> `flux_fb_open_null()`) -- es ist exakt derselbe Zeichen-Code mit
> denselben `flux_ui_draw_*`-Funktionen, der auf dem Geraet/in QEMU auf
> den Framebuffer zeichnet, nur in einen RAM-Puffer statt nach `/dev/fb0`.
> Die Symbole sind jetzt echte **Lucide-Vektor-Icons** (siehe
> [`docs/ICONS.md`](docs/ICONS.md)).

| Lockscreen (Wisch-Hinweis) | Assistent (Homescreen) | Bildschirmtastatur | Lokaler Intent | Ehrlicher Cloud-Hinweis |
|---|---|---|---|---|
| ![Lockscreen](docs/screenshots/01_lockscreen.png) | ![Assistent](docs/screenshots/04_assistent_leer.png) | ![Bildschirmtastatur](docs/screenshots/05_assistent_tipp.png) | ![Lokaler Intent](docs/screenshots/07_assistent_antwort.png) | ![Cloud-Hinweis](docs/screenshots/30_cloud_fallback.png) |

("Akku" -> kein Sensor in QEMU vorhanden, ehrlich gemeldet statt erfunden.
"Wer bist du" -> kein `FLUX_AI_API_KEY` gesetzt, ehrlich gemeldet statt
Absturz oder Fantasieantwort.)

### Neue Vektor-Icons (Lucide via NanoSVG)

Alle Symbole stammen jetzt aus dem Open-Source-Set **Lucide** (MIT) und
werden mit **NanoSVG** (zlib) gerastert -- gestochen scharf, einheitlich,
in jeder Akzentfarbe einfaerbbar. Details: [`docs/ICONS.md`](docs/ICONS.md).

| Einstellungen (Icons) | Kontakte | Kalender | WLAN-Auswahl |
|---|---|---|---|
| ![Einstellungen](docs/screenshots/13_einstellungen.png) | ![Kontakte](docs/screenshots/20_kontakte.png) | ![Kalender](docs/screenshots/19_kalender.png) | ![WLAN](docs/screenshots/31_wlan.png) |

### Neue Features (Batch 5)

| Lockscreen: Badge + Alarm-Chip | Lockscreen: Kalender-Chip | Vollbild-Wecker | Nutzungsgewohnheiten |
|---|---|---|---|
| ![Lockscreen Badge](docs/screenshots/35_lockscreen_badge_chip.png) | ![Lockscreen Kalender](docs/screenshots/36_lockscreen_kalender_chip.png) | ![Wecker](docs/screenshots/37_alarm_klingelt.png) | ![Gewohnheiten](docs/screenshots/39_gewohnheiten.png) |

- **Lockscreen Badge**: Roter Zaehler oben rechts zeigt ungelesene Benachrichtigungen.
- **Info-Chip**: Naechster Alarm (< 2h) oder Kalendertermin (< 24h) erscheint direkt unter dem Datum.
- **Vollbild-Wecker**: `notification.c` prueft jede Minute `/tmp/flux_alarms.txt`; bei Faelligkeit schreibt er `/tmp/flux_alarm_ring.txt`, die Shell zeigt diesen roten Vollbild-Screen.
- **Gewohnheiten-Screen**: Zeigt alle KI-Anfragen mit Uhrzeit aus `/etc/flux/habits.txt` (neueste zuerst, scrollbar). Erreichbar per `gewohnheiten` / `aktivitaet` / `statistik`.

---

## Ehrliche Einordnung (bitte zuerst lesen)

"Besser als Android und iOS" ist als wortwoertliches Versprechen nicht
seriös erfüllbar — diese Systeme sind das Ergebnis von tausenden
Personenjahren Arbeit an Hardware-Treibern, Funkstacks, Sicherheits-
Sandboxing und App-Oekosystemen. Flux faengt nicht bei null an (kein
eigener Kernel, kein eigener Funkstack), sondern baut bewusst auf dem
**Linux-Kernel** auf — so wie Android das auch tut, und so wie es jedes
realistische Hobby-/Indie-Mobile-OS macht (siehe postmarketOS, SailfishOS,
Plasma Mobile). Der Linux-Kernel bringt echte Geraetetreiber,
Speicherverwaltung und Multitasking mit, die niemand sinnvoll von Grund
auf neu schreibt.

**Was an Flux tatsaechlich eigen ist:** die komplette UI- und KI-Schicht
oberhalb des Kernels. Kein Android-Userspace, keine Java/Kotlin-VM, keine
App-Sandbox-Architektur von Google. Stattdessen: ein eigener,
ressourcenschonender Daemon, der KI-Antworten direkt ins Betriebssystem
einbaut, und eine eigene, in C geschriebene Shell, die direkt auf den
Framebuffer zeichnet.

Aktuell lauffaehig: **QEMU `aarch64 virt`** (generische ARM64-Plattform).
Lauffaehig auf echter Telefon-Hardware ist ein eigener, deutlich
groesserer Schritt (Geraetebaum/Treiber fuer genau dein Zielgeraet,
Touchscreen-Kalibrierung, Modem/RIL, Akku-Management) — siehe Roadmap.

---

## KI-Aktionen: Mail, SMS, Anruf (Bestaetigungs-Dialog)

Erkennt `fluxaid` in einer Antwort einen klaren Auftrag ("schreib eine
Mail an...", "ruf... an"), antwortet es mit einem strukturierten
`ACTION:`-Block statt nur Text (`fluxai/src/provider.c`, System-Prompt).
`flux-shell` parst das (`shell/src/action.c`) und zeigt **immer** erst
den Bestaetigungs-Dialog -- drei grosse Knoepfe: Senden, Abbrechen,
Bearbeiten (Text vor dem Senden aendern). Erst ein Tap auf "Senden"
schickt eine `X:`-Ausfuehr-Anfrage an `fluxaid`. Die KI fuehrt nie etwas
direkt aus.

**Mail funktioniert wirklich:** `fluxaid` verschickt sie per SMTP
(`fluxai/src/mail.c`, libcurl). Host/Port/Benutzer/Passwort/Absender
werden in den Einstellungen hinterlegt (siehe unten) -- ohne Konfiguration
gibt es eine ehrliche Fehlermeldung statt eines stillen Fehlschlags.

**SMS und Anruf sind ehrliche Stubs:** QEMU `virt` hat kein Mobilfunk-
Modem und kein eSIM -- diese Hardware existiert in der virtuellen
Maschine schlicht nicht. `fluxai/src/telephony.c` meldet das wahrheits-
gemaess ("kein Modem erkannt") statt einen Erfolg zu erfinden, exakt das
gleiche Prinzip wie beim fehlenden Akku-Sensor. Die Datei ist bewusst als
austauschbares Backend geschnitten: auf echter Hardware mit
ofono/ModemManager ersetzt eine neue Implementierung nur diese eine
Datei, Protokoll und UI bleiben unveraendert.

## Einstellungen & Dateien

- **Einstellungen** (`FLUX_SCREEN_SETTINGS`): PIN-Code, SMTP-Zugangsdaten,
  Absenderadresse, Cloud-API-Key. Geheimnisse (PIN, SMTP-Passwort,
  API-Key) werden in der Liste nur als "gesetzt"/"********" angezeigt,
  nie im Klartext. Gespeichert in `/etc/flux/flux.conf` (`chmod 0600`,
  siehe Sicherheits-Hinweis).
- **Dateien** (`FLUX_SCREEN_FILES`): einfacher Read-Only-Browser (rein/
  raus navigieren, Ordner/Dateigroessen anzeigen). Bewusst kein Loeschen/
  Umbenennen -- ein erster sicherer Schritt, kein vollwertiger
  Datei-Manager.

## Mikrofon-Knopf (Whisper) -- aktuell ein ehrlicher Platzhalter

Der Assistent hat einen Mikrofon-Knopf neben der Eingabezeile. QEMU
`virt` hat aktuell **kein Audiogeraet** -- es gibt nichts, von dem echte
Sprache aufgenommen werden koennte. Ein Tap meldet das ehrlich ("kein
Mikrofon erkannt") statt eine Aufnahme zu simulieren. Geplant: lokales
`whisper.cpp` (kein Cloud-Whisper, Privacy-Grund wie beim Rest des
Systems) -- braucht zuerst ein virtuelles Audiogeraet in der QEMU-Konfig
und ein gebuendeltes Modell im Rootfs-Overlay, siehe Roadmap.

---

## Architektur

```
┌─────────────────────────────────────────────┐
│  flux-shell (eigene UI, kein App-Grid)       │  <- zeichnet direkt auf
│  Lock -> (PIN) -> Assistent (Homescreen)     │     /dev/fb0 (Framebuffer,
│  -> Bestaetigung/Bearbeiten/Einstellungen/   │     Double-Buffering)
│     Dateien -- alles vom Assistenten aus     │
└───────────────────┬───────────────────────────┘
                    │ Unix-Socket (/run/flux/fluxai.sock)
                    │ Q:<frage>  oder  X:<aktion> (nach Bestaetigung)
┌───────────────────┴───────────────────────────┐
│  fluxaid (System-KI-Daemon, kein App-Prozess)│  <- lokale Intents zuerst
│  - lokale Intents: Uhrzeit, Akku, Uptime ...  │     (Geschwindigkeit +
│  - Cloud-Fallback ueber eigenen API-Key       │     Privacy), Cloud nur
│  - Aktionen: Mail (SMTP, echt) / SMS+Anruf    │     wenn wirklich noetig
│    (ehrlicher Modem-Stub, siehe oben)         │
└───────────────────┬───────────────────────────┘
                    │
┌───────────────────┴───────────────────────────┐
│  Linux-Kernel (Buildroot, aarch64)            │  <- Treiber, Speicher,
│  virtio-gpu/DRM, virtio-input, ext4           │     Multitasking: nicht
└─────────────────────────────────────────────┘     selbst geschrieben
```

**Warum kein App-Grid?** Das ist die eigentliche Differenzierung zu
Android/iOS, nicht ein technisches Detail. Die zentrale Interaktion ist
eine Frage in natuerlicher Sprache statt "welches Icon tippe ich an".

**Warum Framebuffer statt Wayland/eines Compositors?** Fuer den Prototyp
ist das die einfachste Schicht, die wirklich ruckelfrei laeuft (siehe
"Performance" unten). Ein echter Compositor (fuer mehrere Fenster,
Animationen, GPU-Beschleunigung) ist ein spaeterer Roadmap-Schritt — aber
genau wie beim x86-Kernel-Vorlaeufer-Projekt gilt: erst die unterste
Schicht zum Laufen bringen, dann ausbauen.

---

## Performance ("es muss fluessig laufen")

Das war explizite Vorgabe, deshalb bewusste Entscheidungen dafuer:

- **C statt Electron/JS/Flutter** fuer Shell und Daemon — keine VM, kein
  Garbage Collector, kein Rendering-Framework-Overhead.
- **Double-Buffering + Dirty-Row-Tracking** im Framebuffer-Code
  (`shell/src/fb.c`): jeder Frame landet zuerst im RAM-Backbuffer; beim
  Praesentieren wird nur kopiert, was sich seit dem letzten Frame
  tatsaechlich geaendert hat (`flux_fb_present`). Eine Uhr, die sich
  einmal pro Sekunde aendert, kostet keine 60 Vollbild-Kopien pro Sekunde.
- **Lokale Intents ohne Netzwerk-Roundtrip** (`fluxai/src/actions.c`):
  "Wie spaet ist es" oder "Akkustand" werden direkt im Daemon beantwortet,
  ohne auf eine Cloud-API zu warten — das ist nicht nur schneller,
  sondern verlaesst das Geraet nie (Privacy by construction, wie schon im
  Vorlaeufer-Projekt angestrebt).
- **Kein Re-Parsing/Re-Layout bei jedem Tastendruck**: nur das geaenderte
  UI-Element wird neu gezeichnet, der Rest bleibt im Backbuffer stehen.

Ehrlich: echte 60-fps-Animationen brauchen GPU-Compositing (DRM/KMS statt
rohem Framebuffer) — das ist ein Roadmap-Punkt, kein Tag-1-Feature. Was
heute schon stimmt: keine Komponente der Software-Architektur erzeugt
unnoetige Arbeit, die spaeter wieder rausoptimiert werden muesste.

---

## Projektstruktur

```
Flux/
├── shell/              flux-shell -- Framebuffer-UI
│   └── src/
│       ├── fb.c/.h          Framebuffer + Double-Buffering (+ RGBA-Blit)
│       ├── stb_easy_font.h  Public-Domain-Bitmapfont (nothings/stb)
│       ├── icons.c/.h       Vektor-Icons (Lucide-SVGs via NanoSVG)
│       ├── nanosvg*.h       SVG-Parser + -Rasterizer (memononen, zlib)
│       ├── anim.h           Easing-/Animationshilfen (header-only)
│       ├── input.c/.h       Tastatur/Touch ueber Linux evdev
│       ├── ipc.c/.h         Client fuer fluxaid (Q:/X:-Anfragen)
│       ├── action.c/.h      Parst ACTION:-Vorschlaege, baut X:-Anfragen
│       ├── ui.c/.h          Alle Bildschirme (Lock/PIN/Assistent/
│       │                    Bestaetigung/Bearbeiten/Einstellungen/Dateien)
│       └── main.c           Event-Loop / Zustandsmaschine
├── fluxai/              fluxaid -- System-KI-Daemon
│   └── src/
│       ├── actions.c/.h     Lokale Geraete-Intents (Zeit, Akku, Uptime)
│       ├── provider.c/.h    Cloud-Fallback (Anthropic API, eigener Key)
│       ├── exec.c/.h        Fuehrt bestaetigte Aktionen aus (X:-Anfragen)
│       ├── mail.c/.h        Echter SMTP-Versand (libcurl)
│       ├── telephony.c/.h   SMS/Anruf -- ehrlicher Modem-Stub, austauschbar
│       └── main.c           Unix-Socket-Server
├── common/
│   ├── flux_protocol.h      Mini-Protokoll Shell <-> Daemon (Q:/X:/ACTION:)
│   ├── flux_config.c/.h     Gemeinsame Konfigdatei (/etc/flux/flux.conf)
│   └── flux_sha256.c/.h     SHA-256 fuer den PIN-Hash
├── build/
│   ├── overlay/              Rootfs-Overlay (QEMU: /etc/inittab, Binaries)
│   ├── overlay-pi5/          Rootfs-Overlay fuer den Raspberry Pi 5
│   ├── raspberrypi5/         Pi-5-Konfig (Defconfig-Fragment, config.txt,
│   │                         Kernel-Fragment, Boot-Logo)
│   ├── build.sh              Buildroot-Build + Cross-Compile (QEMU)
│   ├── build-pi5.sh          dasselbe fuer echten Raspberry Pi 5
│   └── flash-pi5.sh          fertiges Image auf SD-Karte schreiben
└── docs/
    ├── ARCHITECTURE.md
    ├── ICONS.md              Vektor-Icons (Lucide/NanoSVG) + Animationen
    ├── RASPBERRY-PI-5.md     Installation + Boot-Branding auf dem Pi 5
    └── LOGO-PROMPT.md        ChatGPT-Prompt fuer das Flux-Logo
```

---

## Bauen & Starten

### Schnelltest auf dem Host (ohne echtes Geraet/QEMU)
```bash
cd fluxai && make && ./fluxaid &     # Daemon starten
cd ../shell && make                  # nur Compile-Test, /dev/fb0 fehlt auf dem Host
```

### Vollstaendiges Image fuer QEMU aarch64
Baut Linux-Kernel + Rootfs (Buildroot) und kompiliert flux-shell/fluxaid
mit dem dabei erzeugten Cross-Compiler. Dauert beim ersten Mal lange
(Toolchain + Kernel werden von Grund auf gebaut).
```bash
./build/build.sh
```
Start in QEMU (Grafikfenster + virtuelle Tastatur):
```bash
./build/run-qemu.sh
```

### Echte Hardware: Raspberry Pi 5
Flux laeuft auch auf einem echten **Raspberry Pi 5** (BCM2712, arm64) --
der erste Geraete-Port jenseits von QEMU. Weil der Pi 5 dieselbe
CPU-Architektur hat und ueber die DRM-fbdev-Emulation weiterhin ein
`/dev/fb0` bereitstellt, mussten `flux-shell`/`fluxaid` **nicht**
umgeschrieben werden -- nur neu uebersetzt und anders gepackt:
```bash
./build/build-pi5.sh            # baut /tmp/flux-build/out-pi5/images/sdcard.img
./build/flash-pi5.sh /dev/sdX   # auf microSD schreiben (Geraet sicher pruefen!)
```
Vollstaendige Anleitung -- inklusive **„welches OS im Hintergrund"**,
WLAN/NTP und **wie man die Marke beim Booten aendert** (Firmware-Splash,
Kernel-Boot-Logo, In-OS-Branding) -- in
[`docs/RASPBERRY-PI-5.md`](docs/RASPBERRY-PI-5.md). Einen fertigen
Logo-Prompt fuer ChatGPT gibt es in
[`docs/LOGO-PROMPT.md`](docs/LOGO-PROMPT.md).

### Eigenen KI-Zugang einrichten (optional)
Ohne API-Key beantwortet `fluxaid` nur lokale Fragen (Zeit, Akku, Uptime)
und sagt ehrlich, dass kein Cloud-Zugang konfiguriert ist.

**KI-Anbieter waehlbar.** Flux unterstuetzt mehrere vorkonfigurierte
Anbieter; der aktive wird in den Einstellungen unter **KI-Anbieter** per
Tipp durchgeschaltet (Anthropic → DeepSeek → NVIDIA). API-Key und Modell
darunter beziehen sich immer auf den gerade gewaehlten Anbieter:

| Anbieter | Format | Endpunkt | Standardmodell |
|---|---|---|---|
| Anthropic Claude | Messages API (+ Prompt-Caching) | `api.anthropic.com` | `claude-haiku-4-5-20251001` |
| DeepSeek | OpenAI-kompatibel | `api.deepseek.com` | `deepseek-chat` |
| NVIDIA NIM | OpenAI-kompatibel | `integrate.api.nvidia.com` | `meta/llama-3.1-8b-instruct` |

Konfiguriert wird ueber die Einstellungen oder direkt in
`/etc/flux/flux.conf`:
```ini
ai_provider=deepseek          # anthropic | deepseek | nvidia
api_key=...                   # Anthropic-Key
deepseek_key=...              # DeepSeek-Key
nvidia_key=...                # NVIDIA-NIM-Key
anthropic_model=...           # optional, sonst Standardmodell
deepseek_model=...            # optional
nvidia_model=...              # optional
```
Alternativ per Umgebungsvariable (`FLUX_AI_API_KEY`, `DEEPSEEK_API_KEY`,
`NVIDIA_API_KEY`). Bei Rate-Limits (HTTP 429, z.B. NVIDIA) wiederholt
`fluxaid` die Anfrage automatisch mit kurzem Backoff.

### E-Mail einrichten (Senden + Lesen)
In den Einstellungen gibt es **einen** Eintrag „E-Mail Einstellungen": dort
nur die **E-Mail-Adresse** und das **App-Passwort** eingeben. SMTP- und
IMAP-Server samt Ports werden automatisch aus der Domain abgeleitet
(Gmail, Outlook/Hotmail, iCloud, Yahoo, GMX, web.de, t-online, posteo,
mailbox.org; bei unbekannten Domains `smtp.<domain>` / `imap.<domain>`).

Damit kann `fluxaid` Mails senden (SMTP) und ungelesene Mails abrufen
(IMAP), sodass die KI sie zusammenfassen kann („Fasse meine ungelesenen
Mails zusammen"). KI-Tools: `mail_unread` (Kopfzeilen ungelesener Mails) und
`mail_read` (Text einer Mail per UID, setzt das \Seen-Flag nicht).

Wer die Felder lieber direkt setzt, kann das weiterhin in
`/etc/flux/flux.conf` tun (`smtp_host/port/user/pass/from`,
`imap_host/port/user/pass`).

### Web-Suche (SearXNG, optional)
Die KI-Anbieter (DeepSeek/NVIDIA/Anthropic) suchen über die API **nicht**
selbst im Netz. Flux bringt dafür ein eigenes, anbieter-unabhängiges Tool
`web_search` mit, das eine **eigene SearXNG-Instanz** abfragt — bewusst
selbst gehostet (eigener Index, keine Profilbildung, kein API-Key im Gerät).

Empfohlenes Setup: SearXNG läuft auf dem **MacBook** (Backend), das Flux-Gerät
fragt es im selben Netz ab.

1. SearXNG auf dem MacBook starten (am einfachsten via Docker):
   ```bash
   docker run --rm -p 8888:8080 -v ./searxng:/etc/searxng searxng/searxng
   ```
2. In `searxng/settings.yml` das **JSON-Format aktivieren** (sonst antwortet
   SearXNG mit HTTP 403):
   ```yaml
   search:
     formats:
       - html
       - json
   ```
3. In Flux unter **Einstellungen → „Web-Suche (SearXNG)"** die URL eintragen,
   z. B. `http://macbook.local:8888` (oder die IP des MacBooks). Direkt in
   `/etc/flux/flux.conf`: `searxng_url=http://macbook.local:8888`.

Danach kann jeder KI-Anbieter über `web_search` aktuelle Infos holen
(„Suche im Internet nach …"). Später lässt sich dieselbe Konfiguration auf
einen produktiven SearXNG-/Such-Proxy umstellen, ohne Code-Änderung.

---

## Roadmap

1. ~~Framebuffer-UI mit Double-Buffering~~
2. ~~System-KI-Daemon mit lokalen Intents + Cloud-Fallback~~
3. ~~Bootbares aarch64-Image (Buildroot, QEMU `virt`)~~
4. ~~Touch-Input statt nur Tastatur~~ -- Wisch-Geste zum Entsperren,
   Bildschirmtastatur fuer den Assistenten, Hardware-Tastatur bleibt
   nebenbei nutzbar (`shell/src/input.c`, `shell/src/ui.c`)
5. ~~PIN-Sperre, Einstellungen, Dateibrowser, KI-Aktionen mit
   Bestaetigungs-Dialog, echte SMTP-Mail~~ (siehe oben)
6. Lokales `whisper.cpp` fuer den Mikrofon-Knopf -- braucht zuerst ein
   virtuelles Audiogeraet in `build/run-qemu.sh` (aktuell keins
   vorhanden) und ein gebuendeltes Modell im Rootfs-Overlay
7. Echtes Modem-Backend (ofono/ModemManager) fuer SMS/Anruf auf echter
   Hardware -- ersetzt nur `fluxai/src/telephony.c`, siehe oben
8. ~~Stimm-Erkennung als zweiter Entsperr-Faktor~~ -- `voice_unlock.c/.h`
   (RMS-Fingerabdruck-Stub), `FLUX_SCREEN_VOICE_ENROLL` + `FLUX_SCREEN_VOICE_VERIFY`,
   Einstellungs-Eintrag. QEMU: kein Mikrofon vorhanden, ehrliche Meldung.
   Backend (nur `voice_unlock.c`) durch ECAPA-TDNN ersetzbar ohne UI-Aenderung.
9. Echter Compositor (DRM/KMS, GPU-Beschleunigung, Animationen, mehrere
   "Karten" statt nur Lockscreen+Assistent)
10. ~~Benachrichtigungen als eigener Systemdienst~~ -- `notification.c/.h`
    in fluxaid als pthread: prueft Kalender/Memory/Batterie/Proaktiv alle
    15 Min, schreibt nach `/tmp/flux_notifications.txt`. Compile-Test bestanden.
11. Portierung auf ein konkretes echtes Geraet (Geraetebaum, Touchscreen-
    Treiber, Akku/Power-Management) — das ist der Schritt, der "Telefon"
    ernst nimmt, siehe postmarketOS-Doku zum Geraete-Porting.
    **Erster echter Port: Raspberry Pi 5** (`build/build-pi5.sh`,
    `docs/RASPBERRY-PI-5.md`) -- arm64-Kernel der Pi-Foundation,
    Buildroot-Rootfs, `/dev/fb0` per DRM-fbdev-Emulation, echtes WLAN.
    SMS/Anruf + Akku bleiben dort ehrliche Stubs (kein Modem/Akku-Sensor).
12. Sicherheitsmodell fuer Drittanbieter-Apps (Sandbox/Permissions) --
    aktuell laeuft alles als root, das ist fuer einen Dev-Build okay, fuer
    ein echtes Telefon-Betriebssystem nicht

### Hinweis zu Stimm-Entsperrung (Punkt 8)

Sprecher-Verifikation ist ein eigenes, nicht-triviales ML-Problem (nicht
einfach "Whisper transkribiert Text und vergleicht ihn") und sollte nie
die *einzige* Schranke sein -- Stimmen lassen sich aufnehmen/synthetisieren,
und ein Mikrofonausfall darf nicht zum Komplett-Lockout fuehren. Realistischer
Ansatz: Sprecher-Embedding-Modell (z.B. ein kompaktes ECAPA-TDNN/x-vector-
Modell) lokal ausfuehren, Aehnlichkeit zu einem beim Einrichten aufgenommenen
Referenz-Embedding pruefen, und das **immer** nur als zweiten Faktor neben
dem PIN anbieten, nie als alleinigen. Das ist deutlich mehr Infrastruktur
(Mikrofon in QEMU, Embedding-Modell, sicherer Speicherort fuer das
Referenz-Embedding) als der aktuelle Funktionsumfang -- bewusst nicht in
dieser Iteration umgesetzt.

## Sicherheits-Hinweis (Dev-Build)
Das gebaute Image hat einen Root-Login ohne Passwort auf der seriellen
Konsole und keine App-Sandbox -- das ist ein bewusster Kompromiss fuer
einen Entwicklungs-/Demo-Build, kein produktionsreifes Sicherheitsmodell.
`/etc/flux/flux.conf` (PIN-Hash, SMTP-Zugangsdaten, Cloud-API-Key) liegt
unverschluesselt, nur per `chmod 0600` geschuetzt -- kein Ersatz fuer
einen echten Secret-Store. Vor jedem Schritt in Richtung "echtes Geraet"
muss das ueberarbeitet werden.
