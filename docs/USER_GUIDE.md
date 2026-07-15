# FluxOS-Benutzerhandbuch

## Was ist FluxOS?

FluxOS ist ein mobiles Linux-System mit einem KI-Assistenten als Startseite.
Nach dem Entsperren gibt es kein App-Raster. Du kannst entweder direkt etwas
eingeben, die Bildschirmtastatur verwenden oder — wenn ein Mikrofon und die
lokale Sprachsoftware vorhanden sind — sprechen.

Die wichtigsten Bereiche sind über den Assistenten, die Schnellzugriffe oder
klare Begriffe erreichbar:

| Eingabe | Bereich |
| --- | --- |
| `Einstellungen` | System- und Konto-Einstellungen |
| `Dateien` | Datei-Browser und Text-Betrachter |
| `Kalender` | Monatsansicht und Termine |
| `Kontakte` | Kontaktliste |
| `Fotos` oder `Galerie` | Bilder und Kamera |
| `Notizen` | Gespeicherte Notizen |
| `Journal` oder `Tagebuch` | Tagesprotokolle |
| `Gedaechtnis` oder `Erinnerungen` | KI-Gedächtnis |
| `Wecker` oder `Timer` | Alarme und Timer |
| `Suche` | Semantische Suche über lokale Inhalte |
| `Browser` | Web-Browser |

## Eingaben

Tippe auf die Eingabezeile mit dem Hinweis **„Schreib etwas …“**. Danach wird
die Bildschirmtastatur eingeblendet. Mit Enter oder der Bestätigungstaste wird
die Eingabe abgeschickt.

Die Eingabezeile unterstützt:

- Bildschirmtastatur und erkannte Hardware-Tastatur
- Rücktaste und Enter
- Wortvorschläge
- Kopieren und Einfügen
- optionale Spracheingabe über das Mikrofon

Ein Wisch nach oben öffnet die Tastatur, ein Wisch nach unten schließt sie.
Ein weiterer Wisch nach unten öffnet die Benachrichtigungen.

## Notizen

Notizen können direkt über die KI gespeichert werden, zum Beispiel:

```text
Notiere: Morgen Max wegen der Unterlagen zurückrufen.
```

Mit `Notizen` wird der eigene Notizen-Bereich geöffnet. Dort gibt es eine
übersichtliche Liste, eine Einzelansicht und oben die sichtbare Aktion
**Neue Notiz**. Die Eingabemaske hat eine klare Überschrift und speichert den
Eintrag mit Zeitstempel. Die KI kann Notizen außerdem auflisten, durchsuchen
und — nach einer eindeutigen Anweisung — löschen.

Die Notizen liegen im laufenden System in:

```text
/etc/flux/notes.txt
```

Die Datei gehört zum persönlichen Systemzustand und sollte nicht öffentlich
freigegeben werden.

## E-Mail, SMS und Anrufe

FluxOS führt reale Aktionen niemals direkt nach einer freien KI-Antwort aus.
Bei einer Nachricht oder einem Anruf zeigt Flux zuerst eine Übersicht mit
Empfänger, Betreff und Inhalt.

1. Inhalt prüfen.
2. Optional einzelne Felder bearbeiten.
3. **Senden** oder **Abbrechen** wählen.

Erst nach **Senden** wird die Aktion über eine separate `X:`-Anfrage an den
Systemdienst übergeben. Fehlt die erforderliche Hardware oder Konfiguration,
zeigt Flux eine ehrliche Fehlermeldung.

## Dateien und Datenschutz

Der Datei-Browser ist standardmäßig lesend nutzbar. Schreib- und Lösch-
Funktionen sind auf den vorgesehenen Benutzerbereich beschränkt. Geheimnisse
wie PIN-Hash, API-Schlüssel und SMTP-Passwort werden in den Einstellungen nur
maskiert angezeigt.

Lokale Fragen — etwa Uhrzeit, Datum, Wochentag, Akku oder Uptime — werden ohne
Cloud-Anfrage beantwortet. Für andere Fragen wird nur dann ein konfigurierter
Cloud-Anbieter verwendet, wenn das Gerät die Antwort nicht lokal liefern kann.

## Einstellungen

Die Einstellungen sind über den Schnellzugriff oder mit `Einstellungen`
erreichbar. Dort können unter anderem PIN, KI-Anbieter, E-Mail-Konto, WLAN,
Spracheingabe, Vorlesefunktion und Protokollierung verwaltet werden.

Wenn ein PIN-Hash gesetzt ist, muss das Gerät nach dem Lockscreen mit dem PIN
entsperrt werden. Eine eingerichtete Stimme ersetzt den PIN nicht, sondern ist
ein zusätzlicher Faktor.

## Bekannte Einschränkungen

FluxOS ist noch in Entwicklung. Je nach Zielsystem können folgende Funktionen
fehlen:

- QEMU besitzt keine echte Mobilfunk-, Kamera-, Akku- oder Funkhardware.
- Telefonie und SMS benötigen ein kompatibles Modem-Backend.
- Spracheingabe benötigt `arecord` oder `ffmpeg` sowie ein lokales Whisper-
  Modell.
- Der Host-Build prüft den C-Code, rendert aber ohne `/dev/fb0` keinen echten
  Gerätescreen.
- Für einen vollständigen Image-Build wird Buildroot benötigt.

Flux meldet diese Fälle bewusst als nicht verfügbar, statt einen Erfolg zu
erfinden.
