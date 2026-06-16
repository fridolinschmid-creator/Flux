# Flux

Ein mobiles Betriebssystem, das auf KI statt auf einem App-Grid aufbaut.
Kein App-Drawer mit hunderten Icons — der KI-Assistent **ist** der
Homescreen. Man entsperrt das Geraet und fragt direkt, statt zu suchen.

## Verifiziert: echter Boot in QEMU (aarch64), touch-first

Kein Mockup -- das ist ein echter Linux/ARM64-Kernel (Buildroot-gebaut),
der in QEMU bootet, bei dem `flux-shell` auf den von `virtio-gpu`
bereitgestellten Framebuffer zeichnet und `fluxaid` ueber den
Unix-Socket antwortet. Bedienung komplett ohne Tastatur moeglich --
per `virtio-tablet` simuliertem Touch (Wisch-Geste + Bildschirm-
tastatur), Hardware-Tastatur funktioniert weiterhin parallel:

| Lockscreen (Wisch-Hinweis) | Entsperrt per Wisch | Bildschirmtastatur | Lokaler Intent (per Touch) | Ehrlicher Cloud-Hinweis (per Tastatur) |
|---|---|---|---|---|
| ![Lockscreen](docs/screenshots/01-lockscreen.png) | ![Assistent](docs/screenshots/02-assistant.png) | ![Bildschirmtastatur](docs/screenshots/03-touch-keyboard.png) | ![Lokaler Intent](docs/screenshots/04-local-intent.png) | ![Cloud-Hinweis](docs/screenshots/05-cloud-fallback.png) |

("Akku" -> kein Sensor in QEMU vorhanden, ehrlich gemeldet statt erfunden,
hier komplett per Touch-Tastatur eingetippt und abgesendet.
"Wer bist du" -> kein `FLUX_AI_API_KEY` gesetzt, ehrlich gemeldet statt
Absturz oder Fantasieantwort, hier per Hardware-Tastatur gestellt.)

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

## Architektur

```
┌─────────────────────────────────────────────┐
│  flux-shell (eigene UI, kein App-Grid)       │  <- zeichnet direkt auf
│  Lockscreen -> KI-Assistent als Homescreen   │     /dev/fb0 (Framebuffer,
└───────────────────┬───────────────────────────┘     Double-Buffering)
                    │ Unix-Socket (/run/flux/fluxai.sock)
┌───────────────────┴───────────────────────────┐
│  fluxaid (System-KI-Daemon, kein App-Prozess)│  <- lokale Intents zuerst
│  - lokale Intents: Uhrzeit, Akku, Uptime ...  │     (Geschwindigkeit +
│  - Cloud-Fallback ueber eigenen API-Key       │     Privacy), Cloud nur
└───────────────────┬───────────────────────────┘     wenn wirklich noetig
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
├── shell/              flux-shell -- Framebuffer-UI (Lockscreen, Assistent)
│   └── src/
│       ├── fb.c/.h          Framebuffer + Double-Buffering
│       ├── stb_easy_font.h  Public-Domain-Bitmapfont (nothings/stb)
│       ├── input.c/.h       Tastatur/Touch ueber Linux evdev
│       ├── ipc.c/.h         Client fuer fluxaid
│       ├── ui.c/.h          Lockscreen + Assistenten-Bildschirm
│       └── main.c           Event-Loop
├── fluxai/              fluxaid -- System-KI-Daemon
│   └── src/
│       ├── actions.c/.h     Lokale Geraete-Intents (Zeit, Akku, Uptime)
│       ├── provider.c/.h    Cloud-Fallback (Anthropic API, eigener Key)
│       └── main.c           Unix-Socket-Server
├── common/
│   └── flux_protocol.h      Mini-Protokoll Shell <-> Daemon
├── build/
│   ├── overlay/              Rootfs-Overlay (eigenes /etc/inittab, Binaries)
│   └── build.sh              Buildroot-Build + Cross-Compile in einem Schritt
└── docs/
    └── ARCHITECTURE.md
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

### Eigenen KI-Zugang einrichten (optional)
Ohne API-Key beantwortet `fluxaid` nur lokale Fragen (Zeit, Akku, Uptime)
und sagt ehrlich, dass kein Cloud-Zugang konfiguriert ist. Mit eigenem
Anthropic-Key:
```bash
export FLUX_AI_API_KEY="dein-key"
export FLUX_AI_MODEL="claude-haiku-4-5-20251001"   # optional, das ist der Default
```

---

## Roadmap

1. ~~Framebuffer-UI mit Double-Buffering~~
2. ~~System-KI-Daemon mit lokalen Intents + Cloud-Fallback~~
3. ~~Bootbares aarch64-Image (Buildroot, QEMU `virt`)~~
4. ~~Touch-Input statt nur Tastatur~~ -- Wisch-Geste zum Entsperren,
   Bildschirmtastatur fuer den Assistenten, Hardware-Tastatur bleibt
   nebenbei nutzbar (`shell/src/input.c`, `shell/src/ui.c`)
5. Echter Compositor (DRM/KMS, GPU-Beschleunigung, Animationen, mehrere
   "Karten" statt nur Lockscreen+Assistent)
6. Benachrichtigungen als eigener Systemdienst (nicht App-spezifisch)
7. Portierung auf ein konkretes echtes Geraet (Geraetebaum, Touchscreen-
   Treiber, Akku/Power-Management) — das ist der Schritt, der "Telefon"
   ernst nimmt, siehe postmarketOS-Doku zum Geraete-Porting
8. Sicherheitsmodell fuer Drittanbieter-Apps (Sandbox/Permissions) --
   aktuell laeuft alles als root, das ist fuer einen Dev-Build okay, fuer
   ein echtes Telefon-Betriebssystem nicht

## Sicherheits-Hinweis (Dev-Build)
Das gebaute Image hat einen Root-Login ohne Passwort auf der seriellen
Konsole und keine App-Sandbox -- das ist ein bewusster Kompromiss fuer
einen Entwicklungs-/Demo-Build, kein produktionsreifes Sicherheitsmodell.
Vor jedem Schritt in Richtung "echtes Geraet" muss das ueberarbeitet
werden.
