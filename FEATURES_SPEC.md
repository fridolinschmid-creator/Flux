# Flux OS — Feature-Spezifikation: Batch 4
## Briefing für neue Implementierungs-Sessions

---

## 1. Was ist Flux OS?

Flux ist ein KI-zentriertes mobiles Betriebssystem für ARM64-Linux-Geräte (480×854px Framebuffer).
Es gibt **keinen App-Grid, keinen Homescreen mit Icons**. Der KI-Assistent IS der Homescreen.

**Entsperren → KI-Assistent. Fertig.**

Das Gerät zeichnet direkt auf `/dev/fb0`. Touch-Eingabe über Linux evdev. Die KI (fluxaid-Daemon)
läuft als separater Prozess, die Shell kommuniziert per Unix-Socket IPC.

### Aktuell implementierte Bildschirme (bereits fertig):
- Lockscreen mit Uhrzeit, Datum, KI-Begrüßung, Wetter
- PIN-Sperre (optional)
- KI-Assistent (Homescreen) mit Chatblasen, Tastatur, Mikrofon-Button
- Schnellzugriff-Leiste: Einstellungen | Dateien | Kalender | Kontakte
- Bestätigungs-Dialog (KI will Mail/SMS/Anruf auslösen)
- Text-Editor (vor dem Senden)
- Einstellungen (PIN, SMTP, API-Key, Theme, Auto-Lock, TTS)
- Datei-Browser mit Neu-Ordner und Löschen
- Datei-Betrachter (Text-Dateien)
- Benachrichtigungs-Overlay (Wisch nach unten)
- Kalender (Monatsansicht, Termine, Navigation)
- Kontakte (CSV-basiert, KI kann Kontakte speichern)
- Fotogalerie (PPM-Fotos, Kamera-Button)
- Bild-Betrachter (skaliert, KI-Analyse-Button, Löschen)

### KI-Tools die der Assistent bereits nutzen kann:
date_time, weather, file_read, file_list, file_create, file_delete, calculate,
note_save, note_list, sys_info, alarm_set, reminder_set, contacts_search,
brightness_get, brightness_set, wifi_info, vibrate, contact_save, contacts_list,
calendar_add, calendar_list, search_files, prefs_set, image_list, image_analyze, image_take

---

## 2. Unveränderliche Design-Regeln

Diese Regeln gelten für JEDEN neuen Bildschirm. Niemals davon abweichen.

### Farben (definiert in shell/src/ui.c):
```c
COL_BG        = 0x0B0E14   // Fast-Schwarz, Hintergrund
COL_ACCENT    = g_accent   // Dynamisch, Standard: 0x4FD1C5 (Teal)
COL_TEXT      = 0xE6E6E6   // Helles Grau, Haupttext
COL_DIM       = 0x6B7280   // Gedimmtes Grau, Nebentext
COL_STATUSBAR = 0x161B26   // Etwas heller als BG, Statusleiste
COL_ROW       = 0x1A2230   // Listenelement-Hintergrund
COL_DANGER    = 0xE05252   // Rot für Löschen/Fehler
COL_CARD      = 0x1C2840   // Karten-Hintergrund
```

### Layout-Konstanten:
```c
STATUSBAR_H = 40    // Oben: Uhrzeit, Batterie, WLAN
QUICKROW_H  = 56    // Schnellzugriff-Leiste (nur Assistent)
TITLE_AREA_H= 64    // Titelzeile auf Sekundär-Screens
LIST_BACK_H = 64    // "Zurück"-Leiste unten
```

### Animationen (IMMER verwenden bei Screen-Wechseln):
- **Vorwärts**: `animate_slide_in()` — neuer Screen gleitet von unten herein mit Spring-Overshoot
- **Zurück**: `animate_slide_from_left()` — Screen kommt von links
- **Tap-Feedback**: `animate_ripple()` — expandierender Ring beim Antippen
- **Wisch nach unten** öffnet immer den Benachrichtigungs-Overlay

