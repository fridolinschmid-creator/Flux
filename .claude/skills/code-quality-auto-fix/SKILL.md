---
name: code-quality-auto-fix
description: >
  Analysiert den gesamten Projektcode und verbessert ihn automatisch —
  intelligenter Code-Reviewer und Refactoring-Agent in einem. Findet und
  behebt ineffizienten, doppelten oder fehlerhaften Code, Performance- und
  Speicherprobleme, Sicherheitsluecken, potenzielle Bugs, Logikfehler,
  veraltete APIs sowie Stil- und Konsistenzprobleme. Nutzen bei Anfragen
  wie "Code verbessern", "Code aufraeumen", "Code-Qualitaet pruefen",
  "Refactoring", "Auto-Fix", "code review", "improve code quality",
  "clean up code", "fix code issues".
---

# Code Quality & Auto Fix

Du arbeitest als **Code-Reviewer und Refactoring-Agent** fuer dieses
Projekt. Analysiere den Code, behebe Probleme selbststaendig, wenn du dir
sicher bist, und erstelle am Ende einen Bericht. **Korrektheit hat immer
Vorrang vor aggressiven Optimierungen** — das Verhalten des Projekts darf
sich nicht aendern.

Alle Pfade in diesem Dokument sind relativ zur Repo-Wurzel.

## Grundregeln (gelten fuer jede Aenderung)

- Bestehende Funktionalitaet erhalten — niemals Verhalten aendern.
- Moeglichst kleine, sichere Aenderungen; ein Problem pro Commit-Einheit.
- Bestehende Architektur respektieren; keine unnoetigen Refactorings.
- Breaking Changes vermeiden (oeffentliche Schnittstellen, Protokolle,
  Dateiformate, IPC-Nachrichten bleiben stabil).
- Vorhandene Coding-Standards und Namenskonventionen des Projekts
  uebernehmen (dieses Repo: C11/gnu11, `flux_`-Praefix, deutsche
  Kommentare ohne Umlaute, snake_case).
- Nur automatisch fixen, was mit **hoher Sicherheit** korrekt ist. Bei
  Zweifel: nicht fixen, sondern im Bericht unter "bewusst nicht behoben"
  auffuehren — mit Begruendung.

## Phase 1 — Projekt verstehen (vor der ersten Aenderung)

1. Struktur erfassen: In diesem Repo sind die Einheiten
   - `shell/` — `flux-shell`, Framebuffer-UI in C (Makefile-Build)
   - `fluxai/` — `fluxaid`, System-KI-Daemon in C (Makefile-Build,
     braucht libcurl)
   - `common/` — geteilter Code (Config, Log, SHA-256, Protokoll),
     wird von **beiden** Binaries mitkompiliert — Aenderungen hier
     wirken sich auf beide aus.
   - `build/` — Buildroot-Overlays/Configs fuer QEMU und Raspberry Pi 5
     (Shell-/Config-Dateien, kein C-Code).
2. Sprachen/Frameworks erkennen: Bei anderen Projektlagen (JS, Python,
   …) die jeweiligen Manifeste (`package.json`, `pyproject.toml`, …)
   lesen und deren Lint-/Test-Kommandos verwenden.
3. **Baseline bauen, bevor du irgendetwas aenderst** (siehe
   "Verifikation" unten). Warnungen der Baseline notieren — nur so
   erkennst du spaeter, ob eine Warnung neu ist.

## Phase 2 — Analyse

Durchsuche den Code systematisch nach diesen Problemklassen:

- **Effizienz:** unnoetiges Kopieren, wiederholte Berechnungen in
  Schleifen, unnoetige Allokationen, lineare Suchen wo unnoetig.
- **Duplikate:** kopierte Codebloecke, die in eine Hilfsfunktion
  gehoeren (nur zusammenfassen, wenn die Kopien wirklich identisch
  gemeint sind).
- **Speicher/Ressourcen:** Leaks (`malloc` ohne `free`, `fopen` ohne
  `fclose`, Sockets/FDs), fehlende Fehlerpfad-Aufraeumung,
  use-after-free, Puffergroessen.
- **Sicherheit:** ungepruefte `strcpy`/`sprintf`/`strcat`,
  Format-String-Probleme, fehlende Laengen-/NULL-Checks an
  Vertrauensgrenzen (IPC-Socket, Netzwerk/IMAP/HTTP, Dateiinhalte),
  Command-Injection in `exec`-Pfaden, Integer-Overflows bei
  Groessenberechnungen.
- **Bugs/Edge-Cases:** Off-by-one, ungepruefte Rueckgabewerte,
  Race-Conditions (fluxaid nutzt pthreads), NULL-Dereferenzen,
  nicht initialisierte Variablen, Logikfehler.
- **Veraltete APIs:** unsichere/deprecated libc-Funktionen, veraltete
  libcurl-Optionen.
- **Stil/Konsistenz:** Abweichungen von den Konventionen der Datei
  selbst; tote Funktionen; auskommentierter Code; irrefuehrende Namen.
- **Wartbarkeit/Lesbarkeit:** ueberlange Funktionen mit klar trennbaren
  Abschnitten, magische Zahlen, fehlende `static`-Markierung fuer
  dateilokale Funktionen.
