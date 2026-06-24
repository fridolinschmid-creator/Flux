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
tastatur), Hardware-Tastatur funktioniert weiterhin parallel:

| Lockscreen (Wisch-Hinweis) | Entsperrt per Wisch | Bildschirmtastatur | Lokaler Intent (per Touch) | Ehrlicher Cloud-Hinweis (per Tastatur) |
|---|---|---|---|---|
| ![Lockscreen](docs/screenshots/01-lockscreen.png) | ![Assistent](docs/screenshots/02-assistant.png) | ![Bildschirmtastatur](docs/screenshots/03-touch-keyboard.png) | ![Lokaler Intent](docs/screenshots/04-local-intent.png) | ![Cloud-Hinweis](docs/screenshots/05-cloud-fallback.png) |

("Akku" -> kein Sensor in QEMU vorhanden, ehrlich gemeldet statt erfunden,
hier komplett per Touch-Tastatur eingetippt und abgesendet.
"Wer bist du" -> kein `FLUX_AI_API_KEY` gesetzt, ehrlich gemeldet statt
Absturz oder Fantasieantwort, hier per Hardware-Tastatur gestellt.)

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

### Einstellungen per Sprache aendern (mit Bestaetigung)

Auch Systemeinstellungen lassen sich per natuerlicher Sprache aendern --
**immer ueber denselben Bestaetigungs-Dialog**, nie direkt. Sagt man
"stell die Helligkeit auf 50%", "aktivier den KI-Router", "schalt die
Sprachausgabe aus" oder "setz das Theme auf lila", antwortet `fluxaid`
mit einem `ACTION:setting`-Block (KEY/VALUE/DESC). `flux-shell` zeigt
daraus einen verstaendlichen Dialog ("Helligkeit -> 50%", "KI-Router ->
Ein"); der Wert ist vor dem Uebernehmen antippbar/bearbeitbar. Erst ein
Tap auf "Übernehmen" schickt `X:setting` an `fluxaid`.

**Allowlist (maßgeblich im Daemon, `fluxai/src/exec.c`):** Nur eine fest
definierte Liste sicherer Schluessel ist per Sprache aenderbar --
`brightness` (0-100), `theme` (teal/blau/lila/orange/gruen/rot),
`ai_provider`, `ai_router`, `ai_router_battery`, `tts`, `wakeword`,
`vision_backend` (cloud/local), `auto_lock` (0/30/60/120/300 s) und
`voice_unlock_lock`. Schluessel **und** Wert werden im Daemon validiert
und normalisiert (Booleans als ein/aus, Bereiche geprueft) -- selbst wenn
die KI einen anderen Key vorschlaegt, lehnt der Daemon ihn ab. Die
**PIN** und andere Geheimnisse (Passwoerter, API-Keys) stehen bewusst
**nicht** in der Allowlist und sind nie per Sprache aenderbar (nur ueber
die Einstellungs-Maske mit gehashter Speicherung) -- Sicherheit vor
Bequemlichkeit.

**QEMU-Grenze (ehrlich):** `brightness` wird real ueber
`/sys/class/backlight` gesetzt. Fehlt dieser Knoten (typisch in QEMU
`virt`), meldet der Daemon das wahrheitsgemaess ("kein Backlight-Knoten
gefunden") statt einen Erfolg zu erfinden -- konsistent mit dem
bestehenden `brightness_set`-Verhalten. Reine Config-Keys (Theme,
Router, TTS ...) werden in `/etc/flux/flux.conf` geschrieben und greifen
sofort.

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

## Wake-Word "Hey Flux" -- ehrlicher, andockbarer Stub

Unter **Einstellungen → "Wake-Word (Hey Flux)"** (Config-Key `wakeword`,
Default **aus**) laesst sich ein Wake-Word einschalten. Die Idee: statt den
Mikrofon-Knopf zu druecken, sagt man "Hey Flux" und der Assistent geht in den
Spracheingabe-Modus -- **derselbe** Mechanismus wie der Mikrofon-Knopf, kein
zweiter Pfad.

Das Wake-Word gehoert bewusst zur immer-lauschenden **Shell**-Seite
(`shell/src/wakeword.c`), nicht zum Daemon -- es soll lokal lauschen und den
Assistenten wecken. **EHRLICH:** QEMU `virt` hat kein Mikrofon, also keinen
Audio-Stream, in dem ein Wake-Word gesucht werden koennte. Das Modul bleibt
dort ehrlich inaktiv (die Einstellung zeigt dann "Ein (inaktiv: kein
Mikrofon)") und `flux_wakeword_poll()` liefert **nie** einen Treffer -- kein
Fake-Trigger. Local-first/Privacy: kein Cloud-Wake-Word.

`wakeword.c` ist als **eine austauschbare Datei** geschnitten
(init/poll/deinit). Auf echter Hardware dockt ein on-device-Wake-Word an --
**openWakeWord** (`hey_flux.onnx` via ONNX Runtime) oder **porcupine** --, das
einen kontinuierlichen 16-kHz-Mikrofon-Stream durch ein kleines ONNX-Modell
schickt und bei Aktivierung den Treffer meldet. UI, Settings-Toggle und die
Verdrahtung in `main.c` bleiben dabei unveraendert.

## Lokales TTS (Sprachausgabe) -- ehrlicher, andockbarer Stub

Unter **Einstellungen → "Sprache (TTS)"** (Config-Key `tts`, Default aus)
laesst sich die Sprachausgabe der KI-Antworten einschalten. Ist sie an, wird
nach jeder Assistenten-Antwort `flux_tts_speak()` aufgerufen
(`shell/src/tts.c`).

**EHRLICH:** QEMU `virt` hat **kein Audiogeraet** -- es gibt nichts, worueber
abgespielt werden koennte. `flux_tts_available()` prueft sowohl ein
TTS-Backend (Binary) **als auch** ein echtes Wiedergabe-Geraet (`/dev/snd`);
fehlt eines, bleibt die Ausgabe **lautlos** statt eine Wiedergabe
vorzutaeuschen. Kein stiller Fake-Erfolg, kein Cloud-TTS (local-first).

`tts.c` ist als **eine austauschbare Datei** geschnitten. Bevorzugtes echtes
Backend ist **piper** (lokales neuronales TTS, offline) -- der Andock-Weg auf
echter Hardware mit Audiogeraet ist im Code dokumentiert, z.B.:
```bash
echo "<text>" | piper --model de_DE-thorsten-medium.onnx --output_raw \
              | aplay -r 22050 -f S16_LE -t raw -
```
espeak/flite sind als zusaetzliche (klassische, ebenfalls lokale)
Andock-Optionen mitgesucht. Schnittstelle und Aufrufer bleiben beim
Backend-Tausch unveraendert.

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
│       ├── fb.c/.h          Framebuffer + Double-Buffering
│       ├── stb_easy_font.h  Public-Domain-Bitmapfont (nothings/stb)
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
und sagt ehrlich, dass kein Cloud-Zugang konfiguriert ist.

**KI-Anbieter waehlbar.** Flux unterstuetzt mehrere vorkonfigurierte
Anbieter; der aktive wird in den Einstellungen unter **KI-Anbieter** per
Tipp durchgeschaltet (Anthropic → DeepSeek → NVIDIA → Lokal (llama.cpp)).
API-Key und Modell darunter beziehen sich immer auf den gerade gewaehlten
Anbieter:

| Anbieter | Format | Endpunkt | Standardmodell |
|---|---|---|---|
| Anthropic Claude | Messages API (+ Prompt-Caching) | `api.anthropic.com` | `claude-haiku-4-5-20251001` |
| DeepSeek | OpenAI-kompatibel | `api.deepseek.com` | `deepseek-chat` |
| NVIDIA NIM | OpenAI-kompatibel | `integrate.api.nvidia.com` | `meta/llama-3.1-8b-instruct` |
| Lokal (llama.cpp) | OpenAI-kompatibel | `llamacpp_url` (Default `http://127.0.0.1:8080/v1/chat/completions`) | `local-model` |

Konfiguriert wird ueber die Einstellungen oder direkt in
`/etc/flux/flux.conf`:
```ini
ai_provider=deepseek          # anthropic | deepseek | nvidia | llamacpp
api_key=...                   # Anthropic-Key
deepseek_key=...              # DeepSeek-Key
nvidia_key=...                # NVIDIA-NIM-Key
anthropic_model=...           # optional, sonst Standardmodell
deepseek_model=...            # optional
nvidia_model=...              # optional
llamacpp_url=http://127.0.0.1:8080/v1/chat/completions  # Endpunkt des lokalen Servers
llamacpp_model=local-model    # optional (welches Modell llama-server geladen hat)
ai_router=off                 # off (Default) | on -- Hybrid-Router (s.u.)
ai_router_battery=on          # on (Default) | off -- bei niedrigem Akku lokal (s.u.)
vision_backend=cloud          # cloud (Default) | local -- Bild-KI Cloud/lokaler VLM (s.u.)
vlm_url=http://127.0.0.1:8081/v1/chat/completions  # Endpunkt des lokalen VLM-Servers
vlm_model=moondream           # optional (welches Vision-Modell der VLM-Server geladen hat)
tts=0                         # 0 (Default) | 1 -- KI-Antworten vorlesen (piper-Stub)
wakeword=off                  # off (Default) | on -- Wake-Word "Hey Flux" (openWakeWord-Stub)
```
Alternativ per Umgebungsvariable (`FLUX_AI_API_KEY`, `DEEPSEEK_API_KEY`,
`NVIDIA_API_KEY`, `LLAMACPP_URL`). Bei Rate-Limits (HTTP 429, z.B. NVIDIA)
wiederholt `fluxaid` die Anfrage automatisch mit kurzem Backoff.

#### On-Device-LLM lokal (llama.cpp, offline)

Der Anbieter **Lokal (llama.cpp)** spricht einen selbst gestarteten
`llama-server` an (Teil von [llama.cpp](https://github.com/ggml-org/llama.cpp),
OpenAI-kompatible HTTP-API). Damit laeuft die KI **local-first/offline** --
keine Anfrage verlaesst das Geraet bzw. das lokale Netz, kein Cloud-Key
noetig. Flux bringt llama.cpp **nicht** mit (kein Vendoring); du startest
den Server selbst, z.B.:
```bash
llama-server -m ./modell.gguf --port 8080
```
Danach in den Einstellungen **KI-Anbieter** auf „Lokal (llama.cpp)"
schalten. Den Endpunkt setzt du in `/etc/flux/flux.conf`
(`llamacpp_url=...`) -- dieser Anbieter hat in der Einstellungs-Liste
keinen eigenen URL-Eintrag, nur Modell ist dort editierbar. Ein API-Key
ist nicht noetig: der Anbieter gilt als verfuegbar, sobald eine URL
gesetzt ist. Ist der Server nicht erreichbar, meldet `fluxaid` das
ehrlich („Kein lokaler KI-Server erkannt -- laeuft llama-server...?")
statt eine Antwort zu erfinden.

#### Hybrid-Router (lokal/Cloud)

Optional kann Flux pro Anfrage **selbst entscheiden**, ob sie lokal
(llama.cpp) oder beim konfigurierten Cloud-Anbieter beantwortet wird --
Local-first: einfache, kurze Anfragen bleiben auf dem Geraet, nur die
„harten" gehen in die Cloud. Der Router ist **standardmaessig aus** und
aendert nur dann etwas, wenn er eingeschaltet ist; sonst gilt unveraendert
der fest gewaehlte Anbieter.

Einschalten in den Einstellungen unter **KI-Router (lokal/Cloud)** (Tipp
schaltet Aus/Ein) oder per `/etc/flux/flux.conf`:
```ini
ai_router=on            # off (Default) | on
ai_router_battery=on    # on (Default) | off -- bei niedrigem Akku lokal bevorzugen
ai_router_marker=off    # on -> dezenter Marker "[lokal]"/"[cloud]" vor der Antwort
```

Die Heuristik ist bewusst **simpel und ehrlich** (kein gelerntes Routing,
keine Magie): Eine Anfrage gilt als „hart" (→ Cloud), wenn sie lang ist
(ab ~200 Zeichen) oder Schluesselwoerter wie „code", „programmier",
„analysiere", „ausfuehrlich" enthaelt -- dann ist meist Tiefe oder
Tool-Nutzung noetig. Alles andere geht an den lokalen Server, **sofern er
erreichbar ist** (kurzer Verbindungstest). Ist der lokale Server nicht
erreichbar, faellt der Router ehrlich auf die Cloud zurueck; ist keine
Cloud konfiguriert, bleibt es lokal. Welcher Pfad gewaehlt wurde, steht im
Log (`/var/log/flux/flux.log`, „Router: lokal/Cloud-Pfad"). Lokale Intents
(Uhrzeit, Akku, Datum) werden weiterhin **vor** dem Router ohne Netz
beantwortet.

**Energie-bewusstes Routing (`ai_router_battery`, Default an).** Ist der
Router aktiv und der Akkustand **niedrig (< 20 %)**, bevorzugt der Router
das leichtere **lokale** Modell -- AUCH fuer „harte" Anfragen, sofern ein
lokaler Server erreichbar ist. Das spart Energie und teure
Cloud-Roundtrips. Im Log steht dann „Router: Akkusparmodus (…%) -> lokal";
mit `ai_router_marker=on` wird die Antwort transparent mit
„[lokal: Akkusparmodus]" gekennzeichnet. Schalten laesst sich das in den
Einstellungen unter **KI-Akkusparmodus** oder per `ai_router_battery`.

EHRLICH: Der Akkustand wird ueber denselben sysfs-Knoten gelesen wie die
Akku-Anfrage (`/sys/class/power_supply/.../capacity`). **Gibt es keinen
Akku-Sensor (z.B. QEMU) oder ist der Wert nicht lesbar, ist diese Logik
einfach inaktiv** -- es wird kein Akkustand erfunden und kein Sparmodus
erzwungen; das Routing bleibt dann wie ohne dieses Feature. Ist kein
lokaler Server erreichbar, kann der Sparmodus nicht ausweichen und es geht
normal zur Cloud (Log-Hinweis). In QEMU ist diese Funktion daher praktisch
immer inaktiv und konnte dort **nicht** verifiziert werden.

#### Bildanalyse: Cloud oder lokaler VLM (offline)

Das KI-Tool `image_analyze` („Was ist auf diesem Bild?", „Wo wurde das
aufgenommen?") kann über **zwei austauschbare Backends** laufen -- gewaehlt
über den Config-Key `vision_backend`:

| Wert | Backend | Endpunkt | Modell |
|---|---|---|---|
| `cloud` (Default) | Anthropic Vision (Messages-API) | `api.anthropic.com` | (fest, Cloud) |
| `local` | lokaler VLM-Server (OpenAI-kompatibel, Bild-Input) | `vlm_url` (Default `http://127.0.0.1:8081/v1/chat/completions`) | `vlm_model` (Default `moondream`) |

Bei `local` läuft die Bildanalyse **local-first/offline**: das Bild
verlässt das Gerät bzw. das lokale Netz nicht und es ist kein Cloud-Key
nötig. Wie beim llama.cpp-Textanbieter bringt Flux **kein** VLM mit (kein
Vendoring) -- du startest selbst einen Server, der Bild-Input im
OpenAI-Vision-Format (`image_url` mit `data:`-URI) versteht. Geeignet sind
z.B. kompakte Vision-Modelle wie **Moondream**, **SmolVLM** oder
**MiniCPM-V**, etwa über `llama-server` mit einem Multimodal-Projektor:
```bash
# Beispiel: llama.cpp-Server mit Vision-Modell + Projektor auf Port 8081
llama-server -m ./moondream.gguf --mmproj ./moondream-mmproj.gguf --port 8081
```
Umschalten in den Einstellungen unter **Bild-KI (Cloud/Lokal)** (Tipp
schaltet Cloud/Lokal) oder per `/etc/flux/flux.conf` (`vision_backend`,
`vlm_url`, `vlm_model`; env-Fallback `VLM_URL`). Der Cloud-Pfad bleibt
unverändert der Standard.

**Ehrlich:** Ist der lokale VLM-Server nicht erreichbar, meldet `fluxaid`
das wahrheitsgemäß („Kein lokaler VLM-Server erkannt -- laeuft
moondream/llama-server mit Vision unter <url>?") statt eine Bildbeschreibung
zu erfinden. Der Pfad ist gegen einen **echten** lokalen VLM-Server bisher
**nicht in QEMU verifiziert** (nur Compile-Test); die Andock-Stelle ist im
Code (`fluxai/src/vision.c`, Funktion `vision_local`) klar markiert.

#### Foto eines Dokuments lesen: Kamera-OCR (local-first)

Das KI-Tool `ocr_scan` macht aus einem **Foto eines Dokuments** (Brief,
Vertrag, Bon) wortgetreuen Text, sodass die KI darüber sprechen kann
(„Was ist die Kündigungsfrist?") -- **komplett auf dem Gerät, kein
Cloud-Scandienst**. ARG ist ein Bildpfad (in `/home/user/Pictures` oder
`/home/user/Documents`) oder `letztes` für das zuletzt aufgenommene Foto.
OCR läuft über das **vorhandene lokale VLM** (`vision_backend=local`, mit
OCR-Prompt) oder klassisch über das **`tesseract`-Binary** (rein lokal),
sofern installiert. Ist weder ein lokaler VLM-Server erreichbar noch
tesseract vorhanden, meldet `fluxaid` das **ehrlich** („kein OCR-Backend
verfügbar") statt Text zu erfinden. Der erkannte Text wird zusätzlich nach
`/home/user/Documents/ocr_<Zeitstempel>.txt` geschrieben und ist damit direkt
mit `doc_analyze` verkettbar -- die KI liest die Datei für Detailfragen.

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

### Lokale Suche über alles auf dem Gerät (`semantic_search`)
Das KI-Tool `semantic_search` durchsucht **offline** die festen lokalen
Wissensquellen — Memory (`/etc/flux/memory.txt`), Notizen
(`/etc/flux/notes.txt`), Kalender, Kontakte und das Tages-Journal
(`/home/user/Journal/`) — und gibt der KI die besten passenden Snippets mit
Quelle als RAG-Kontext zurück („such in meinen Notizen nach …", „was weiß mein
Gerät über …"). **Ehrlich:** Das ist **kein** ML-Embedding, sondern eine
lexikalische Overlap-Heuristik (Tokenisierung + Term-Overlap, Top-5). Findet
nichts passt, sagt es das wahrheitsgemäß statt etwas zu erfinden. Kein Netz,
keine Cloud — alles bleibt auf dem Gerät. Ein echtes Embedding-Backend
(z. B. nomic-embed / all-MiniLM via llama-server `/embeddings`) ist als
austauschbare Score-Funktion vorgesehen und dockt ohne UI-Änderung an.

### Personen-/zeitbezogene Erinnerungssuche (`memory_recall`)
Das KI-Tool `memory_recall` durchsucht das KI-Gedächtnis
(`/etc/flux/memory.txt`) gezielt nach **Person** und/oder **Zeitraum** und
beantwortet Fragen wie „Was hat Laura letzte Woche gesagt?". Personen werden
gegen bekannte Namen (Kategorie `PERSON` + Kontakte) abgeglichen, deutsche
Zeitausdrücke (`heute`, `gestern`, `letzte woche`, `letzten montag`,
`diesen monat`, `letzten monat`, `im märz`, `YYYY-MM-DD`) regelbasiert in einen
Datumsbereich aufgelöst, der Rest als lexikalischer Term-Overlap gewertet.
**Ehrlich:** Das ist ein **regelbasierter Entitäts-/Zeit-Index**, kein
Knowledge-Graph und keine Embeddings; nicht erkannte Zeitausdrücke werden
ignoriert statt geraten, und ohne Treffer gibt es eine wahrheitsgemäße Meldung.
Ein echtes Graph-/Embedding-Memory-Backend (MemX/Mem0-artig) dockt an derselben
Stelle an (Personen-Abgleich bzw. Score-Funktion), ohne dass sich Zeitfilter
oder Ausgabe ändern.

### Übersetzung über den aktiven Anbieter (`translate`)
Das KI-Tool `translate` übersetzt Text mit dem Sprachmodell des **aktiven**
KI-Anbieters. Ist der lokale llama.cpp-Anbieter gewählt, läuft die Übersetzung
**offline/local-first**; sonst über die konfigurierte Cloud. ARG-Format:
`<zielsprache>: <text>` (z. B. `englisch: Guten Morgen`) oder
`<text> nach <zielsprache>` (z. B. `Guten Morgen nach englisch`); die
Zielsprache ist auf Deutsch benennbar (englisch/französisch/spanisch/…). Intern
wird der vorhandene Provider-Pfad wiederverwendet (kein zweiter HTTP-Stack), und
zwar als **tool- und kontextfreier** Modell-Aufruf (`flux_provider_complete`),
damit das aus der Agenten-Tool-Schleife heraus aufgerufene Tool keine
Tool-Rekursion auslöst. **Ehrlich:** Ist kein Anbieter nutzbar (kein lokaler
Server, kein Cloud-Key), gibt es eine wahrheitsgemäße Meldung statt einer
erfundenen Übersetzung.

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
   vorhanden) und ein gebuendeltes Modell im Rootfs-Overlay.
   Verwandt (alle drei haengen am selben fehlenden QEMU-Audiogeraet):
   - **Wake-Word "Hey Flux"** (`shell/src/wakeword.c`, Toggle `wakeword`):
     ehrlicher Stub, ohne Mikrofon nie ein Treffer; echtes Backend
     openWakeWord/porcupine (ONNX) dockt ohne UI-Aenderung an. Triggert den
     gleichen Spracheingabe-Pfad wie der Mikrofon-Knopf.
   - **Lokales TTS** (`shell/src/tts.c`, Toggle `tts`): ehrlicher Stub,
     ohne Audiogeraet lautlos; echtes Backend **piper** (neuronal, lokal)
     dockt ohne Aufruf-Aenderung an.
7. Echtes Modem-Backend (ofono/ModemManager) fuer SMS/Anruf auf echter
   Hardware -- ersetzt nur `fluxai/src/telephony.c`, siehe oben
8. ~~Stimm-Erkennung als zweiter Entsperr-Faktor~~ -- `voice_unlock.c/.h`
   (RMS-Fingerabdruck-Stub), `FLUX_SCREEN_VOICE_ENROLL` + `FLUX_SCREEN_VOICE_VERIFY`,
   Einstellungs-Eintrag. QEMU: kein Mikrofon vorhanden, ehrliche Meldung.
   Backend (nur `voice_unlock.c`) durch ECAPA-TDNN ersetzbar ohne UI-Aenderung.
   Zusaetzlich (neu) auch **Entsperren direkt am Lockscreen per Stimme**:
   Mikrofon-Chip "Zum Entsperren sprechen" -- erscheint **nur, wenn eine
   Stimme eingelernt ist, der Toggle "Stimm-Entsperrung am Lockscreen"
   (`voice_unlock_lock`, Default aus) aktiv ist UND keine PIN gesetzt ist**.
   Ist eine PIN gesetzt, bleibt die Stimme ausschliesslich der zweite Faktor
   NACH korrekter PIN -- die PIN ist nie per Stimme umgehbar. Der Wisch nach
   oben bleibt immer als Standardweg sichtbar; kein Mikrofon / keine Stimme
   erkannt fuehrt zurueck zum Lockscreen (nie Lockout), kein Fake-Erfolg.
9. Echter Compositor (DRM/KMS, GPU-Beschleunigung, Animationen, mehrere
   "Karten" statt nur Lockscreen+Assistent)
10. ~~Benachrichtigungen als eigener Systemdienst~~ -- `notification.c/.h`
    in fluxaid als pthread: prueft Kalender/Memory/Batterie/Proaktiv alle
    15 Min, schreibt nach `/tmp/flux_notifications.txt`. Compile-Test bestanden.
11. Portierung auf ein konkretes echtes Geraet (Geraetebaum, Touchscreen-
    Treiber, Akku/Power-Management) — das ist der Schritt, der "Telefon"
    ernst nimmt, siehe postmarketOS-Doku zum Geraete-Porting
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
Referenz-Embedding) als der aktuelle Funktionsumfang.

Inzwischen kann die Stimme **auch direkt am Lockscreen** entsperren -- aber
bewusst nur dort, wo sie keine Schranke schwaecht: nur wenn **keine PIN**
gesetzt ist (dann ist das Geraet ohnehin nicht durch ein Geheimnis geschuetzt
und die Stimme ist reine Bequemlichkeit, kein Sicherheitsgewinn). Sobald eine
PIN existiert, verschwindet der Sprechen-Weg vom Lockscreen vollstaendig und
die Stimme bleibt **zweiter Faktor nach der PIN** -- die PIN ist nie per
Stimme allein umgehbar. Ein Mikrofonausfall oder eine nicht erkannte Stimme
fuehrt nie zum Lockout: der Wisch nach oben bleibt immer der Standardweg, und
der RMS-Stub meldet "kein Mikrofon erkannt" ehrlich, statt Erfolg vorzutaeuschen.
Das echte Sprecher-Embedding (ECAPA-TDNN/x-vector) bleibt der austauschbare
Backend-Teil in `voice_unlock.c` -- die UI- und Entsperr-Logik aendern sich
dadurch nicht.

## Sicherheits-Hinweis (Dev-Build)
Das gebaute Image hat einen Root-Login ohne Passwort auf der seriellen
Konsole und keine App-Sandbox -- das ist ein bewusster Kompromiss fuer
einen Entwicklungs-/Demo-Build, kein produktionsreifes Sicherheitsmodell.
`/etc/flux/flux.conf` (PIN-Hash, SMTP-Zugangsdaten, Cloud-API-Key) liegt
unverschluesselt, nur per `chmod 0600` geschuetzt -- kein Ersatz fuer
einen echten Secret-Store. Vor jedem Schritt in Richtung "echtes Geraet"
muss das ueberarbeitet werden.
