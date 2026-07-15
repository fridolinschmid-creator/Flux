# FluxOS-Dokumentation

Diese Seite ist der Einstieg in die Dokumentation von FluxOS.

## Für Nutzer

- [Benutzerhandbuch](USER_GUIDE.md) — Bedienung, Eingaben, Notizen, Dateien,
  KI-Aktionen und Sicherheitsbestätigungen.
- [Notizen-Screenshot](screenshots/27b_notizen.png) — Beispiel der neuen
  Notizen-Oberfläche.

## Für Entwicklung und Betrieb

- [Architektur](ARCHITECTURE.md) — Prozesse, Zustandsmodell und IPC-Protokoll.
- [Designsystem](DESIGN.md) — Farben, Typografie, Icons und Animationen.
- [Icons](ICONS.md) — Einbindung und Rasterung der Lucide-Icons.
- [Raspberry Pi 5](RASPBERRY-PI-5.md) — Hardware-Build und Installation.
- [Audit](AUDIT.md) — bekannte technische Schulden und Sicherheitsbefunde.

## Schnellorientierung

```text
flux-shell  ->  /run/flux/fluxai.sock  ->  fluxaid  ->  Linux/Buildroot
   zeichnet          Q: Frage / X: Aktion             denkt und handelt
```

FluxOS ist aktuell ein funktionaler Prototyp für QEMU `aarch64 virt` und den
Raspberry Pi 5. Es ist noch kein allgemein unterstütztes Smartphone-
Betriebssystem: einige Hardware-Backends, etwa Modem, Mikrofon und bestimmte
Sensoren, melden fehlende Hardware bewusst statt Erfolg zu simulieren.
