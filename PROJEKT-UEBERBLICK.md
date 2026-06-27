# Flux – Projekt-Überblick

> Diese Datei fasst kompakt zusammen, was du über dein Projekt wissen
> solltest: was Flux ist, welche „Add-ins" (KI, Mail, Web-Suche …) es nutzt,
> **von wem** diese bereitgestellt werden und worauf du achten musst.
> Ausführliche Details stehen in [`README.md`](README.md) und [`docs/`](docs).

---

## 1. Was ist Flux?

Flux ist ein **mobiles Betriebssystem, das auf KI statt auf einem App-Grid**
aufbaut. Es gibt keinen App-Drawer mit Icons – der **KI-Assistent ist der
Homescreen**. Man entsperrt das Gerät (optional per PIN) und fragt direkt in
natürlicher Sprache, statt nach Apps zu suchen.

- **Eigenentwicklung:** die komplette UI- und KI-Schicht oberhalb des Kernels.
- **Nicht eigenentwickelt (bewusst):** der **Linux-Kernel** (via Buildroot) –
  liefert Treiber, Speicherverwaltung, Multitasking. Genau wie Android, postmarketOS etc. darauf aufbauen.
- **Läuft aktuell auf:** QEMU `aarch64 virt` (virtuell) und als erster echter
  Geräte-Port auf dem **Raspberry Pi 5** (arm64).
- **Sprache:** C (Shell + Daemon) – kein Electron, kein JS, kein Garbage Collector → bewusst ressourcenschonend und flüssig.

### Zwei Hauptkomponenten

| Komponente | Was sie tut | Code |
|---|---|---|
| **flux-shell** | Die UI. Zeichnet direkt auf den Framebuffer (`/dev/fb0`), Double-Buffering. Lockscreen → (PIN) → Assistent → Einstellungen/Dateien/Bestätigungs-Dialoge. | `shell/src/` |
| **fluxaid** | Der System-KI-Daemon. Beantwortet Fragen, ruft KI-Anbieter auf, führt bestätigte Aktionen aus. Läuft als Hintergrunddienst, kein App-Prozess. | `fluxai/src/` |

Beide reden über einen Unix-Socket (`/run/flux/fluxai.sock`) miteinander:
`Q:<frage>` für Anfragen, `X:<aktion>` für bestätigte Aktionen.

---

## 2. Die KI – und von wem sie bereitgestellt wird

Das ist der wichtigste Punkt: **Flux selbst betreibt kein eigenes KI-Modell.**
Es nutzt eine zweistufige Strategie:

### Stufe 1 – Lokale Intents (kein Internet, kostenlos, privat)
Einfache Fragen werden direkt im Daemon beantwortet, ohne dass Daten das Gerät
verlassen: Uhrzeit, Datum, Akkustand, Uptime, Rechnen usw.
(`fluxai/src/actions.c`). Schnell und privacy-freundlich.

