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
  KI-Anbieter gebunden sein (Anthropic/DeepSeek/NVIDIA/llama.cpp sind austauschbar).
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
- (keine unterbrochene Iteration)

### Geplant (priorisiert, oberster zuerst)
- [ ] whisper.cpp lokal – virtuelles Audiogerät in build/run-qemu.sh + gebündeltes Modell im Rootfs-Overlay  — schaltet Mikrofon-Button von Stub auf echte Transkription
- [ ] Echter Compositor (DRM/KMS) – GPU-Beschleunigung, echte Animationen, mehrere „Karten"  — Voraussetzung für flüssige 60fps auf echter Hardware
- [ ] Benachrichtigungen als Systemdienst – Erweiterung von notification.c um Push-Socket, damit fluxaid die Shell direkt wecken kann  — ersetzt Polling durch echtes Push
- [ ] Portierung auf echtes Gerät (postmarketOS) – Gerätebaum, Touchscreen-Treiber, Modem/RIL  — der „echtes Telefon"-Schritt
- [ ] Sicherheitsmodell – App-Sandbox/Permissions, root-freier Betrieb  — Pflicht vor echtem Einsatz

### Ideen aus Recherche (unsortiert, noch zu bewerten)
- [ ] (2026-06-23, whisper.cpp-Repo) Stream-Modus in whisper.cpp – Echtzeit-Transkription statt Batch — passt zu Flux, weil Mikrofon-Button reaktiver wird
- [ ] (2026-06-23, postmarketOS-Wiki) ECAPA-TDNN ONNX-Modell ~20 MB – echtes Speaker-Embedding für voice_unlock.c — passt zu Flux, weil Stub damit ersetzbar ohne UI-Änderung
- [ ] (2026-06-23, ofono-Doku) ofono/ModemManager statt Telephony-Stub – echtes SMS/Anruf-Backend — passt zu Flux, weil telephony.c als austauschbares Backend gebaut ist
- [ ] (2026-06-24, nomic-embed/all-MiniLM via llama.cpp) Echtes Embedding-Backend fuer semantic_search – Query/Zeilen-Vektoren via llama-server /embeddings, Ranking per Cosinus statt lexikalischem Overlap — passt zu Flux, weil semantic_search bereits als austauschbare Score-Funktion geschnitten ist (Quellen/Top-N/Ausgabe bleiben, nur sem_score() wird getauscht), local-first ueber den schon vorhandenen llama-server
- [ ] (2026-06-24, MemX/Mem0/Opal Memory-Forschung) Echtes Graph-/Embedding-Memory-Backend statt regelbasiertem Entitaets-/Zeit-Index – Personen als Knoten, zeitlich indizierte Episoden, semantische Aehnlichkeit statt Substring-Match — passt zu Flux, weil memory_recall bereits genau dort andockbar ist (mr_known_persons-Entitaetsabgleich + sem_score-Score-Funktion austauschbar, Zeitfilter/Top-N/Ausgabe bleiben), local-first ueber denselben llama-server

