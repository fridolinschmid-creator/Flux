<div align="center">

# FluxOS

**Das erste mobile Betriebssystem, bei dem der KI-Assistent der Homescreen ist.**

Kein App-Grid. Keine hundert Icons. Du entsperrst das Gerät und fragst —
statt zu suchen.

[![Plattform](https://img.shields.io/badge/Plattform-Linux%20arm64-1f6feb)](#zielplattformen)
[![QEMU](https://img.shields.io/badge/QEMU-aarch64%20virt-2ea043)](#schnellstart)
[![Hardware](https://img.shields.io/badge/Hardware-Raspberry%20Pi%205-c2410c)](docs/RASPBERRY-PI-5.md)
[![Sprache](https://img.shields.io/badge/Sprache-C%20(C11)-555)](#technologie)
[![Status](https://img.shields.io/badge/Status-Prototyp%20(bootet)-yellow)](#projektstatus)

[Konzept](#konzept) · [Demo](#verifiziert-echter-boot-in-qemu) · [Schnellstart](#schnellstart) · [Architektur](#architektur) · [Konfiguration](#konfiguration) · [Roadmap](#roadmap)

</div>

---

## Konzept

FluxOS dreht die Grundinteraktion eines Smartphones um. Statt eines Rasters
aus Apps, durch das man navigiert, steht eine einzige Frage in natürlicher
Sprache im Mittelpunkt — der KI-Assistent **ist** der Homescreen.

| Klassisches Smartphone | FluxOS |
|---|---|
| App-Drawer mit Dutzenden Icons | Eine Eingabezeile / ein Sprachbefehl |
| „Welches Icon tippe ich an?" | „Schreib meiner Mutter eine Mail" |
| App startet → Aktion suchen | Assistent erkennt Intent → bestätigen → fertig |
| Cloud-first | **Lokale Intents zuerst**, Cloud nur wenn nötig |

Zwei Prinzipien ziehen sich durch das ganze System:

1. **KI-first statt App-first.** Einstellungen und Dateibrowser sind über den
   Assistenten erreichbar (Schnellzugriff-Knöpfe oder „Einstellungen" / „Dateien"
   tippen/sprechen) — bewusst kein zweites App-Grid.
2. **Bestätigung vor Aktion.** Will die KI eine Mail, SMS oder einen Anruf
   auslösen, führt sie das **nie direkt aus**. Es erscheint immer erst ein
   Bestätigungs-Dialog (Senden / Abbrechen / Bearbeiten). Siehe
   [KI-Aktionen](#ki-aktionen-mail-sms-anruf).

---

## Verifiziert: echter Boot in QEMU

Kein Mockup. FluxOS bootet als echter Linux-/ARM64-Kernel (Buildroot-gebaut)
in QEMU. `flux-shell` zeichnet auf den von `virtio-gpu` bereitgestellten
Framebuffer, `fluxaid` antwortet über einen Unix-Socket. Bedienung komplett
ohne Tastatur — per `virtio-tablet`-Touch (Wisch-Geste + Bildschirmtastatur);
eine Hardware-Tastatur funktioniert parallel weiter.

> **Zu den Screenshots:** Sie werden direkt aus dem echten UI-Code gerendert
> (`shell/tools/render_screenshots.c` über `flux_fb_open_null()`) — exakt
> derselbe Zeichen-Code mit denselben `flux_ui_draw_*`-Funktionen, der auf
> dem Gerät auf den Framebuffer zeichnet, nur in einen RAM-Puffer statt nach
> `/dev/fb0`. Die Symbole sind echte **Lucide-Vektor-Icons**
> (siehe [`docs/ICONS.md`](docs/ICONS.md)).

| Lockscreen | Assistent (Homescreen) | Bildschirmtastatur | Lokaler Intent | Cloud-Hinweis |
|---|---|---|---|---|
| ![Lockscreen](docs/screenshots/01_lockscreen.png) | ![Assistent](docs/screenshots/04_assistent_leer.png) | ![Tastatur](docs/screenshots/05_assistent_tipp.png) | ![Intent](docs/screenshots/07_assistent_antwort.png) | ![Cloud](docs/screenshots/30_cloud_fallback.png) |

| Einstellungen | Kontakte | Kalender | WLAN-Auswahl |
|---|---|---|---|
| ![Einstellungen](docs/screenshots/13_einstellungen.png) | ![Kontakte](docs/screenshots/20_kontakte.png) | ![Kalender](docs/screenshots/19_kalender.png) | ![WLAN](docs/screenshots/31_wlan.png) |

| Eingehender Anruf | Verbunden + KI-Mitschnitt | Vollbild-Wecker | Nutzungsgewohnheiten |
|---|---|---|---|
| ![Anruf](docs/screenshots/38b_anruf.png) | ![Verbunden](docs/screenshots/38c_anruf_verbunden.png) | ![Wecker](docs/screenshots/37_alarm_klingelt.png) | ![Gewohnheiten](docs/screenshots/39_gewohnheiten.png) |

---

## Funktionen

| Bereich | Funktion |
|---|---|
| **Assistent** | Natürlichsprachige Anfragen, lokale Intents (Zeit, Akku, Uptime) ohne Netzwerk, Cloud-Fallback bei Bedarf |
| **KI-Aktionen** | Mail / SMS / Anruf — immer mit Apple-artigem Bestätigungs-Dialog (Senden / Abbrechen / Bearbeiten) |
| **E-Mail** | Echter Versand per SMTP und Abruf ungelesener Mails per IMAP (libcurl); Server werden aus der Domain abgeleitet |
| **Telefonie** | Vollbild-Anruf-Screen (annehmen/auflegen) mit KI-Mitschnitt, gespeichert als Markdown unter `/home/user/Anrufe/` |
| **KI-Anbieter** | Umschaltbar: Anthropic Claude, DeepSeek, NVIDIA NIM |
| **Web-Suche** | `web_search`-Tool gegen eine selbst-gehostete SearXNG-Instanz (kein Profiling, kein Drittanbieter-Key) |
| **Sicherheit** | PIN-Sperre (SHA-256-Hash), Stimm-Entsperrung als zweiter Faktor (Stub) |
| **System** | Lockscreen-Badges, Info-Chips (nächster Alarm/Termin), Vollbild-Wecker, Nutzungsstatistik, Read-Only-Dateibrowser |
| **Benachrichtigungen** | Eigener Systemdienst in `fluxaid`: prüft Kalender/Memory/Akku/Proaktiv alle 15 Min |
| **UI** | Direkt auf den Framebuffer gezeichnet, Double-Buffering + Dirty-Row-Tracking, echte Lucide-Vektor-Icons |

---

## Architektur

FluxOS besteht aus zwei eigenen Komponenten oberhalb eines Standard-Linux-Kernels.
Jede Schicht macht genau eine Sache — die UI zeichnet, der Daemon denkt, der
Kernel verwaltet Hardware.

```
┌───────────────────────────────────────────────┐
│  flux-shell  (UI-Prozess, kein App-Grid)       │   zeichnet direkt auf
│  Lock → (PIN) → Assistent (Homescreen)         │   /dev/fb0 — Framebuffer
│  → Bestätigung / Bearbeiten / Einstellungen /  │   mit Double-Buffering
│     Dateien — alles vom Assistenten aus        │
└───────────────────────┬───────────────────────┘
                        │ Unix-Socket  /run/flux/fluxai.sock
                        │ Q:<frage>   |   X:<aktion> (nach Bestätigung)
┌───────────────────────┴───────────────────────┐
│  fluxaid  (System-KI-Daemon, kein App-Prozess) │   1. lokale Intents
│  - lokale Intents: Zeit, Akku, Uptime …        │      (schnell + privat)
│  - Cloud-Fallback (Anthropic/DeepSeek/NVIDIA)  │   2. Cloud nur wenn nötig
│  - Aktionen: Mail (SMTP, echt) / SMS+Anruf     │   3. Tools: Mail, IMAP,
│  - Tools, Benachrichtigungen, Mitschnitt       │      Web-Suche, Vision …
└───────────────────────┬───────────────────────┘
                        │
┌───────────────────────┴───────────────────────┐
│  Linux-Kernel  (Buildroot, aarch64)            │   Treiber, Speicher,
│  virtio-gpu/DRM, virtio-input, ext4            │   Multitasking — nicht
└────────────────────────────────────────────────┘   selbst geschrieben
```

**Protokoll (`common/flux_protocol.h`).** Bewusst kein JSON: zeilenbasiert,
ein Request pro Verbindung. Client → Server `Q:<frage>` oder
`X:<typ>\nTO:…\nSUBJECT:…\nBODY:\n…`; Server → Client `A:<antwort>\nEND`.
Schlägt die KI eine Aktion vor, antwortet sie auf `Q:` mit einem
strukturierten `ACTION:`-Block, den die Shell zum Bestätigungs-Dialog parst.

**Warum kein App-Grid?** Das ist die eigentliche Differenzierung zu Android/iOS,
kein fehlendes Feature. Die zentrale Interaktion ist eine Frage in natürlicher
Sprache statt „welches Icon tippe ich an".

**Warum Framebuffer statt Compositor?** Für den Prototyp die einfachste
Schicht, die wirklich ruckelfrei läuft. Ein echter Compositor (mehrere Fenster,
GPU-Animationen) ist ein späterer Roadmap-Schritt.

Vollständige Beschreibung: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## Technologie

- **Sprache:** C (C11, `-O2`), kein Garbage Collector, keine VM, kein
  Rendering-Framework — Shell und Daemon starten in Millisekunden.
- **Rendering:** eigener Framebuffer-Code mit Double-Buffering und
  Dirty-Row-Tracking (`flux_fb_present` kopiert nur geänderte Zeilen).
- **Text:** `stb_easy_font` (Public Domain) als Vektor-Quads.
- **Icons:** Lucide-SVGs (MIT), gerastert mit NanoSVG (zlib), gecacht und
  als eingefärbte Maske geblittet.
- **Eingabe:** Linux `evdev` — Tastatur und Touch/Pointer parallel, generisch
  über `EVIOCGBIT` erkannt (nicht hart verdrahtet).
- **Netzwerk/KI:** libcurl für Cloud-API, SMTP und IMAP.
- **Basissystem:** Buildroot (Kernel + Toolchain + Rootfs), `build/overlay/`
  liefert nur die eigenen Binaries und `/etc/inittab`.

---

## Schnellstart

### 1. Host-Schnelltest (kein QEMU, kein Gerät)

```bash
# Daemon bauen & starten
cd fluxai && make && ./fluxaid &

# Shell nur kompilieren (Zeichnen braucht /dev/fb0, fehlt auf dem Host)
cd ../shell && make
```

### 2. Vollständiges Image für QEMU (aarch64)

Baut Linux-Kernel + Rootfs mit Buildroot und cross-kompiliert
`flux-shell`/`fluxaid`. Der erste Lauf dauert lange (Toolchain + Kernel
werden komplett gebaut).

```bash
./build/build.sh        # Image bauen
./build/run-qemu.sh     # in QEMU starten (headless; GUI: FLUX_DISPLAY=gtk)
```

### 3. Echte Hardware: Raspberry Pi 5

FluxOS läuft auf einem echten **Raspberry Pi 5** (BCM2712, arm64) — dem ersten
Geräte-Port jenseits von QEMU. Gleiche CPU-Architektur, `/dev/fb0` über
DRM-fbdev-Emulation: `flux-shell`/`fluxaid` mussten **nicht** umgeschrieben,
nur neu übersetzt werden.

```bash
./build/build-pi5.sh            # baut .../out-pi5/images/sdcard.img
./build/flash-pi5.sh /dev/sdX   # auf microSD schreiben (Gerät genau prüfen!)
```

Vollständige Anleitung inkl. Boot-Branding (Firmware-Splash, Kernel-Logo,
WLAN/NTP): [`docs/RASPBERRY-PI-5.md`](docs/RASPBERRY-PI-5.md).

---

## Konfiguration

Alle Einstellungen liegen in `/etc/flux/flux.conf` (`chmod 0600`) und sind
auch über den **Einstellungen**-Screen erreichbar. Geheimnisse (PIN,
SMTP-Passwort, API-Key) werden in der UI nur als „gesetzt" / `********`
angezeigt, nie im Klartext.

### KI-Anbieter

Der aktive Anbieter wird in den Einstellungen unter **KI-Anbieter** per Tipp
durchgeschaltet. API-Key und Modell beziehen sich auf den gewählten Anbieter.

| Anbieter | Format | Endpunkt | Standardmodell |
|---|---|---|---|
| Anthropic Claude | Messages API (+ Prompt-Caching) | `api.anthropic.com` | `claude-haiku-4-5-20251001` |
| DeepSeek | OpenAI-kompatibel | `api.deepseek.com` | `deepseek-chat` |
| NVIDIA NIM | OpenAI-kompatibel | `integrate.api.nvidia.com` | `meta/llama-3.1-8b-instruct` |

```ini
ai_provider=anthropic     # anthropic | deepseek | nvidia
api_key=...               # Anthropic-Key
deepseek_key=...          # DeepSeek-Key
nvidia_key=...            # NVIDIA-NIM-Key
anthropic_model=...       # optional, sonst Standardmodell
```

Alternativ per Umgebungsvariable: `FLUX_AI_API_KEY`, `DEEPSEEK_API_KEY`,
`NVIDIA_API_KEY`. Bei Rate-Limits (HTTP 429) wiederholt `fluxaid` automatisch
mit kurzem Backoff. Ohne Key beantwortet `fluxaid` nur lokale Fragen und meldet
ehrlich, dass kein Cloud-Zugang konfiguriert ist.

### E-Mail (Senden + Lesen)

In den Einstellungen unter **E-Mail Einstellungen** nur **Adresse** und
**App-Passwort** eingeben. SMTP-/IMAP-Server und Ports werden aus der Domain
abgeleitet (Gmail, Outlook/Hotmail, iCloud, Yahoo, GMX, web.de, t-online,
posteo, mailbox.org; sonst `smtp.<domain>` / `imap.<domain>`). KI-Tools:
`mail_unread` (Kopfzeilen) und `mail_read` (Text per UID, ohne `\Seen` zu setzen).

### Web-Suche (SearXNG, optional)

Die KI-Anbieter suchen über ihre API **nicht** selbst im Netz. FluxOS bringt
dafür das anbieter-unabhängige Tool `web_search` mit, das eine **selbst
gehostete SearXNG-Instanz** abfragt — eigener Index, keine Profilbildung, kein
Drittanbieter-Key im Gerät.

```bash
# SearXNG z. B. auf dem MacBook (Docker)
docker run --rm -p 8888:8080 -v ./searxng:/etc/searxng searxng/searxng
```

In `searxng/settings.yml` das **JSON-Format aktivieren** (sonst HTTP 403):

```yaml
search:
  formats: [html, json]
```

In Flux unter **Einstellungen → Web-Suche (SearXNG)** die URL eintragen, z. B.
`http://macbook.local:8888` — oder direkt:
`searxng_url=http://macbook.local:8888`.

---

## KI-Aktionen: Mail, SMS, Anruf

Erkennt `fluxaid` einen klaren Auftrag („schreib eine Mail an…", „ruf… an"),
antwortet es mit einem strukturierten `ACTION:`-Block statt Freitext
(`fluxai/src/provider.c`). `flux-shell` parst das (`shell/src/action.c`) und
zeigt **immer** erst den Bestätigungs-Dialog — drei Knöpfe: **Senden**,
**Abbrechen**, **Bearbeiten**. Erst ein Tap auf „Senden" schickt eine
`X:`-Ausführanfrage an `fluxaid`. **Die KI führt nie etwas direkt aus.**

- **Mail funktioniert wirklich:** SMTP-Versand per libcurl (`fluxai/src/mail.c`).
  Ohne Konfiguration gibt es eine ehrliche Fehlermeldung statt eines stillen
  Fehlschlags.
- **SMS und Anruf sind ehrliche Stubs:** QEMU `virt` hat kein Modem und kein
  eSIM — diese Hardware existiert in der VM schlicht nicht.
  `fluxai/src/telephony.c` meldet das wahrheitsgemäß („kein Modem erkannt")
  statt einen Erfolg zu erfinden. Die Datei ist bewusst als austauschbares
  Backend geschnitten: auf echter Hardware ersetzt eine ofono/ModemManager-
  Implementierung nur diese eine Datei — Protokoll und UI bleiben gleich.

---

## Projektstruktur

```
Flux/
├── shell/                  flux-shell — Framebuffer-UI
│   ├── src/
│   │   ├── fb.c/.h             Framebuffer + Double-Buffering (+ RGBA-Blit)
│   │   ├── stb_easy_font.h     Public-Domain-Bitmapfont (nothings/stb)
│   │   ├── icons.c/.h          Vektor-Icons (Lucide-SVGs via NanoSVG)
│   │   ├── nanosvg*.h          SVG-Parser + -Rasterizer (zlib)
│   │   ├── anim.h              Easing-/Animationshilfen (header-only)
│   │   ├── input.c/.h          Tastatur/Touch über Linux evdev
│   │   ├── ipc.c/.h            Client für fluxaid (Q:/X:-Anfragen)
│   │   ├── action.c/.h         Parst ACTION:-Vorschläge, baut X:-Anfragen
│   │   ├── ui.c/.h             Alle Bildschirme (Zustandsmaschine)
│   │   ├── voice*.c/.h         Sprachsteuerung + Stimm-Entsperrung (Stub)
│   │   └── main.c              Event-Loop / Zustandsmaschine
│   ├── tools/                  render_screenshots.c (UI → PNG, headless)
│   └── Makefile
├── fluxai/                 fluxaid — System-KI-Daemon
│   ├── src/
│   │   ├── actions.c/.h        Lokale Geräte-Intents (Zeit, Akku, Uptime)
│   │   ├── provider.c/.h       Cloud-Fallback (Anthropic/DeepSeek/NVIDIA)
│   │   ├── exec.c/.h           Führt bestätigte Aktionen aus (X:-Anfragen)
│   │   ├── mail.c/.h           SMTP-Versand (libcurl)
│   │   ├── imap.c/.h           IMAP-Abruf ungelesener Mails
│   │   ├── telephony.c/.h      SMS/Anruf — ehrlicher Modem-Stub, austauschbar
│   │   ├── tools.c/.h          KI-Tools (Web-Suche, Mail, Vision …)
│   │   ├── vision.c/.h         Bild-Analyse
│   │   ├── notification.c/.h   Benachrichtigungsdienst (pthread)
│   │   ├── proactive.c/.h      Proaktive Hinweise
│   │   ├── habits.c/.h         Nutzungsgewohnheiten/-statistik
│   │   ├── journal.c/.h        Gesprächs-/Aktivitätsjournal
│   │   └── main.c              Unix-Socket-Server
│   └── Makefile
├── common/                 Geteilter Code Shell ↔ Daemon
│   ├── flux_protocol.h        Mini-Protokoll (Q:/X:/ACTION:)
│   ├── flux_config.c/.h       Konfigdatei (/etc/flux/flux.conf)
│   └── flux_sha256.c/.h       SHA-256 für den PIN-Hash
├── build/                  Image-Build + Hardware-Port
│   ├── build.sh               Buildroot + Cross-Compile (QEMU)
│   ├── run-qemu.sh            QEMU-Start (headless/GTK)
│   ├── build-pi5.sh           Build für Raspberry Pi 5
│   ├── flash-pi5.sh           Image auf SD-Karte schreiben
│   ├── overlay/               Rootfs-Overlay (QEMU)
│   ├── overlay-pi5/           Rootfs-Overlay (Pi 5)
│   └── raspberrypi5/          Pi-5-Konfig (config.txt, Kernel-Fragment, Logo)
└── docs/
    ├── ARCHITECTURE.md        Architektur im Detail
    ├── ICONS.md               Vektor-Icons (Lucide/NanoSVG) + Animationen
    ├── RASPBERRY-PI-5.md      Installation + Boot-Branding auf dem Pi 5
    └── LOGO-PROMPT.md         Prompt für das Flux-Logo
```

---

## Performance

Flüssiges Laufen war explizite Vorgabe — daraus folgen bewusste Entscheidungen:

- **C statt Electron/JS/Flutter** für Shell und Daemon: keine VM, kein GC,
  kein Framework-Overhead.
- **Double-Buffering + Dirty-Row-Tracking** (`shell/src/fb.c`): jeder Frame
  landet im RAM-Backbuffer, präsentiert wird nur, was sich geändert hat. Eine
  Uhr, die einmal pro Sekunde tickt, kostet keine 60 Vollbild-Kopien/s.
- **Lokale Intents ohne Netzwerk-Roundtrip** (`fluxai/src/actions.c`):
  „Wie spät ist es" / „Akkustand" werden direkt im Daemon beantwortet —
  schneller, und die Frage verlässt das Gerät nie (Privacy by construction).
- **Kein Re-Layout pro Tastendruck:** nur das geänderte UI-Element wird neu
  gezeichnet.

Ehrlich: echte 60-fps-Animationen brauchen GPU-Compositing (DRM/KMS statt
rohem Framebuffer) — das ist ein Roadmap-Punkt. Was heute schon stimmt: keine
Komponente erzeugt unnötige Arbeit, die später wieder rausoptimiert werden müsste.

---

## Projektstatus

FluxOS ist ein **funktionierender Prototyp**, kein fertiges Produkt — und
kommuniziert das überall ehrlich im Code, statt Erfolge zu erfinden.

**Heute lauffähig:** QEMU `aarch64 virt` und echter **Raspberry Pi 5**.
Bootet, zeichnet, beantwortet lokale und Cloud-Fragen, versendet echte Mails,
ruft IMAP/Web-Suche auf.

**Was eigen ist:** die komplette UI- und KI-Schicht oberhalb des Kernels —
kein Android-Userspace, keine Java/Kotlin-VM, keine Google-App-Sandbox. Ein
eigener, ressourcenschonender Daemon und eine eigene, in C geschriebene Shell.

**Was bewusst noch nicht da ist:** ein eigener Kernel oder Funkstack (FluxOS
baut wie Android, postmarketOS und SailfishOS auf dem Linux-Kernel auf),
GPU-Compositing, ein App-Sandbox-Sicherheitsmodell und Modem-/Mikrofon-Hardware
in der VM. Stellen, an denen Hardware fehlt (Akku, Modem, Mikrofon), melden das
wahrheitsgemäß statt zu simulieren.

---

## Roadmap

- [x] Framebuffer-UI mit Double-Buffering
- [x] System-KI-Daemon mit lokalen Intents + Cloud-Fallback
- [x] Bootbares aarch64-Image (Buildroot, QEMU `virt`)
- [x] Touch-Input (Wisch-Entsperren, Bildschirmtastatur)
- [x] PIN-Sperre, Einstellungen, Dateibrowser
- [x] KI-Aktionen mit Bestätigungs-Dialog + echter SMTP-Mail
- [x] IMAP-Abruf + Mail-Zusammenfassung
- [x] Mehrere KI-Anbieter (Anthropic/DeepSeek/NVIDIA)
- [x] Web-Suche über selbst-gehostetes SearXNG
- [x] Benachrichtigungsdienst, Nutzungsgewohnheiten, Vollbild-Wecker
- [x] Erster Hardware-Port: **Raspberry Pi 5**
- [x] Stimm-Entsperrung als zweiter Faktor (RMS-Stub, Backend austauschbar)
- [ ] Lokales `whisper.cpp` für den Mikrofon-Knopf (braucht virtuelles
      Audiogerät + gebündeltes Modell)
- [ ] Echtes Modem-Backend (ofono/ModemManager) für SMS/Anruf
- [ ] Echter Compositor (DRM/KMS, GPU-Beschleunigung, Animationen)
- [ ] Portierung auf ein konkretes Telefon-Gerät (Gerätebaum, Touch, Power)
- [ ] Sicherheitsmodell für Drittanbieter-Apps (Sandbox/Permissions)

---

## Sicherheitshinweis (Dev-Build)

Das gebaute Image ist ein **Entwicklungs-/Demo-Build**, kein produktionsreifes
Sicherheitsmodell: Root-Login ohne Passwort auf der seriellen Konsole, keine
App-Sandbox (alles läuft als root). `/etc/flux/flux.conf` (PIN-Hash,
SMTP-Zugangsdaten, API-Key) liegt unverschlüsselt, nur per `chmod 0600`
geschützt — kein Ersatz für einen echten Secret-Store. Vor jedem Schritt
Richtung „echtes Gerät" muss das überarbeitet werden (siehe Roadmap).

---

## Weiterführende Dokumentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — Architektur und Designprinzipien
- [`docs/ICONS.md`](docs/ICONS.md) — Vektor-Icons (Lucide/NanoSVG) und Animationen
- [`docs/RASPBERRY-PI-5.md`](docs/RASPBERRY-PI-5.md) — Pi-5-Installation + Boot-Branding
- [`docs/LOGO-PROMPT.md`](docs/LOGO-PROMPT.md) — Prompt für das Flux-Logo
- [`FEATURES_SPEC.md`](FEATURES_SPEC.md) — detaillierte Feature-Spezifikation
- [`CLAUDE.md`](CLAUDE.md) — Leitfaden für KI-gestützte Entwicklung im Repo
