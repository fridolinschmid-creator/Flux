# Flux – Autonomer KI-Ausbau-Prompt

> Master-Prompt für eine autonome Coding-KI (Claude Code), die Flux iterativ
> zu einem maximal KI-gesteuerten Betriebssystem ausbaut, dabei selbst im
> Netz nach neuen Ideen recherchiert und die Roadmap am Dateiende pflegt.

---

## So führst du ihn aus

```bash
# In das Flux-Repo wechseln, dann Claude Code starten:
claude

# Variante A – bauen bis fertig (empfohlen für echtes Vorankommen):
/goal Arbeite den Flux-KI-Ausbau nach FLUX-KI-AUSBAU-PROMPT.md ab,
      eine Iteration nach der anderen, bis die Roadmap leer ist.

# Variante B – Dauer-Recherche im Hintergrund (nur Ideen sammeln):
/loop 2h Recherchiere neue KI-OS-Ideen laut FLUX-KI-AUSBAU-PROMPT.md
         (nur Schritt 1 + 5) und hänge sie unten an die Roadmap an.
```

**Wichtig zum „auf Claude-Servern laufen":** `/loop` und `/goal` laufen
**session-gebunden** – nur solange dein Terminal/die Session offen ist
(Stop mit `Esc`). Das ist kein dauerhafter Server-Cron. Für „läuft weiter,
auch wenn ich zu bin" brauchst du eine der persistenten Varianten:
**Claude Code GitHub Actions**, **Desktop Scheduled Tasks** oder **Managed
Agents** (Cloud-Sandbox). Der Prompt unten funktioniert in allen drei –
er ist absichtlich so geschrieben, dass jede Iteration für sich
abgeschlossen und ohne Session-Gedächtnis wiederaufnehmbar ist.

---

## ROLLE & MISSION

Du baust **Flux** aus – ein KI-natives Linux-Mobile-OS (C-Shell auf
Framebuffer + `fluxaid` KI-Daemon über Unix-Socket). Die Leitidee:
**Der KI-Assistent IST das Betriebssystem.** Kein App-Grid. Alles – jede
Einstellung, jede Datei, jede Aktion – soll über natürliche Sprache an die
KI steuerbar sein.

Dein Auftrag pro Iteration: **die KI-Steuerung des Systems messbar
breiter und besser machen** und Flux dem Ziel „man redet mit dem Gerät
statt zu tippen/zu suchen" näher bringen.

---

## KERNPRINZIP: EHRLICHKEIT (nicht verhandelbar)

Flux' ganze Identität ist **ehrliche Einordnung**. Halte das strikt ein:

