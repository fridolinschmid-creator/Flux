# Flux auf dem Raspberry Pi 5

Diese Anleitung beschreibt, wie das komplette Flux-Betriebssystem auf
**echter Hardware** -- einem Raspberry Pi 5 -- gebaut, installiert und
gebrandet wird. Bisher lief Flux nur in QEMU (`aarch64 virt`); der Pi 5
ist der erste echte Geraete-Port (Roadmap-Punkt 11).

## Warum der Pi 5 gut passt

- **Gleiche CPU-Architektur (arm64).** Der Pi 5 hat einen BCM2712 mit
  Cortex-A76 -- also `aarch64`, genau wie das bisherige QEMU-Ziel.
  `flux-shell` und `fluxaid` mussten dafuer **nicht** umgeschrieben
  werden, sie werden nur mit der Pi-Toolchain neu uebersetzt.
- **`/dev/fb0` ist vorhanden.** Der Pi 5 nutzt den modernen vc4-KMS-
  Treiber, aber mit aktivierter *fbdev-Emulation* (`CONFIG_DRM_FBDEV_-
  EMULATION`) gibt es weiterhin ein `/dev/fb0`. Genau darauf zeichnet
  `flux-shell` -- der Renderer bleibt unveraendert.
- **Touch funktioniert ueber dieselbe Abstraktion.** `shell/src/input.c`
  liest generisch `/dev/input/eventN` (evdev). Ein offizielles Pi-
  Touch-Display oder ein USB-Touchscreen liefert exakt dieselben
  `EV_ABS`/`BTN_TOUCH`-Events wie das `virtio-tablet` in QEMU.
- **Echtes WLAN.** Anders als in QEMU hat der Pi einen echten WLAN-Chip.
  Die Flux-Defconfig zieht die noetige Firmware (`rpi-wifi-firmware`)
  und `wpa_supplicant`/`iw` mit ein -- der WLAN-Screen
  (`shell/src/wifi.c`) wird damit auf dem Pi tatsaechlich nutzbar.

## Welches OS laeuft im Hintergrund?

Dasselbe Prinzip wie bei der QEMU-Variante, nur mit Pi-Kernel:

- **Kernel:** der von der Raspberry Pi Foundation gepflegte Linux-Kernel
  (`linux-rpi`/BCM2712) -- der einzige Kernel mit vollstaendigen Pi-5-
  Treibern (VideoCore VII, RP1-Southbridge fuer USB/Ethernet/GPIO).
- **Userspace/Rootfs:** **Buildroot** baut ein minimales eigenes Linux
  (BusyBox-Init, glibc, ext4). Das ist **kein** Raspberry Pi OS und
  **kein** Android -- nur die schmale Basis, auf der `fluxaid` und
  `flux-shell` als einzige sichtbare Schicht laufen. Die ganze
  Flux-eigene UI/KI-Schicht ist unveraendert dieselbe wie in QEMU.

Kurz: **Linux-Kernel (Pi-Fork) + Buildroot-Minimal-Rootfs + Flux**.
Wir bauen Flux bewusst nicht auf Raspberry Pi OS auf, weil Flux *die*
gesamte Oberflaeche stellen will (kein Desktop, kein Login-Manager) --
ein vollwertiges Distro-Userland waere nur Ballast.

## 1. Voraussetzungen (auf dem Build-Rechner, Linux)

```bash
# Buildroot 2024.02.x oder neuer (erst dort gibt es raspberrypi5_defconfig)
git clone --depth 1 --branch 2024.02.x \
    https://github.com/buildroot/buildroot.git /tmp/flux-build/buildroot
```
Ausserdem die ueblichen Build-Tools (`build-essential`, `git`, `wget`,
`cpio`, `unzip`, `rsync`, `bc`, `file`, ImageMagick fuer das Logo).

## 2. Image bauen

```bash
./build/build-pi5.sh
```
Das Skript:
1. setzt aus Buildroots `raspberrypi5_defconfig` + unserem Fragment
   (`build/raspberrypi5/flux-pi5.fragment`) die Flux-Defconfig zusammen,
2. legt (falls vorhanden) das eigene Boot-Logo in den Kernelbaum,
3. baut Toolchain, Pi-5-Kernel und Rootfs,
4. uebersetzt `flux-shell`/`fluxaid` mit der Pi-Toolchain und packt sie
   ins Overlay (`build/overlay-pi5/`),
5. erzeugt ein fertiges **`sdcard.img`**.

Ergebnis: `/tmp/flux-build/out-pi5/images/sdcard.img`.

## 3. Auf die SD-Karte schreiben