### Architektur-Regeln:
- Neue Bildschirme als `FLUX_SCREEN_XYZ` in `shell/src/ui.h` enum
- Render-Funktion `flux_ui_draw_xyz()` in `shell/src/ui.c`
- Hit-Test-Funktion `flux_ui_xyz_hit()` in `shell/src/ui.c`
- Event-Loop-Handler in `shell/src/main.c`
- Neue KI-Tools in `fluxai/src/tools.c` (dispatch + description)
- Neue Dateien in `src/` werden automatisch kompiliert (wildcard Makefile)
- Alle Texte auf DEUTSCH (UI-Sprache ist Deutsch)

### Bildschirm-Struktur (Standard):
```
[Statusleiste 40px — Uhrzeit, WLAN, Batterie]
[Titelzeile 64px — Screen-Name links, evtl. Button rechts]
[Inhalt — scrollbar oder statisch]
[Zurück-Leiste 64px — zentrierter Text in COL_ACCENT]
```

---

## 3. Feature 1: AI Memory — Dauerhaftes KI-Gedächtnis

### Was der Nutzer erlebt:

Die KI vergisst nie etwas das ihr gesagt wurde. Sie verhält sich wie ein persönlicher
Assistent der sich an alle wichtigen Details erinnert — ohne dass der Nutzer etwas
manuell speichern muss.

**Beispiel-Gespräche:**
```
Nutzer: "Max mag keine Meetings vor 10 Uhr"
KI:     "Notiert. Ich werde das bei Terminvorschlägen berücksichtigen."

[Zwei Wochen später:]
Nutzer: "Mach einen Termin mit Max für morgen"
KI:     "Ich schlage 10:30 Uhr vor — Max bevorzugt keine frühen Termine."
```

```
Nutzer: "Meine Frau heißt Laura und hat am 15. März Geburtstag"
KI:     "Notiert. Ich erinnere dich rechtzeitig."

[Am 14. März, proaktiv:]
KI:     "Morgen hat Laura Geburtstag. Soll ich dir helfen eine Nachricht zu schreiben?"
```

```
Nutzer: "Ich bin allergisch gegen Nüsse"
KI:     "Gespeichert in deinem Profil."

[Bei Restaurantsuche:]
KI:     "Achtung: Das Restaurant bietet viele Gerichte mit Nüssen an."
```

### Wie es im System funktioniert:

**Speicherort:** `/etc/flux/memory.txt`
Format: `[YYYY-MM-DD HH:MM] KATEGORIE: Inhalt`

Kategorien: PERSON, PRAEFERENZ, FAKT, TERMIN, ALLERGIE, WICHTIG

**Neues KI-Tool:** `memory_save` — speichert eine Erinnerung
**Neues KI-Tool:** `memory_list` — listet alle Erinnerungen
**Neues KI-Tool:** `memory_search` — sucht in Erinnerungen (ARG: Suchbegriff)
**Neues KI-Tool:** `memory_delete` — löscht eine Erinnerung (ARG: Zeilennummer)

**Wichtig:** Der System-Prompt in `fluxai/src/provider.c` lädt beim Start automatisch
die letzten 20 Einträge aus memory.txt und fügt sie als Kontext ein — so "weiß" die
KI alles ohne extra Tool-Aufruf für häufige Informationen.

**Neuer Bildschirm:** `FLUX_SCREEN_MEMORY`
- Erreichbar über Assistent: Tipp "erinnerungen" oder "memory"
- Zeigt alle gespeicherten Erinnerungen als Liste
- Tap auf Eintrag → löschen mit Bestätigung
- Kategorien farblich markiert (PERSON=Teal, WICHTIG=Gelb, ALLERGIE=Rot)

---

## 4. Feature 2: Proaktive KI — Die KI meldet sich von selbst

### Was der Nutzer erlebt:

Die KI wartet nicht auf Fragen. Sie analysiert regelmäßig Kontext (Kalender, Wetter,
Uhrzeit, Memory) und zeigt proaktive Hinweise direkt auf dem Lockscreen oder als
Push-Benachrichtigung.

**Beispiele was die KI von sich aus meldet:**

```
[Morgens 7:30 Uhr, Lockscreen:]
"Heute 14 Uhr Arzttermin. Losfahren um 13:30 — heute regnet es."

[Freitagabends:]
"Du hast diese Woche 3 ungelesene E-Mails von Max. Soll ich sie zusammenfassen?"

[Tag vor Geburtstag aus Memory:]
"Morgen hat Laura Geburtstag (aus deinen Notizen). Glückwunsch-Nachricht schreiben?"

[Wenn Batterie unter 20%:]
"Batterie bei 18%. Soll ich den Energiesparmodus aktivieren?"
```