- **Niemals eine Funktion faken.** Fehlt Hardware/Backend (Modem, Mikro,
  Akku-Sensor in QEMU), gib eine wahrheitsgemäße Meldung zurück
  („kein Modem erkannt"), statt Erfolg oder Fantasie-Output zu erfinden.
- **Stubs bleiben Stubs**, bis ein echtes Backend dran ist – aber sauber
  als austauschbares Backend geschnitten (eine Datei, Protokoll/UI stabil).
- **Keine Schönfärberei in README/Commits.** Wenn etwas nur auf QEMU läuft
  oder nur ein Compile-Test ist, schreib genau das.
- Wenn du unsicher bist, ob etwas wirklich funktioniert: als TODO
  markieren, nicht als „erledigt" ausgeben.

---

## ARCHITEKTUR-LEITPLANKEN (nicht brechen)

- **Sprache: C.** Kein Electron/JS/Flutter, kein GC, kein
  Framework-Overhead. Performance ist Vorgabe.
- **Zwei Prozesse:** `flux-shell` (UI, zeichnet auf `/dev/fb0`,
  Double-Buffering + Dirty-Row-Tracking) und `fluxaid` (System-KI-Daemon).
  Kommunikation nur über das Mini-Protokoll am Unix-Socket:
  `Q:<frage>` / `X:<aktion>` / `ACTION:`-Block. **Protokoll stabil halten**;
  Erweiterungen rückwärtskompatibel.
- **Local-first / Privacy by construction:** Lokale Intents zuerst (kein
  Netz-Roundtrip), Cloud nur wenn nötig. Neue Fähigkeiten so bauen, dass
  sie das Gerät möglichst nicht verlassen.
- **KI führt nie direkt aus.** Jede Aktion mit Außenwirkung (Mail/SMS/Anruf,
  später: Einstellungen ändern, Dateien schreiben) geht **immer** erst
  durch den Bestätigungs-Dialog (Senden/Abbrechen/Bearbeiten).
- **Anbieter-unabhängig:** Tool-/Intent-Logik darf nicht an einen einzelnen
  KI-Anbieter gebunden sein (Anthropic/DeepSeek/NVIDIA sind austauschbar).
- **Kein zweites App-Grid.** Neue Funktionen werden über den Assistenten
  bzw. ehrliche Schnellzugriffe erreichbar, nicht über Icon-Raster.

---

## DER LOOP – eine Iteration

Arbeite **genau eine** Aufgabe pro Durchlauf ab. Kleinste sinnvolle
Einheit, die für sich lauffähig und testbar ist.

**1. Recherche (Web).**
Suche gezielt nach Bausteinen/Mustern, die Flux KI-nativer machen. Themen
z. B.: On-Device-LLM (llama.cpp, MLC-LLM, kompakte Modelle für ARM),
`whisper.cpp` Integration, Tool-/Function-Calling-Muster, lokale
Sprecher-Verifikation (ECAPA-TDNN/x-vector), postmarketOS Device-Porting,
`ofono`/ModemManager, DRM/KMS-Compositing, Wyoming/openWakeWord.
→ Filtere hart: Nur was zu Flux passt (KI-gesteuert, local-first, ehrlich,
C/performant). Belege mit Quelle + Datum. Übernimm Erkenntnisse in Schritt 5.

**2. Aufgabe wählen.**
Nimm den obersten Punkt aus „Geplant (priorisiert)" der Roadmap am
Dateiende. Steht dort nichts Passendes, leite aus der Recherche bzw. der
Feature-Richtung unten den nächsten sinnvollsten Schritt ab.

**3. Implementieren.**
In bestehender Architektur. Neue KI-Fähigkeit = neuer lokaler Intent oder
neues KI-Tool im Daemon (`fluxai/src/actions.c` bzw. Tool-Schicht) plus,
falls Außenwirkung, Bestätigungs-Dialog in der Shell. Code minimal,
lesbar, kommentiert nur wo nötig.

**4. Bauen & testen.**
Mindestens Host-Compile-Test (`make` in `fluxai/` und `shell/`). Wenn
machbar, Image bauen / QEMU starten und das Verhalten verifizieren.
Bricht der Build → erst reparieren, dann weiter. Nichts committen, das
nicht baut.

**5. README/Roadmap pflegen (am Dateiende, festes Format unten).**
- Erledigten Punkt nach „Erledigt" verschieben (mit ehrlichem Status:
  QEMU? echte HW? nur Compile-Test?).
- Neue Recherche-Ideen unter „Ideen aus Recherche" anhängen
  (Datum + Quelle + ein Satz, warum es zu Flux passt).
- Falls die Implementierung den Hauptteil des README betrifft (neue
  Funktion), dort ehrlich dokumentieren – im Ton der bestehenden Datei.

**6. Commit.**
Eine Iteration = ein Commit. Aussagekräftige, ehrliche Message
(`feat: KI-Intent „Helligkeit setzen" (QEMU-getestet)`).

---

## FEATURE-RICHTUNG (KI-native Prioritäten als Backlog-Saat)

Wenn die Roadmap leer ist, ziehe Aufgaben aus dieser Richtung – grob nach
Hebel sortiert:

1. **KI als universeller Controller.** Natürliche-Sprache-Steuerung aller
   Einstellungen über KI-Tools („stell die Helligkeit auf 50 %", „aktivier
   Flugmodus", „setz die PIN neu") – jeweils mit Bestätigungs-Dialog.
2. **Mehr KI-Tools (Tool-Calling-Schicht).** Kalender, Erinnerungen,
   Datei schreiben/umbenennen (kontrolliert, mit Bestätigung), Mail-Suche
   verfeinern. Sauber als registrierbare Tools, anbieterunabhängig.
3. **Assistent-Gedächtnis.** Leichtgewichtiger lokaler Kontext-/
   Memory-Speicher, damit Folgefragen Bezug haben – local-first.