### Erledigt
- [x] Agentische Systemsteuerung mit Bestaetigung (NL -> jede erlaubte Einstellung) – adressiert „KI als universeller Controller" (Feature-Richtung 1). Neuer Aktionstyp `ACTION:setting` (KEY/VALUE/DESC) erweitert das bestehende ACTION->CONFIRM->X:-Muster RUECKWAERTSKOMPATIBEL (Mail/SMS/Anruf-Pfade unveraendert). System-Prompt (provider.c) instruiert die KI, bei klarem Einstellungs-Auftrag diesen Block zu erzeugen statt direkt zu handeln, MIT expliziter Allowlist im Prompt. Shell: action.c parst KEY/VALUE/DESC; eigener, verstaendlicher Bestaetigungs-Dialog (`flux_ui_draw_confirm_setting`/`flux_ui_confirm_setting_hit`, wiederverwendet confirm_layout/build_confirm_buttons – KEINE neue Geometrie), zeigt z.B. „Helligkeit -> 50%"; „Wert"-Zeile per Tap ueber den vorhandenen EDIT_BODY-Flow bearbeitbar (neues edit_target EDIT_ACTION_SETTING_VALUE); erst „Übernehmen" -> `X:setting`. Theme wird nach dem Setzen sofort per apply_theme() sichtbar. SICHERHEIT: Allowlist + Wert-Validierung MAßGEBLICH im Daemon (exec.c/exec_setting) – brightness 0–100, theme nur erlaubte Werte, Booleans on/off bzw. tts 0/1, vision_backend cloud/local, auto_lock 0/30/60/120/300; jeder Key ausserhalb der Allowlist (inkl. pin_hash/Passwoerter/API-Keys) wird ehrlich abgelehnt, selbst wenn die KI ihn schickt. PIN BEWUSST NICHT in der Allowlist (nie per Sprache aenderbar – nur ueber die Einstellungs-Maske mit gehashter Speicherung). Erlaubte Keys: brightness, theme, ai_provider, ai_router, ai_router_battery, tts, wakeword, vision_backend, auto_lock, voice_unlock_lock. brightness wird real ueber /sys/class/backlight gesetzt (wie brightness_set), reine Config-Keys ueber flux_config_set. README-Abschnitt „Einstellungen per Sprache aendern (mit Bestaetigung)" ergaenzt (Allowlist + QEMU-Effekt-Grenzen ehrlich).  — Compile-Test bestanden (fluxai 15 / shell 31 Warnungen, keine neuen, 0 Fehler), beide Binaries gebaut; End-to-End-Verhalten (Dialog-Flow, echtes Setzen) NICHT in QEMU verifiziert (kein laufender KI-Anbieter im Build/QEMU fuer den ACTION:setting-Pfad; Helligkeit ohne Backlight-Knoten meldet der Daemon dort ehrlich „nicht aenderbar" statt Erfolg)
- [x] translate – Offline-/lokale Uebersetzung ueber den AKTIVEN Anbieter: neues KI-Tool `translate` (tools.c) uebersetzt Text mit dem Sprachmodell des aktuell gewaehlten Anbieters -> ist der lokale llama.cpp-Anbieter gewaehlt, laeuft es offline/local-first (Privacy), sonst Cloud. ARG-Parsing robust + einfach: (1) erstes ':' trennt "<zielsprache>: <text>" (z.B. "englisch: Guten Morgen"); (2) falls kein Doppelpunkt, trennt das LETZTE " nach " das Muster "<text> nach <zielsprache>" (z.B. "Guten Morgen nach englisch"); Zielsprache deutsch benennbar (englisch/franzoesisch/...); greift keines -> ehrliche Hinweis-Meldung mit Beispiel. Umsetzung WIEDERVERWENDEND: KEINE eigene zweite HTTP-Schicht -- neue schmale, additive Provider-Hilfsfunktion `flux_provider_complete(system, user, out, cap)` in provider.c/.h: EIN direkter api_call ueber den aktiven Anbieter, OHNE Tools (System-Prompt ohne flux_tools_description) und OHNE Gespraechsverlauf (use_ctx=0) -> da `translate` AUS der Agenten-Tool-Schleife laeuft, ist so keine Tool-Schleifen-Rekursion moeglich. Bestehende Signaturen (`flux_provider_ask`/`flux_provider_route`/`flux_provider_active`/`flux_provider_available`) UNVERAENDERT. Uebersetzungs-Prompt klar: "Uebersetze ... nach <Zielsprache>. Gib NUR die Uebersetzung zurueck, keine Erklaerung." EHRLICH: ist kein Anbieter nutzbar (kein lokaler Server, kein Cloud-Key), liefert flux_provider_complete 0 + wahrheitsgemaesse Meldung (analog "kein KI-Zugang konfiguriert") -> nie eine erfundene Uebersetzung. Output begrenzt wie die anderen Tools. Registriert in flux_tool_exec() + flux_tools_description() (ARG-Format + zwei Beispiele). README-Abschnitt "Uebersetzung ueber den aktiven Anbieter" ergaenzt.  — Compile-Test bestanden (fluxai 15 / shell 31 Warnungen, keine neuen, 0 Fehler), beide Binaries gebaut; echte Uebersetzungsqualitaet gegen einen laufenden llama-server bzw. Cloud-Anbieter in QEMU NICHT verifiziert (kein Anbieter im Build/QEMU vorhanden) -- der Provider-Pfad selbst ist der bestehende, getestete api_call
- [x] memory_recall – strukturiertes/temporales Gedaechtnis: neues KI-Tool (tools.c) wertet das bestehende Memory (memory.txt) zu einem leichten lokalen Entitaets-/Zeit-Index auf, sodass die KI gezielt nach PERSON und/oder ZEITRAUM recherchieren kann („Was hat Laura letzte Woche gesagt?"). Regelbasierte Anfrage-Analyse: (1) Person -> Abgleich mit bekannten Namen aus PERSON-Eintraegen in memory.txt + erstem CSV-Feld in contacts.txt (mr_known_persons); (2) deutscher Zeitausdruck -> Datumsbereich (mr_parse_time): heute/gestern/vorgestern, diese/letzte (vorige) woche, „letzten <wochentag>" (Vorwoche), diesen/letzten monat, dieses jahr, „im <monat>" (aktuelles Jahr), sowie konkretes YYYY-MM-DD bzw. YYYY-MM; (3) Reststichwoerter -> Term-Overlap (wiederverwendet sem_tokenize/sem_score aus semantic_search). Ranking: memory.txt zeilenweise nach Zeitstempel `[YYYY-MM-DD HH:MM]` (mr_line_time) gefiltert, Person stark gewichtet (Person verlangt aber fehlend -> Zeile raus), Top-5 mit Originalzeile (Zeitstempel+Kategorie+Inhalt) zurueck. EHRLICH: nichts passt -> wahrheitsgemaesse Meldung mit konkretem Filter („Nichts Passendes zu Person 'laura', Zeitraum ...") statt Erfindung; nicht erkannte Zeitausdruecke werden ignoriert statt geraten. KLAR kommentiert als REGELBASIERTER Entitaets-/Zeit-Index (kein Knowledge-Graph-ML, keine Embeddings); ANDOCK-STELLE fuer ein echtes Graph-/Embedding-Backend (MemX/Mem0-artig) im Code markiert (mr_known_persons + sem_score austauschbar). Nur memory.txt + contacts.txt werden geoeffnet (keine beliebigen Pfade), Output wie andere Tools begrenzt. Registriert in flux_tool_exec() + flux_tools_description() (mit Beispielen). Bestehende memory_*-Tools + semantic_search UNVERAENDERT. provider.c: kurzer System-Prompt-Hinweis ergaenzt (memory_recall fuer personen-/zeitbezogene Erinnerungsfragen). README-Abschnitt ergaenzt (ehrlich: regelbasiert, Embedding-/Graph-Backend andockbar).  — Compile-Test bestanden (fluxai 15 / shell 31 Warnungen, keine neuen, 0 Fehler), beide Binaries gebaut; Zeitausdruck-Aufloesung gegen echte Nutzerdaten in QEMU NICHT verifiziert (nur Compile-Test, kein Laufzeit-Test mit befuellter memory.txt)
- [x] Energie-bewusstes Routing (erweitert den Hybrid-Router) – neuer Config-Key `ai_router_battery` (Default `on`, per Tap/flux.conf auf `off`). Greift NUR, wenn der Router aktiv ist (`ai_router=on`) UND `ai_router_battery=on`: bei niedrigem Akku (< 20 %) bevorzugt `router_pick()` jetzt das leichtere LOKALE Modell – AUCH fuer „harte" Anfragen –, sofern ein lokaler Server erreichbar ist (`local_server_reachable`), und spart so Energie/teure Cloud-Roundtrips. Akkustand EHRLICH ueber denselben sysfs-Mechanismus wie actions.c (`/sys/class/power_supply/{battery,BAT0}/capacity`, neue Helfer `router_battery_pct`/`router_battery_enabled` in provider.c). KEIN Fake: fehlt der Sensor (QEMU) oder ist der Wert nicht lesbar -> Energie-Logik inaktiv, Router-Verhalten unveraendert. Ist kein lokaler Server erreichbar -> normal zur Cloud (Log-Hinweis, nie ins Leere routen). Neuer Pfad-Marker 'B' -> Log „Router: Akkusparmodus (…%) -> lokal" und (nur bei `ai_router_marker=on`) „[lokal: Akkusparmodus]" vor der Antwort. Additiv in den bestehenden Router-Funktionen; `flux_provider_route`/`flux_provider_ask` unveraendert, Nicht-Router-Pfad (`ai_router=off`) unberuehrt, Hintergrunddienste routen weiterhin NICHT. Shell: neuer Settings-Toggle „KI-Akkusparmodus" (FLUX_SETTINGS_N 15->16, alle Parallel-Arrays + Tap-Handler analog `ai_router`), Anzeige ehrlich („Ein (inaktiv: KI-Router aus)" / „Ein (inaktiv: kein Akku-Sensor)" / „Ein (bei <20%% lokal)" / „Aus"). README Hybrid-Router-Abschnitt + flux.conf-Keys ergaenzt.  — Compile-Test bestanden (fluxai 15 / shell 31 Warnungen, keine neuen, 0 Fehler), beide Binaries gebaut; Akkusparmodus in QEMU NICHT verifiziert (kein power_supply-Sensor -> Logik dort ehrlich inaktiv), gegen echten Akku-Sensor + lokalen llama-server unverifiziert
- [x] Kamera-OCR -> Dokument-KI verketten (local-first): neues KI-Tool `ocr_scan` (tools.c) macht aus einem Foto eines Dokuments (Brief/Vertrag/Bon) wortgetreuen Text, ueber den die KI sprechen kann („Was ist die Kuendigungsfrist?") -- KEIN Cloud-Scandienst. ARG: Bildpfad (Pictures/Documents) ODER „letztes"/„neuestes" (waehlt die neueste Bilddatei in /home/user/Pictures nach mtime). OCR-Backend ehrlich + austauschbar: bevorzugt das vorhandene lokale VLM (vision.c, `vision_backend=local`) mit OCR-Prompt -- dafuer vision.c refaktoriert (gemeinsamer `vision_dispatch(prompt,...)`, neue API `flux_vision_ocr()` mit OCR-Prompt „Gib NUR den Text wortgetreu wieder"; `vision_cloud`/`vision_local` nehmen jetzt den Prompt als Parameter, Bildbeschreibung unveraendert). Als klassische lokale Alternative (ANDOCK-STELLE markiert) `tesseract`-Binary via fork/execvp (kein Shell-Aufruf, Sprachen deu+eng mit Fallback), Praesenz per PATH-Scan geprueft. Backend-Reihenfolge: bei `vision_backend=local` zuerst VLM dann tesseract, sonst zuerst tesseract dann VLM; jeweils der andere als Fallback. EHRLICH: liefert kein Backend Text -> wahrheitsgemaesse Meldung „Kein OCR-Backend verfuegbar -- lokalen VLM-Server starten oder tesseract installieren" statt erfundenem Text. Verkettung: erkannter Text wird zusaetzlich nach /home/user/Documents/ocr_<ts>.txt geschrieben (Verzeichnis bei Bedarf angelegt); Rueckgabe = Pfad + Hinweis „-> doc_analyze mit ARG <pfad>" + Textauszug, sodass die KI direkt `doc_analyze` (gleiche Traversal-Absicherung) auf den Pfad anwenden kann. Path-Traversal-sicher: ARG-Pfad per realpath() auf /home/user/Pictures bzw. /home/user/Documents begrenzt (analog doc_analyze). Registriert in flux_tool_exec() + flux_tools_description() (mit Verkettungs-Hinweis). README-Abschnitt „Kamera-OCR (local-first)" ergaenzt.  — Compile-Test bestanden (fluxai 15 / shell 31 Warnungen, keine neuen, 0 Fehler), beide Binaries gebaut; OCR-Qualitaet gegen echten VLM-Server bzw. echtes tesseract in QEMU NICHT verifiziert (kein VLM-Server/tesseract im Build/QEMU vorhanden), beide OCR-Backends bleiben bis zum Andocken unverifiziert
- [x] Lokales Vision-Modell (VLM) als austauschbares, local-first Bild-KI-Backend neben der Cloud. vision.c refaktoriert: Bild-Aufbereitung (PPM→JPEG via ImageMagick→base64) einmalig in `load_image_b64()`, danach zwei Backends `vision_cloud()` (Anthropic, unveraendertes Verhalten = Default) und `vision_local()` (lokaler VLM-Server, OpenAI-Vision-Format: Bild als `image_url` mit `data:image/jpeg;base64,...`, analog zum llama.cpp-Textanbieter -- kein Vendoring, kein API-Key, nur HTTP an konfigurierbaren Endpunkt). Auswahl ueber Config-Key `vision_backend` (`cloud` Default / `local`); lokaler Endpunkt `vlm_url` (Default `http://127.0.0.1:8081/v1/chat/completions`, env-Fallback `VLM_URL`), Modell `vlm_model` (Default-Platzhalter `moondream`). EHRLICH: ist der lokale VLM-Server nicht erreichbar, wahrheitsgemaesse Meldung („Kein lokaler VLM-Server erkannt -- laeuft moondream/llama-server mit Vision unter <url>?") statt erfundener Bildbeschreibung; Andock-Stelle fuer das echte VLM in `vision.c`/`vision_local` klar markiert. Shell: neuer Settings-Toggle „Bild-KI (Cloud/Lokal)" (Config-Key `vision_backend`, per Tap, FLUX_SETTINGS_N 14→15, alle Parallel-Arrays + Anzeige + Tap-Handler analog `ai_router`). README: neuer Vision-Abschnitt (Cloud/lokaler VLM, welche Modelle, wie man den Server startet, offline/local-first-Vorteil, ehrlicher Erreichbarkeits-Hinweis) + flux.conf-Keys. Cloud-Pfad unveraendert (Default), Protokoll Shell↔Daemon stabil.  — Compile-Test bestanden (fluxai 15 / shell 31 Warnungen, keine neuen, 0 Fehler), beide Binaries gebaut; gegen einen ECHTEN lokalen VLM-Server NICHT in QEMU verifiziert (kein VLM-Server im Build/QEMU vorhanden), Backend bleibt bis zum Andocken eines echten Servers unverifiziert
- [x] Wake-Word „Hey Flux" + lokales TTS (beide als ehrliche, andockbare Stubs). WAKE-WORD: neues Shell-Modul `shell/src/wakeword.c`+`.h` (Schnittstelle init/poll/deinit), bewusst auf der immer-lauschenden SHELL-Seite (nicht im Daemon), weil es — wie der Mikrofon-Knopf — den Assistenten-Eingabemodus wecken soll. Neuer Settings-Toggle „Wake-Word (Hey Flux)" (Config-Key `wakeword`, Default aus, per Tap analog `ai_router`). Verdrahtung in main.c: `flux_wakeword_poll()` im Idle-Zweig; ein (auf echter HW) erkanntes Wake-Word setzt `want_voice_start` und springt per `goto start_voice` in den BESTEHENDEN Mikrofon-Start-Pfad (kein zweiter Mechanismus, gleiche Aufnahme-Logik). EHRLICH: QEMU hat kein Mikrofon → `flux_wakeword_init()` bleibt inaktiv (prueft `flux_voice_can_record()`), `poll()` liefert nie einen Treffer, kein Fake-Trigger; Settings zeigt „Ein (inaktiv: kein Mikrofon)". Andock-Stelle openWakeWord/porcupine (ONNX) klar im Code markiert. TTS: bestehende inline-`tts_speak()` (espeak/flite, faked Erfolg ohne Audio-Check) refaktoriert in austauschbares Backend `shell/src/tts.c`+`.h` (`flux_tts_speak`/`flux_tts_available`); prueft jetzt EHRLICH Backend-Binary UND `/dev/snd` → ohne Audiogeraet lautlos statt stiller Fake. Aufruf-Stelle (nach jeder KI-Antwort, nur wenn `tts=1`) unveraendert. Andock-Stelle **piper** (lokales neuronales TTS, Pipe → aplay) im Code dokumentiert. Makefile zieht beide neuen .c automatisch per wildcard (keine Makefile-Aenderung). README (neue Abschnitte Wake-Word + TTS, Roadmap-Punkt 6 verlinkt) + flux.conf-Beispiel (`tts`/`wakeword`) ehrlich aktualisiert.  — Compile-Test bestanden (fluxai 15 / shell 31 Warnungen, keine neuen, 0 Fehler), beide Binaries gebaut; gegen echtes Audiogeraet (Wake-Word-Erkennung / TTS-Wiedergabe) NICHT in QEMU verifiziert (QEMU `virt` hat kein Audiogeraet), Backends openWakeWord/piper bleiben Stub bis echte HW
- [x] Stimm-Entsperrung am Lockscreen (nur richtige Person, PIN bleibt unumgehbar) – neuer Config-Toggle `voice_unlock_lock` (Default aus) als Einstellungs-Eintrag „Stimm-Entsperrung am Lockscreen" (Tap schaltet aus/ein, analog ai_router). Wenn aktiv UND eine Stimme eingelernt UND KEINE PIN gesetzt: Lockscreen zeigt einen Mikrofon-Chip „Zum Entsperren sprechen" (neue ui.c-Geometriefunktion `lock_voice_chip_geom` fuer Zeichnen+Hit-Test, `flux_ui_set_lock_voice_hint`/`flux_ui_lock_voice_hit`), Tap fuehrt in den bestehenden FLUX_SCREEN_VOICE_VERIFY. Erfolg → direkt Assistent. SICHERHEITS-ENTSCHEIDUNG: Ist eine PIN gesetzt, erscheint der Sprechen-Weg am Lockscreen NICHT (Stimme bleibt nur zweiter Faktor NACH PIN, PIN nie per Stimme allein umgehbar) — der Settings-Wert zeigt dann ehrlich „Ein (inaktiv: PIN gesetzt)". EHRLICH: kein Mikrofon / Mismatch / Ueberspringen entsperrt nicht, sondern fuehrt zurueck zum Lockscreen (Wisch bleibt immer Standardweg → nie Lockout), kein Fake-Erfolg; voice_unlock.c-Stub unveraendert (additiv). Neuer Lock-Pfad bricht Auto-Lock/Swipe-Back/Notify nicht; KI-Overlay zusaetzlich auf VOICE_VERIFY gesperrt (keine KI waehrend des Entsperrens). README Punkt 8 + Hinweis-Abschnitt ehrlich aktualisiert.  — Compile-Test bestanden (fluxai 15 / shell 31 Warnungen, keine neuen, 0 Fehler), Verhalten gegen echtes Mikrofon NICHT in QEMU verifiziert (QEMU virt hat kein Audiogeraet)
- [x] semantic_search – lokale RAG-/„Frag-dein-Geraet"-Suche: neues KI-Tool (tools.c) durchsucht OFFLINE die festen Quellen memory.txt/notes.txt/calendar.txt/contacts.txt + alle /home/user/Journal/*.txt und gibt die besten passenden Snippets mit Quelle als RAG-Kontext zurueck. Ranking: ehrliche lexikalische Overlap-Heuristik (Tokenisierung lowercase + Split an Nicht-Buchstaben, Term-Overlap Query↔Zeile mit leichter Term-Frequenz-Gewichtung, Top-5). KEIN ML — klar so kommentiert; echtes Embedding-Backend (nomic-embed/all-MiniLM via llama-server /embeddings) dockt als austauschbare Score-Funktion an (sem_score()). EHRLICH: leere Quellen / kein Treffer → wahrheitsgemaesse Meldung statt Erfindung. Path-Traversal-sicher (ARG nie als Pfad, nur feste Quell-Pfade). Registriert in flux_tool_exec() + flux_tools_description() (Prompt-Hinweis „such in meinen Notizen/im Journal …" inklusive, da Description automatisch in den System-Prompt geht). README-Abschnitt ergaenzt (ehrlich: lexikalisch, Embedding-Backend andockbar).  — Compile-Test bestanden (fluxai 15 / shell 31 Warnungen, keine neuen, 0 Fehler), Ranking-Qualitaet gegen echte Nutzerdaten in QEMU NICHT verifiziert
- [x] Hybrid-Router (lokal vs. Cloud) – neuer Config-Key `ai_router` (off Default/on), off-by-default und rueckwaertskompatibel; bei `on` waehlt eine bewusst SIMPLE Heuristik pro Q:-Anfrage den Anbieter: kurze (<~200 Zeichen) plauder-/wissensartige Anfragen gehen an den lokalen llama.cpp-Server (mit kurzem Erreichbarkeits-Check via curl), „harte" (lang ODER Schluesselwoerter wie code/programmier/analysiere/ausfuehrlich) zum konfigurierten Cloud-Anbieter; ehrliche Fallbacks (lokal nicht erreichbar → Cloud; keine Cloud → lokal). Umgesetzt durch `resolve_provider_id(override_id, ...)` + interner `provider_ask_with()`; `flux_provider_ask()` bleibt unveraendert (Hintergrunddienste routen NICHT), neuer Einstieg `flux_provider_route()` nur fuer den interaktiven Q:-Pfad in main.c. Transparenz via flux_log („Router: lokal/Cloud-Pfad") + optionaler dezenter Marker „[lokal]"/„[cloud]" (`ai_router_marker=on`). UI-Toggle „KI-Router (lokal/Cloud)" in den Einstellungen (Tap schaltet aus/ein, analog KI-Anbieter). README-Abschnitt ergaenzt (ehrlich: simple Heuristik, kein gelerntes Routing).  — Compile-Test bestanden (fluxai + shell, keine neuen Warnungen ggü. Vorzustand), Routing-Pfade gegen echten llama-server NICHT in QEMU verifiziert
- [x] On-Device-LLM als 4. Anbieter „Lokal (llama.cpp)" – neuer Eintrag im PROVIDERS[]-Array (provider.c, FMT_OPENAI, konfigurierbarer Endpunkt `llamacpp_url`/env `LLAMACPP_URL`, Default 127.0.0.1:8080, Modell `llamacpp_model`/Default "local-model", kein API-Key noetig), `flux_provider_available()` wuergt key-lose lokale Anbieter nicht mehr ab, ehrliche Fehlermeldung wenn llama-server nicht erreichbar; Shell-Provider-Rotation um „llamacpp" erweitert, Modell-Feld editierbar, URL nur per flux.conf (kein eigener Listeneintrag, ehrlich dokumentiert); README-Tabelle + Start-Anleitung ergaenzt  — Compile-Test bestanden (fluxai + shell, keine neuen Warnungen), gegen echten llama-server nicht in QEMU verifiziert
- [x] FLUX_SCREEN_JOURNAL – dedizierter Journal-Screen (Liste, Tap öffnet Eintrag, kein Löschen)  — Compile-Test bestanden, QEMU nicht verifiziert
- [x] journal_list / journal_read KI-Tools  — Compile-Test bestanden
- [x] meeting_list / meeting_read KI-Tools  — Compile-Test bestanden
- [x] doc_analyze KI-Tool  — Compile-Test bestanden, path-traversal-gesichert
- [x] Item 8 – Stimm-Entsperrung als zweiter Faktor: voice_unlock.c/.h (RMS-Fingerabdruck-Stub), FLUX_SCREEN_VOICE_ENROLL, FLUX_SCREEN_VOICE_VERIFY, PIN→Voice-Verify-Flow, Einstellungs-Eintrag  — ehrlicher Stub (kein Mikrofon in QEMU), auf echter Hardware austauschbar durch ECAPA-TDNN-Backend
- [x] Item 10 – Benachrichtigungs-Dienst als pthread in fluxaid: notification.c/.h, prüft Kalender/Memory/Batterie/Proaktiv alle 15 Min, schreibt nach /tmp/flux_notifications.txt  — Compile-Test bestanden, QEMU nicht verifiziert
- [x] Notification-Thread in fluxaid/src/main.c integriert  — Compile-Test bestanden