### Wie es im System funktioniert:

**Hintergrund-Prozess:** `fluxai/src/proactive.c` + `proactive.h`
- Läuft als separater Thread im fluxaid-Daemon
- Prüft alle 15 Minuten: Kalender, Memory, Wetter, Batterie, Uhrzeit
- Wenn Bedingung erfüllt → schreibt Nachricht nach `/tmp/flux_proactive.txt`
- Schreibt nur 1 Nachricht gleichzeitig (nicht spammen)

**Bedingungen die geprüft werden:**
1. Kalender: Termin in den nächsten 60 Minuten → Hinweis + Fahrtzeit
2. Memory: Geburtstag morgen → Erinnerung
3. Memory: Regelmäßige Aufgaben → Hinweis
4. Batterie < 20% → Warnung
5. Ungelesene wichtige Nachrichten (aus Mail-Tool) → Zusammenfassung
6. Wetter-Verschlechterung für heute → Warnung

**Lockscreen-Integration (`shell/src/ui.c` → `flux_ui_draw_lock()`):**
- Liest `/tmp/flux_proactive.txt`
- Zeigt Hinweis in einem farbigen Banner unter der Uhrzeit
- Tap auf Banner → öffnet direkt den Assistenten mit Kontext vorausgefüllt

**Assistent-Integration:**
- Wenn proactive.txt existiert → beim Öffnen des Assistenten wird der Hinweis
  als KI-Antwort angezeigt (als ob die KI ihn schon formuliert hätte)

---

## 5. Feature 3: AI Journal — Automatisches Tagestagbuch

### Was der Nutzer erlebt:

Jeden Abend (22:00 Uhr) generiert Flux automatisch eine Zusammenfassung des Tages.
Der Nutzer muss nichts tun — das Gerät beobachtet still mit und schreibt die Geschichte
des Tages auf. Fotos werden automatisch eingebunden.

**Beispiel Journal-Eintrag:**
```
═══════════════════════════════════════
Donnerstag, 18. Juni 2026
═══════════════════════════════════════

Heute war ein produktiver Tag. Am Morgen hast du 2 E-Mails an Max
gesendet — darunter die Projektunterlagen die er angefordert hatte.
Um 14:00 Uhr war der Arzttermin (aus dem Kalender).

Am Nachmittag hast du 3 Fotos aufgenommen: zwei davon in der Stadt,
eines zeigt einen Park mit Rosenbüschen (Rosa canina, lt. Pflanzen-KI).

Du hast heute 2 Fragen an die KI gestellt, darunter eine Anfrage
zum Wetter und eine Berechnung.

Morgen: Keine Termine eingetragen.
───────────────────────────────────────
```

### Wie es im System funktioniert:

**Datenquellen die ausgewertet werden:**
- Kalender (`/etc/flux/calendar.txt`) — heutige Termine
- Fotos (`/home/user/Pictures/`) — heute aufgenommene Fotos + deren Captions
- Mail-Verlauf (aus fluxaid exec-Log)
- KI-Gesprächsverlauf des Tages (temporär gecacht)
- Memory-Einträge die heute hinzugefügt wurden
- Wetter des Tages (aus `/tmp/flux_weather.txt`)

**Generierung:**
- Täglich um 22:00 Uhr (via fluxaid-internem Timer-Thread)
- KI-Aufruf: alle Daten werden als Kontext übergeben, KI schreibt den Eintrag
- Gespeichert unter: `/home/user/Journal/YYYY-MM-DD.txt`
- Verzeichnis wird automatisch erstellt

**Neues KI-Tool:** `journal_list` — listet alle Journal-Einträge
**Neues KI-Tool:** `journal_read` — liest einen Eintrag (ARG: YYYY-MM-DD oder "heute"/"gestern")

