# Logo-Prompt fuer ChatGPT / Bildgenerierung

Verwendung: in ChatGPT (Bildmodus) einfuegen. Das Ergebnis als PNG
exportieren und mit ImageMagick ins Boot-Logo umwandeln (siehe
[`RASPBERRY-PI-5.md`](RASPBERRY-PI-5.md), Abschnitt 4b):
```bash
convert flux-logo.png -resize 320x320 -dither FloydSteinberg \
        -colors 224 build/raspberrypi5/flux-bootlogo.ppm
```

> Tipp: Das Boot-Logo wird auf nur **224 Farben** reduziert und klein
> dargestellt. Bitte ChatGPT also um eine Variante mit **kraeftigen,
> flaechigen Farben, klaren Kanten, ohne feine Verlaeufe** und auf
> **rein schwarzem Hintergrund** (passt zum dunklen Flux-Bootscreen).

---

## Der Prompt (kopierfertig)

> Entwirf mir ein Logo fuer ein Betriebssystem namens **„Flux"**.
>
> **Was Flux ist (Kontext, damit du den Charakter triffst):** Flux ist
> ein rebellisches, KI-zentriertes Handy-/Geraete-Betriebssystem. Es
> bricht bewusst mit der Idee des App-Rasters von iOS und Android --
> es gibt **kein Grid aus hunderten Icons**. Stattdessen *ist* ein
> KI-Assistent der Homescreen: man entsperrt das Geraet und **redet**
> einfach, statt zu suchen. Flux laeuft direkt auf dem Linux-Kernel,
> ist in purem C geschrieben, ohne fette Frameworks, und zeichnet seine
> Oberflaeche pixelgenau selbst auf den Bildschirm. Werte: **schnell,
> minimalistisch, privat (laeuft lokal), eigensinnig, ein bisschen
> Hacker-Punk.** Es soll sich anfuehlen wie „das OS, das ein genialer,
> leicht verrueckter Tueftler nachts in der Garage gebaut hat".
>
> **Die Idee „Flux":** Flux = Fluss, Stroemung, staendige Bewegung,
> ein Energiefeld in Bewegung. Denk an fliessende Energie, einen
> magnetischen Fluss, fluessiges Plasma, einen Strom aus Daten. Genau
> diese **Bewegung und dieses Fliessen** soll man im Logo spueren --
> als waere es im naechsten Moment schon weitergeflossen.
>
> **Konkrete Gestaltung:** Ein **stilisiertes, fettes „F"**, das aus
> einem **fliessenden, sich windenden Energiestrom** geformt ist --
> wie fluessiges Neon oder eine Plasma-Schlaufe, die sich zum
> Buchstaben F kruemmt. Glaenzend, fast fluessig-metallisch.
> **Hauptfarben:** ein elektrisches **Indigo/Violett (#6C5CE7)**, das
> in ein helleres **Cyan-Blau (#00D2FF)** uebergeht -- als waere
> Strom hindurchgejagt. Optional ein paar **wegspritzende
> Funken/Partikel oder Glitch-Fragmente** am Rand, die die „leicht
> verrueckte" Energie zeigen. Strenger schwarzer Hintergrund, damit
> die Farben leuchten.
>
> **Stil:** modern, futuristisch, hochkontrastig, ikonisch genug, um
> winzig (als Boot-Logo, App-Icon) noch erkennbar zu sein --
> **eine einzige, klare Form**, kein verspielter Detailwust. Mut zur
> Verruecktheit ist erwuenscht: lieber kuehn und eigen als brav.
>
> **Bitte liefere zwei Varianten:**
> 1. das **Energie-F-Symbol allein** (quadratisch, zentriert, fuer
>    Boot-Logo/Icon),
> 2. dasselbe Symbol **plus den Schriftzug „Flux"** darunter in einer
>    klaren, leicht technischen, fetten Schrift (fuer Splash/Header).
>
> Format: quadratisch, **schwarzer Hintergrund**, kraeftige flaechige
> Farben, klare Kanten (keine zarten Verlaeufe -- das Logo wird spaeter
> auf 224 Farben reduziert).

---

## Variations-Ideen, falls dir das erste Ergebnis zu brav ist

Haeng eine dieser Zeilen an den Prompt an:

- „Mach es **wilder**: das F soll aussehen, als wuerde es gerade
  explodieren/auseinanderfliessen, mit einem Schweif aus Energie."
- „Eher **Retro-Synthwave**: Neon-Gitter, Sonnenuntergangs-Verlauf,
  80er-Vibe, aber immer noch das fliessende F."
- „Eher **fluessiges Quecksilber/Chrom**: das F wie ein
  3D-Metall-Tropfen, der gerade Form annimmt, mit Reflexen."
- „Fuege einen **dezenten pulsierenden Punkt** rechts neben dem F hinzu
  -- den gibt es in der echten Flux-UI auch (Akzent-Puls)."