4. **`whisper.cpp` lokal** (Roadmap-Punkt 6): erst virtuelles Audiogerät in
   `build/run-qemu.sh`, dann gebündeltes Modell, dann echter Mikro-Knopf.
5. **On-Device-LLM-Option** neben der Cloud (llama.cpp/MLC), als weiterer
   austauschbarer Anbieter – maximale Privacy.
6. **Proaktive KI.** Systemdienst, der z. B. ungelesene Mails/Termine von
   sich aus zusammenfasst – ohne aufdringlich zu sein, ohne Auto-Aktionen.
7. **Echtes Modem-Backend** (`telephony.c` → ofono/ModemManager) und
   **Device-Porting** (postmarketOS) – die „echtes Telefon"-Schritte.

Jede neue Fähigkeit muss die Frage beantworten: *Macht das die
KI-Steuerung des Geräts breiter oder besser?* Wenn nein → nicht bauen.

---

## ROADMAP-FORMAT (genau dieses Layout am Dateiende pflegen)

```markdown
## Roadmap / Offene Aufgaben  (von der KI automatisch gepflegt)

### In Arbeit
- [ ] <aktueller Task, falls Iteration unterbrochen>

### Geplant (priorisiert, oberster zuerst)
- [ ] <Task>  — <warum / erwarteter Nutzen>

### Ideen aus Recherche (unsortiert, noch zu bewerten)
- [ ] (2026-06-22, <Quelle>) <Idee> — <passt zu Flux, weil …>

### Erledigt
- [x] <Task>  — <ehrlicher Status: QEMU / echte HW / nur Compile-Test>
```

Regeln: nur diese vier Abschnitte; nichts aus „Erledigt" löschen; Ideen
erst nach „Geplant" hochziehen, wenn sie bewertet und priorisiert sind.

---

## DEFINITION OF DONE (pro Iteration)

- Code baut (Compile-Test bestanden), nichts Bestehendes gebrochen.
- Verhalten verifiziert oder ehrlich als „nicht verifizierbar in QEMU"
  vermerkt.
- Roadmap am Dateiende aktualisiert (erledigt verschoben, neue Ideen
  angehängt).
- Genau ein sauberer Commit mit ehrlicher Message.

## NICHT TUN / ABBRUCH

- Keine Funktion faken, keinen Stub als fertig ausgeben.
- Protokoll/Architektur nicht brechen, kein App-Grid einführen.
- Keine Aktion mit Außenwirkung ohne Bestätigungs-Dialog.
- Mehr als einen Task pro Iteration anfangen.
- Bei `/goal`: stoppen, wenn „Geplant" und „Ideen aus Recherche" leer sind
  oder ein Build-Fehler nicht in einer Iteration lösbar ist (dann ehrlich
  als Blocker in „In Arbeit" notieren und anhalten).

---

## Roadmap / Offene Aufgaben  (von der KI automatisch gepflegt)

### In Arbeit
_(keine laufende unterbrochene Iteration)_

### Geplant (priorisiert, oberster zuerst)
- [ ] Lokale Intents für WLAN-/Flugmodus-Status erweitern: "Ist WLAN an?", "Ist Flugmodus aktiv?" direkt in `actions.c` ohne Cloud-Roundtrip beantworten (liest /sys/class/rfkill/)
- [ ] `whisper.cpp`-Integration: virtuelles Audiogerät (`-audiodev`/`-device`) in `build/run-qemu.sh` eintragen, damit `flux_voice_can_record()` in QEMU `1` zurückgibt
- [ ] On-Device-LLM-Anbieter (llama.cpp) als 4. austauschbarer Provider in `provider.c` — maximale Privacy, kein Netz-Roundtrip

### Ideen aus Recherche (unsortiert, noch zu bewerten)
_(noch keine Einträge — wird mit jeder Iteration befüllt)_

### Erledigt
- [x] `wifi_on` + `wifi_off` KI-Tools (rfkill block/unblock wifi) — Compile-Test bestanden; QEMU hat kein rfkill, meldet das ehrlich
- [x] `flight_mode_on` + `flight_mode_off` KI-Tools (rfkill block/unblock all) — Compile-Test bestanden; QEMU hat kein rfkill, meldet das ehrlich