**Neuer Bildschirm:** `FLUX_SCREEN_JOURNAL`
- Erreichbar: Tipp "journal" oder "tagebuch"
- Liste aller Einträge mit Datum
- Tap → öffnet Eintrag im Datei-Betrachter (bestehende `flux_ui_draw_file_viewer()`)
- Einträge können nicht gelöscht werden (Schutz vor versehentlichem Löschen)
- Optional: Suche nach Datum oder Stichwort

**Erster Eintrag:**
Wenn noch kein Journal existiert und der Nutzer "journal" tippt →
KI generiert sofort einen Eintrag für heute (nicht warten bis 22 Uhr)

---

## 6. Feature 4: Dokument-KI — Intelligenter Dokumenten-Assistent

### Was der Nutzer erlebt:

Der Nutzer kann jede Text-Datei (TXT, MD, auch lange Dokumente) öffnen und direkt
mit der KI darüber sprechen — ohne Copy-Paste, ohne Cloud. Die KI liest das Dokument
und beantwortet Fragen dazu.

**Beispiel-Nutzung:**

```
[Nutzer öffnet "Mietvertrag_2026.txt" im Datei-Browser]
[Datei-Betrachter zeigt den Text]
[Neuer Button: "KI fragen"]

Nutzer tippt: "Wie lange ist die Kündigungsfrist?"
KI: "Laut Paragraph 4 des Dokuments beträgt die Kündigungsfrist
     3 Monate zum Quartalsende."

Nutzer: "Fasse die wichtigsten Punkte zusammen"
KI: "Das Dokument enthält folgende Kernpunkte:
     1. Mietbeginn: 01.07.2026
     2. Kaltmiete: 950€ monatlich
     3. Kündigungsfrist: 3 Monate zum Quartalsende
     4. Nebenkosten-Abrechnung: jährlich"

Nutzer: "Gibt es irgendwas Ungewöhnliches?"
KI: "Paragraph 8 enthält eine unübliche Klausel: der Vermieter
     darf die Wohnung mit 24h Vorankündigung betreten. Das ist
     rechtlich fragwürdig — normalerweise sind 48h Standard."
```

**Weitere Anwendungsfälle:**
- Rezept lesen → "Was kann ich weglassen wenn ich kein Mehl habe?"
- Technische Anleitung → "Erkläre Schritt 3 einfacher"
- Notizen → "Was habe ich letzte Woche zu Max geschrieben?"
- Code-Datei → "Was macht diese Funktion?"

### Wie es im System funktioniert:

**Integration in bestehenden Datei-Betrachter** (`FLUX_SCREEN_FILE_VIEWER`):
- Neuer Button in der unteren Leiste: "KI fragen" (neben "Zurück")
- Tap auf "KI fragen" → öffnet einen Chat-Dialog über dem Betrachter
- Der Dateiinhalt wird als Kontext an die KI übergeben

**Technisch:**
- Dateiinhalt (max. 8.000 Zeichen, Rest abschneiden mit Hinweis) wird in den
  System-Prompt eingefügt: `"Aktuelle Datei: [DATEINAME]\n[INHALT]"`
- Separater Gesprächs-Kontext pro Datei (nicht mit normalem Assistenten-Verlauf mischen)
- Antwort wird als Chat-Blase im Overlay angezeigt

**Neues KI-Tool:** `doc_analyze` — analysiert eine Datei und gibt Zusammenfassung
  ARG: Dateipfad (für den Fall dass die KI selbst eine Datei analysieren soll)

**UI-Erweiterung in `flux_ui_draw_file_viewer()`:**
- Untere Leiste: [Zurück] [KI fragen] statt nur [Zurück]
- Wenn "KI fragen" gedrückt → `FLUX_SCREEN_DOC_AI` Overlay
- Overlay: halber Bildschirm (untere Hälfte), dunkel-transparent
  - Kleine Eingabezeile unten
  - KI-Antwort darüber (scrollbar)
  - Dokument bleibt oben sichtbar

---

## 7. Feature 5: Meeting-Mitschrift — Automatisches Protokoll

### Was der Nutzer erlebt:

Der Nutzer hält sein Gerät während eines Meetings hin. Flux hört zu, transkribiert
und generiert am Ende ein strukturiertes Protokoll — Teilnehmer, Themen,
Beschlüsse, Aufgaben.