### Stufe 2 – Cloud-KI-Anbieter (nur wenn nötig, braucht API-Key)
Reichen die lokalen Intents nicht, fragt `fluxaid` einen **externen
KI-Anbieter** an. **Du wählst den Anbieter selbst** (Einstellungen →
„KI-Anbieter", durchschaltbar) und musst einen **eigenen API-Schlüssel**
hinterlegen. Drei Anbieter sind vorkonfiguriert:

| Anbieter | Bereitgestellt von | Endpunkt | Standardmodell | API-Format |
|---|---|---|---|---|
| **Anthropic Claude** (Standard) | Anthropic | `api.anthropic.com` | `claude-haiku-4-5-20251001` | Messages API (+ Prompt-Caching) |
| **DeepSeek** | DeepSeek | `api.deepseek.com` | `deepseek-chat` | OpenAI-kompatibel |
| **NVIDIA NIM** | NVIDIA | `integrate.api.nvidia.com` | `meta/llama-3.1-8b-instruct` | OpenAI-kompatibel |

Quelle im Code: `fluxai/src/provider.c` (Liste `PROVIDERS[]`).

**Wichtig für dich:**
- **Ohne API-Key** beantwortet Flux nur lokale Fragen und sagt das ehrlich –
  es wird nichts erfunden und nichts stürzt ab.
- Der API-Key **kostet dich Geld bzw. unterliegt den Konditionen des jeweiligen
  Anbieters** (Anthropic / DeepSeek / NVIDIA). Flux ist nur der Client.
- Bei der Cloud-Nutzung **verlässt deine Anfrage das Gerät** und geht an den
  gewählten Anbieter – das ist der Unterschied zu Stufe 1.
- Schlüssel werden in `/etc/flux/flux.conf` gespeichert (`chmod 0600`) oder per
  Umgebungsvariable (`FLUX_AI_API_KEY`, `DEEPSEEK_API_KEY`, `NVIDIA_API_KEY`).

### Was die KI alles kann (Tools / Funktionen)
Die KI hat Zugriff auf zahlreiche Geräte-Tools (`fluxai/src/tools.c`), u.a.:
Kalender (`calendar_add/list`), Kontakte, Notizen, Erinnerungen/Memory,
Mail lesen & senden (`mail_unread/read`), Dateien, Wecker (`alarm_set`),
Helligkeit, Web-Suche (`web_search`), Bild-/Dokument-Analyse
(`image_analyze`, `doc_analyze`), Journal, Geburtstage u.v.m.

---

## 3. Die weiteren Add-ins / externen Dienste

| Add-in | Wofür | Bereitgestellt von / Backend | Status |
|---|---|---|---|
| **KI-Cloud** | Antworten jenseits lokaler Intents | Anthropic / DeepSeek / NVIDIA (du wählst) | **funktioniert**, braucht deinen API-Key |
| **E-Mail senden** | Mail per SMTP | Dein Mail-Anbieter (Gmail, iCloud, Outlook, GMX …) | **funktioniert** (libcurl, `mail.c`) |
| **E-Mail lesen** | Ungelesene Mails abrufen/zusammenfassen | Dein Mail-Anbieter via IMAP | **funktioniert** (`imap.c`) |
| **Web-Suche** | Aktuelle Infos aus dem Netz | **Eigene, selbst gehostete SearXNG-Instanz** (z.B. auf deinem MacBook) | **funktioniert**, du hostest selbst |
| **Icons** | Symbole der UI | **Lucide** (MIT) gerastert via **NanoSVG** (zlib) | eingebaut |
| **Schrift** | UI-Schrift | **Instrument Sans** (TrueType) | eingebaut |
| **SMS / Anruf** | Telefonie | Mobilfunk-Modem (ofono/ModemManager auf echter HW) | **ehrlicher Stub** – QEMU hat kein Modem |
| **Mikrofon / Spracheingabe** | Sprache → Text | Geplant: lokales `whisper.cpp` (kein Cloud) | **Platzhalter** – noch kein Audiogerät |
| **Stimm-Entsperrung** | 2. Faktor neben PIN | Geplant: lokales ECAPA-TDNN | **Stub** |

**E-Mail-Setup ist bequem:** In den Einstellungen nur **Adresse + App-Passwort**
eingeben – SMTP/IMAP-Server werden automatisch aus der Domain abgeleitet
(Gmail, Outlook/Hotmail, iCloud, Yahoo, GMX, web.de, t-online, posteo,
mailbox.org).

---

## 4. Wichtige Designprinzipien (warum Flux so gebaut ist)

1. **KI führt nie etwas direkt aus.** Bei Mail/SMS/Anruf erscheint **immer**
   erst ein Apple-artiger Bestätigungs-Dialog (Senden / Abbrechen / Bearbeiten).
   Erst dein Tap auf „Senden" löst die Aktion aus.
2. **Ehrlichkeit statt Fake.** Fehlt Hardware (Akku-Sensor, Modem, Mikrofon)
   oder ein API-Key, sagt Flux das klar – es erfindet keine Antworten und
   stürzt nicht ab.
3. **Privacy by construction.** Lokale Intents verlassen das Gerät nie. Web-Suche
   läuft über deine eigene SearXNG-Instanz (kein fremder Such-API-Key, keine
   Profilbildung).
4. **Performance.** C statt VM-Stacks, Double-Buffering + Dirty-Row-Tracking,
   nur geänderte UI-Teile werden neu gezeichnet.

---

## 5. Bauen & Starten (Kurzfassung)

```bash
# Schnelltest auf dem Host (nur Daemon, ohne Display)
cd fluxai && make && ./fluxaid &

# Vollständiges QEMU-Image (Kernel + Rootfs, dauert beim 1. Mal lange)
./build/build.sh
./build/run-qemu.sh

# Echter Raspberry Pi 5
./build/build-pi5.sh
./build/flash-pi5.sh /dev/sdX   # Gerät GENAU prüfen!
```

Konfiguration zentral in `/etc/flux/flux.conf` (PIN-Hash, API-Keys,
Mail-Zugang, SearXNG-URL).

---

## 6. Sicherheits-Hinweis (unbedingt wissen!)

Das gebaute Image ist ein **Entwicklungs-/Demo-Build**, **nicht** produktionsreif:

- **Root-Login ohne Passwort** auf der seriellen Konsole.
- **Keine App-Sandbox** – alles läuft als root.
- `/etc/flux/flux.conf` enthält **PIN-Hash, Mail-Passwort und API-Keys
  unverschlüsselt** (nur `chmod 0600`) – kein echter Secret-Store.

→ Vor jedem Schritt Richtung „echtes Alltagsgerät" muss das überarbeitet werden.

---

## 7. Wo finde ich was?

| Datei / Ordner | Inhalt |
|---|---|
| [`README.md`](README.md) | Ausführliche Projektbeschreibung, Screenshots, Setup |
| [`FEATURES_SPEC.md`](FEATURES_SPEC.md) | Feature-Spezifikation |
| [`FLUX-KI-AUSBAU-PROMPT.md`](FLUX-KI-AUSBAU-PROMPT.md) | Prompt/Plan zum KI-Ausbau |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Architektur im Detail |
| [`docs/ICONS.md`](docs/ICONS.md) | Vektor-Icons (Lucide/NanoSVG) |
| [`docs/RASPBERRY-PI-5.md`](docs/RASPBERRY-PI-5.md) | Pi-5-Installation & Branding |
| `shell/src/` | UI-Code (flux-shell) |
| `fluxai/src/` | KI-Daemon (fluxaid), inkl. `provider.c` (Anbieter), `tools.c` (KI-Tools) |
| `common/` | Gemeinsames Protokoll, Config, PIN-Hashing |

---

*Stand: 2026-06-27 · Diese Übersicht wurde aus dem aktuellen Stand von Code
und README abgeleitet.*