- **Dateiorganisation:** nur reorganisieren, wenn es die Struktur klar
  verbessert und keine Include-/Build-Brueche erzeugt (Makefiles nutzen
  `$(wildcard src/*.c)` — neue/verschobene `.c`-Dateien unter `src/`
  werden automatisch mitgebaut).

Nuetzliche Suchmuster fuer die C-Codebasis:

```bash
# Unsichere String-Funktionen (Treffer einzeln pruefen, nicht blind ersetzen)
grep -rn --include='*.c' -E '\b(strcpy|strcat|sprintf|gets)\s*\(' shell/src fluxai/src common

# Ungeprueftes malloc
grep -rn --include='*.c' -E '=\s*(malloc|calloc|realloc)\(' shell/src fluxai/src common
```

Klassifiziere jedes Finding als:
- **Auto-Fix** — Korrektur eindeutig, Verhalten identisch, lokal begrenzt.
- **Nur berichten** — Fix waere riskant, mehrdeutig, architektur-
  veraendernd oder verhaltensrelevant.

## Phase 3 — Fixen, verifizieren, iterieren

Fuer **jedes** Auto-Fix-Finding, in dieser Reihenfolge:

1. Betroffenen Code vollstaendig lesen und verstehen (auch Aufrufer:
   `grep -rn 'funktions_name' shell fluxai common`).
2. Auswirkungen auf andere Dateien pruefen — besonders bei `common/`
   (beide Binaries!) und bei Headern.
3. Die sicherste Loesung waehlen und minimal umsetzen.
4. **Sofort verifizieren** (siehe unten). Neue Warnung oder Fehler →
   Aenderung korrigieren oder zuruecknehmen.
5. Pruefen, ob der Fix weitere sichere Verbesserungen freilegt.

Iteriere ueber die gesamte Codebasis, bis keine offensichtlichen, sicher
behebbaren Probleme mehr uebrig sind. Danach Bericht schreiben.

## Verifikation (Pflicht nach jeder Aenderung)

Beide Binaries muessen **warnungsfrei gegenueber der Baseline** bauen
(`-Wall -Wextra -Wformat-security` sind bereits in den Makefiles):

```bash
# Einmalig noetig, falls curl/curl.h fehlt:
apt-get update && apt-get install -y libcurl4-openssl-dev

make -C shell        # baut shell/flux-shell
make -C fluxai       # baut fluxai/fluxaid (braucht -lcurl -lpthread, macht das Makefile)
```

Bei Aenderungen an UI-Code (`shell/src/ui.c`, `icons.c`, `fb.c`, …)
zusaetzlich den Screenshot-Renderer laufen lassen — er rendert alle ~40
Screens aus exakt dem echten Zeichencode und stuerzt bei Regressionen ab:

```bash
cd shell && gcc -O2 -Wall -std=gnu11 -D_GNU_SOURCE \
  -o /tmp/render_screenshots tools/render_screenshots.c \
  $(ls src/*.c | grep -v main.c) ../common/*.c -lm -lpng
mkdir -p /tmp/flux_shots && /tmp/render_screenshots /tmp/flux_shots
# Erwartet: "Fertig! 40 Screenshots ..." — einzelne PNGs stichprobenartig ansehen.
```

Vergleiche die gerenderten PNGs mit `docs/screenshots/` — bei reinen
Qualitaets-Fixes duerfen sie sich **nicht** unterscheiden.

In fremden Projekten stattdessen: vorhandene Test-/Lint-Kommandos aus
Manifest bzw. CI-Konfiguration verwenden; existiert nichts, mindestens
den Build ausfuehren.

## Bericht (am Ende, als Abschlussnachricht)

Erstelle einen uebersichtlichen Bericht mit:

- Anzahl gefundener Probleme (nach Kategorie)
- Anzahl automatisch behobener Probleme
- Performance-Verbesserungen (was, wo, warum)
- Sicherheitsverbesserungen (was, wo, warum)
- Durchgefuehrte Refactorings
- Geaenderte Dateien (mit `datei:zeile`-Verweisen)
- **Bewusst nicht behobene Probleme** — jedes mit Begruendung, warum
  ein Auto-Fix zu riskant war und was ein Mensch entscheiden muss

## Stolperfallen in diesem Repo

- `fluxai/` baut ohne `libcurl4-openssl-dev` nicht (`curl/curl.h`
  fehlt). Vor dem `apt-get install` ist ein `apt-get update` noetig,
  sonst 404 auf das Paketarchiv.
- `make` bricht beim **ersten** Fehler ab, meldet aber vorher viele
  Zeilen glibc-Fortify-Noise — nach `error:` greppen, nicht nur die
  letzten Zeilen lesen.
- `common/` wird per `../common/*.c` in **beide** Binaries kompiliert:
  nach jeder Aenderung dort beide Builds pruefen.
- Der Screenshot-Renderer ersetzt `main.c` der Shell (eigene `main`);
  beim Kompilieren `src/main.c` ausschliessen, sonst doppelte `main`.
- Kommentare/Strings im Repo sind Deutsch **ohne Umlaute** (ae/oe/ue/ss)
  — bei Textaenderungen beibehalten.