**Ablauf aus Nutzersicht:**
```
[Nutzer tippt "meeting" oder "aufnahme"]

Bildschirm: MEETING-AUFNAHME
┌─────────────────────────────────┐
│  ● AUFNAHME LÄUFT               │
│  00:12:34                       │
│                                 │
│  [Live-Transkript scrollt...]   │
│  "...dann müssen wir bis        │
│   Freitag die Unterlagen..."    │
│                                 │
│  [STOPP & PROTOKOLL ERSTELLEN]  │
└─────────────────────────────────┘

[Nach Stopp — KI analysiert ~10-30 Sekunden:]

Meeting-Protokoll — 18.06.2026, 14:00 Uhr
Dauer: 23 Minuten

THEMEN:
• Projektstand Q3-Bericht
• Budget-Freigabe für Kampagne
• Onboarding neuer Mitarbeiter

BESCHLÜSSE:
✓ Budget von 15.000€ genehmigt
✓ Launch-Termin: 1. Juli 2026
✓ Max übernimmt Onboarding

AUFGABEN:
□ Max: Unterlagen bis Freitag fertigstellen
□ Anna: Design-Entwurf bis Montag
□ Du: Meeting mit Geschäftsführung vereinbaren

[SPEICHERN] [TEILEN PER MAIL] [ZURÜCK]
```

### Wie es im System funktioniert:

**Audio-Aufnahme:**
- Linux ALSA (`alsa-lib`) oder `/dev/dsp` für Mikrofon-Zugriff
- WAV-Format, 16kHz Mono (optimal für Spracherkennung)
- Gespeichert temporär unter `/tmp/flux_meeting_audio.wav`

**Transkription:**
- **On-Device Option:** `whisper.cpp` (OpenAI Whisper in C++) — läuft auf ARM64
  - Modell: `whisper-small` (~150MB) — gut genug für Deutsch
  - Echtzeit-Transkription möglich (Stream-Modus)
- **MacBook Option:** Whisper über Ollama oder direkte Whisper-API
- Konfigurierbar in Einstellungen: "Meeting-Transkription: Lokal / MacBook"

**Protokoll-Generierung:**
- Nach Stopp: Transkript an KI (fluxaid) mit Prompt:
  "Erstelle ein strukturiertes Meeting-Protokoll mit: Themen, Beschlüssen, Aufgaben.
   Antworte auf Deutsch. Transkript: [TEXT]"
- KI-Antwort wird als formatiertes Protokoll angezeigt

**Speicherung:**
- `/home/user/Meetings/Meeting_YYYYMMDD_HHMM.txt`
- Verzeichnis wird automatisch erstellt

**Neuer Bildschirm:** `FLUX_SCREEN_MEETING`
- Live-Timer oben
- Scrollendes Live-Transkript (letzte 5 Zeilen sichtbar)
- Großer STOPP-Button (rot, gut erreichbar)
- Nach Stopp: Protokoll-Ansicht mit Speichern/Teilen

**Neues KI-Tool:** `meeting_list` — listet alle Protokolle
**Neues KI-Tool:** `meeting_read` — liest ein Protokoll (ARG: YYYY-MM-DD oder "letztes")

**Einstellungs-Erweiterung:**
- Neuer Eintrag in Einstellungen: "Meeting-Transkription"
  Werte: "lokal-whisper" / "macbook" / "aus"

---

## 8. Technische Vorgaben für die Implementierung

### Codebase-Struktur:
```
Flux/
├── shell/               # flux-shell (Framebuffer-UI)
│   ├── src/
│   │   ├── main.c       # Event-Loop, Screen-Handler
│   │   ├── ui.c         # Alle Render-Funktionen (~1600 Zeilen)
│   │   ├── ui.h         # Screen-Enum, Funktions-Deklarationen
│   │   ├── fb.c/h       # Framebuffer (flux_fb_fill_rect, flux_fb_text, ...)
│   │   ├── camera.c/h   # Kamera (V4L2 + Testmuster-Fallback)
│   │   └── input.c/h    # Evdev-Eingabe (Keyboard + Touch)
│   └── Makefile         # SRC := $(wildcard src/*.c) — neue .c automatisch
├── fluxai/              # fluxaid (KI-Daemon)
│   ├── src/
│   │   ├── main.c       # Unix-Socket-Server
│   │   ├── provider.c   # Anthropic API + System-Prompt
│   │   ├── tools.c      # Alle KI-Tools (~1100 Zeilen)
│   │   ├── vision.c/h   # Bild-Analyse via Anthropic Vision
│   │   └── exec.c/h     # Action-Ausführung (Mail, SMS, Anruf)
│   └── Makefile         # SRC := $(wildcard src/*.c)
└── common/              # Geteilt zwischen shell und fluxai
    ├── flux_config.c/h  # Konfiguration (/etc/flux/flux.conf)
    └── flux_protocol.h  # IPC-Protokoll

```