```bash
lsblk                          # Zielgeraet sicher identifizieren!
./build/flash-pi5.sh /dev/sdX  # /dev/sdX = die GANZE Karte, nicht /dev/sdX1
```
microSD in den Pi 5 stecken, Strom dran. Der erste Boot dauert etwas
laenger (Rootfs wird auf Kartengroesse erweitert). Danach:
Lockscreen -> Wisch nach oben -> Assistent.

> Alternativ kann man `sdcard.img` auch mit dem **Raspberry Pi Imager**
> (Option „Use custom image") oder `balenaEtcher` schreiben.

## 4. Die Marke beim Starten aendern ("Boot-Branding")

Beim Hochfahren des Pi laufen **drei** Marken-Ebenen ab -- jede laesst
sich getrennt anpassen:

### a) Firmware-Splash (der bunte Regenbogen direkt nach dem Einschalten)
Das ist das Allererste, was die Pi-Firmware zeigt. Schon abgeschaltet in
`build/raspberrypi5/config.txt`:
```ini
disable_splash=1
```

### b) Das Kernel-Boot-Logo (DAS ist die eigentliche "Marke beim Starten")
Sobald der Kernel den Bildschirm uebernimmt, zeigt Linux ein Logo
(standardmaessig der Tux-Pinguin). Das ersetzen wir durch das Flux-Logo:

1. **Logo erzeugen** (224 Farben, das ist das Maximum dieses Logo-Typs).
   Aus einem PNG -- z.B. dem ChatGPT-Logo (siehe
   [`docs/LOGO-PROMPT.md`](LOGO-PROMPT.md)):
   ```bash
   convert flux-logo.png -resize 320x320 -dither FloydSteinberg \
           -colors 224 build/raspberrypi5/flux-bootlogo.ppm
   ```
2. **Bauen.** `build-pi5.sh` ruft automatisch
   `build/raspberrypi5/install-bootlogo.sh` auf, das die Datei nach
   `drivers/video/logo/logo_linux_clut224.ppm` im Kernelbaum kopiert.
   Aktiviert ist das Logo bereits ueber
   `build/raspberrypi5/linux-pi5.fragment`
   (`CONFIG_LOGO` + `CONFIG_LOGO_LINUX_CLUT224`).
3. Damit nur **ein** Logo statt vier (eins pro CPU-Kern) erscheint, steht
   in `build/raspberrypi5/cmdline.txt` schon `fbcon=logo-count:1`.

> Tipp: Liegt das Logo beim allerersten Build noch nicht vor, wird der
> Kernel zunaechst mit Standard-Logo gebaut. Logo erzeugen und dann
> einmal `make -C /tmp/flux-build/buildroot O=/tmp/flux-build/out-pi5
> linux-rebuild all` laufen lassen -- oder `build-pi5.sh` erneut starten.

### c) Die Marke INNERHALB von Flux (Lockscreen + Assistent)
Das ist der „Flux"-Schriftzug und der runde **F**-Button in der UI --
kein Boot-, sondern ein Laufzeit-Element. Quelle: `shell/src/ui.c`.
- Kopfzeile „Flux": Zeile ~1114 (`const char *title = "Flux";`).
- „Flux KI"-Karte: Zeile ~522.
- Großes **F**-Logo auf dem leeren Assistenten: Zeile ~1163
  (`flux_fb_text(... "F" ...)`).
Hostname (`flux`, erscheint auf der seriellen Konsole):
`build/overlay-pi5/etc/hostname`.

## 5. KI/Mail/WLAN einrichten

Identisch zur QEMU-Variante (siehe Haupt-README): Einstellungen im
Gerät oder direkt `/etc/flux/flux.conf`. Auf dem Pi sind WLAN, echte
Uhrzeit (NTP) und ein realer Framebuffer vorhanden -- API-Key
hinterlegen, WLAN verbinden, fertig.

## Was auf dem Pi (noch) wie in QEMU ein ehrlicher Stub bleibt

- **Mobilfunk (SMS/Anruf):** Der Pi 5 hat kein Modem.
  `fluxai/src/telephony.c` meldet das weiterhin ehrlich. Mit einem
  USB-/HAT-Modem + ModemManager liesse sich genau diese eine Datei
  ersetzen.
- **Mikrofon/Whisper:** braucht ein USB-Mikrofon + gebuendeltes
  `whisper.cpp`-Modell (Roadmap-Punkt 6).
- **Akku:** ein Desktop-Pi hat keinen Akku-Sensor; mit einem UPS-HAT
  (I2C) liesse sich `read_battery_pct()` anbinden.
