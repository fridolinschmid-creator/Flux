# FluxOS-Entwicklung

## Projektstruktur

| Verzeichnis | Verantwortung |
| --- | --- |
| `shell/` | UI-Prozess, Framebuffer, Touch und Bildschirmtastatur |
| `fluxai/` | KI-Daemon, lokale Intents, Tools und Aktionen |
| `common/` | Konfiguration, Logging, Kryptografie und IPC-Vertrag |
| `build/` | Buildroot-Konfiguration, Overlays und Startskripte |
| `tests/` | Host-Unit-Tests und Protokolltest |
| `docs/` | Architektur-, Design-, Betriebs- und Benutzerdokumentation |

## Lokaler Build

```bash
make
make test
```

Die Shell benötigt für den Host-Build zusätzlich die Linux- und Grafik-
Entwicklungsheader. Für das Zielsystem wird der von Buildroot erzeugte
Cross-Compiler verwendet:

```bash
make -C shell CROSS_COMPILE=aarch64-linux-gnu-
make -C fluxai CROSS_COMPILE=aarch64-linux-gnu-
```

Ein komplettes Image wird mit `./build/build.sh` gebaut. Die exakten
Voraussetzungen und der Raspberry-Pi-5-Ablauf stehen in
[RASPBERRY-PI-5.md](RASPBERRY-PI-5.md).

## Verifikation

Die wichtigste Qualitätsgrenze ist ein warnungsarmer Build mit:

```text
-Wall -Wextra -Wformat-security -std=gnu11
```

Für UI-Änderungen rendert `shell/tools/render_screenshots.c` die Bildschirme
ohne Hardware in PNG-Dateien. Draw- und Hit-Test-Code müssen dieselbe
Geometrie-Hilfsfunktion verwenden; sonst stimmen Darstellung und Touch-
Bereich nicht mehr überein.

## IPC-Vertrag

Der Vertrag liegt in [`common/flux_protocol.h`](../common/flux_protocol.h).

- `Q:<frage>\n` fragt den Assistenten.
- `X:<typ>\nTO:…\nSUBJECT:…\nBODY:\n<text>` führt eine zuvor bestätigte
  Aktion aus.
- Antworten enden mit `END`.

Änderungen an diesem Vertrag wirken sich auf Shell, Daemon und Tests aus und
müssen gemeinsam geprüft werden. Besonders wichtig: `Q:` darf keine reale
  Mail-, SMS- oder Telefonaktion direkt ausführen.

## UI-Regeln

Neue Screens benötigen:

1. einen Eintrag in `flux_screen_t` in `shell/src/ui.h`,
2. eine Zeichenfunktion in `shell/src/ui.c`,
3. einen passenden Hit-Test,
4. einen Event-Loop-Zweig in `shell/src/main.c`,
5. einen Screenshot-Renderer-Test.

Text, Kommentare und UI-Bezeichnungen bleiben deutsch. Hardware- und
Netzwerkfehler werden sichtbar und nachvollziehbar dargestellt.

## Konfiguration und Geheimnisse

Laufzeitgeheimnisse gehören in `/etc/flux/flux.conf` mit restriktiven
Berechtigungen. Niemals API-Schlüssel, SMTP-Passwörter oder PIN-Hashes in
README, Screenshots, Tests oder Commits ablegen. Die UI zeigt Geheimnisse nur
als gesetzt beziehungsweise maskiert.