### Build & Test:
```bash
make -C shell    # Kompiliert flux-shell
make -C fluxai   # Kompiliert fluxaid

# Screenshots rendern (kein echtes Gerät nötig):
cd shell && gcc -O2 -Wall -std=gnu11 -D_GNU_SOURCE \
  -o render_screenshots tools/render_screenshots.c \
  src/fb.c src/ui.c src/action.c \
  ../common/flux_config.c ../common/flux_sha256.c -lm
./render_screenshots /tmp/flux_screenshots/
```

### Konfigurationsdateien:
```
/etc/flux/flux.conf      # Hauptkonfig (pin_hash, smtp_*, api_key, theme, ...)
/etc/flux/calendar.txt   # Format: YYYY-MM-DD HH:MM Beschreibung
/etc/flux/contacts.txt   # Format: Name,Telefon,Email
/etc/flux/notes.txt      # Freie Notizen (eine pro Zeile)
/etc/flux/prefs.txt      # Nutzer-Präferenzen (eine pro Zeile)
/etc/flux/memory.txt     # [NEU] AI Memory Einträge
/home/user/Pictures/     # Fotos (PPM + .caption Sidecar-Dateien)
/home/user/Journal/      # [NEU] Tages-Journal Einträge
/home/user/Meetings/     # [NEU] Meeting-Protokolle
```

### Wichtige Implementierungs-Hinweise:

1. **Neue Tools in tools.c**: Immer in `flux_tool_exec()` registrieren UND
   in `flux_tools_description()` mit ARG-Beschreibung dokumentieren

2. **Neue Bildschirme**: Immer in allen Screen-Wechseln berücksichtigen:
   - `FLUX_SCREEN_NOTIFY` return-handling (pre_notify_screen)
   - Swipe-Left für Zurück-Navigation
   - Auto-Lock muss auf neuen Screens funktionieren

3. **Memory-Integration in provider.c**: Die letzten N Memory-Einträge werden
   beim Start in den System-Prompt geladen — Puffergröße beachten (system_prompt[6144])

4. **Proactive-Thread in fluxaid/src/main.c**: Als pthread starten,
   mutex für `/tmp/flux_proactive.txt` Zugriff

5. **Audio (Meeting)**: ALSA-Bibliothek (`-lasound`) zum Makefile hinzufügen

6. **Whisper.cpp**: Separates Subprojekt unter `whisper/` oder als
   System-Paket (`apt install libwhisper-dev`)

### Git-Branch:
Alle Änderungen auf Branch: `claude/ai-mobile-app-j34w3c`

---

## 9. Prioritäts-Reihenfolge für die Implementierung

**Phase A — Grundlagen (implementiere zuerst):**
1. AI Memory (`memory_save`, `memory_list`, `memory_search`, FLUX_SCREEN_MEMORY)
2. Memory in System-Prompt integrieren (provider.c)

**Phase B — Reaktiv:**
3. Proaktive KI (proactive.c, Lockscreen-Banner, Timer-Thread)
4. AI Journal (journal generierung, FLUX_SCREEN_JOURNAL)

**Phase C — Dokumente:**
5. Dokument-KI (Erweiterung FLUX_SCREEN_FILE_VIEWER, DOC_AI Overlay)

**Phase D — Audio (komplex, braucht Hardware):**
6. Meeting-Mitschrift (ALSA, Whisper, FLUX_SCREEN_MEETING)

---

*Dieses Dokument ist das vollständige Briefing für Batch 4 der Flux OS Entwicklung.*
*Alle neuen Features müssen das bestehende Design (Farben, Animationen, Struktur) exakt einhalten.*
