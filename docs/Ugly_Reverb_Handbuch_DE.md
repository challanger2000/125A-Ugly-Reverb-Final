# 125A Ugly Reverb - Handbuch

**Character Reverb / Controlled Wrongness**

125A Ugly Reverb ist ein dunkles, metallisches Character-Reverb für Windows x64. Es wurde bewusst um Eigenschaften gebaut, die moderne Reverbs oft vermeiden: hörbare Moden, metallisches Klingeln, harte Reflexionen, instabile Resonanzen und früh-digitale Textur.

## Installation

1. ZIP-Datei vollständig entpacken.
2. Den kompletten Ordner `125A Ugly Reverb.vst3` nach `C:\Program Files\Common Files\VST3\` kopieren.
3. Eine eventuell vorhandene ältere Version vorher ersetzen.
4. DAW neu starten oder den VST3-Scan erneut ausführen.

## Bedienelemente

- **Material** - Plate / Thin Plate / Heavy Plate / Sheet / Spring / Steel / Pipe / Metal Drum / Oil Can / Chamber / Tank.
- **Size** - skaliert die resonante Struktur und die wahrgenommene Raumgröße.
- **Decay** - bestimmt die Dauer der Hallfahne.
- **Pre-Delay** - 0 bis 180 ms.
- **Diffusion** - bestimmt, wie stark die parallelen Echos in die seriellen Diffusionsstufen übergehen.
- **Damping** - Höhenabsorption innerhalb des Feedback-Netzwerks.
- **Body** - verschiebt die strukturelle Gewichtung des Resonanzsystems.
- **Width** - steuert die Stereo-Breite des bearbeiteten Signals.
- **Metal** - verschiebt das Netzwerk zu kürzeren, härteren und deutlich metallischen Resonanzen.
- **Clang** - hebt ausgewählte Moden an und macht das Klingeln absichtlich deutlicher hörbar.
- **Rattle** - fügt kontrollierte mechanische Modulation zu ausgewählten Resonanzen hinzu.
- **Digital Color** - Clean / 12-bit / 8-bit Quantisierung innerhalb des Feedback-Netzwerks.
- **Mix** - linearer Dry/Wet-Mix.
- **Output** - -12 dB bis +12 dB.
- **Bypass** - exakter Plugin-Bypass.

## Technik

- VST3, Windows x64
- Stereo In / Stereo Out
- 32-bit Float Processing
- 0 Samples gemeldete Latenz
- 150 Sekunden gemeldete Reverb-Tail
- sample-genaue Parameter-Automation
- versionierter Component-State mit Migration aus dem früheren State-Format
- GUI-Zoom 100 / 125 / 150 / 175 / 200 Prozent
- 1x/2x HiDPI-Ressourcen für Knobs sowie Wear/Glass-Overlays

## DSP-Konzept

Metal, Clang und Rattle sind Bestandteile des Hallnetzwerks selbst und keine einfachen Nachbearbeitungs-Effekte. Der Kern kombiniert materialabhängige Comb- und Allpass-Strukturen, modale Gewichtung, kontrollierte Feedback-Asymmetrie, optionale mechanische Bewegung und optionale digitale Quantisierung innerhalb des Feedback-Pfads.

## Praxis-Tipps

- **Subtil:** Metal und Clang niedrig halten, Diffusion erhöhen, Damping nach Geschmack einstellen.
- **Industrial:** Metal und Clang deutlich erhöhen, Rattle hinzunehmen und mit Pipe / Steel / Metal Drum experimentieren.
- **Lo-fi:** Digital Color auf 12-bit oder 8-bit stellen. Die Quantisierung sitzt im Feedback-Netzwerk und beeinflusst deshalb die Hallfahne selbst.
- **Größe:** Size und Body greifen in die Resonanzstruktur ein; extreme Werte können bewusst stark metallische Ergebnisse erzeugen.

## Finale Validierung

- GitHub Actions Build #67: SUCCESS
- DSP Measurement Suite: PASS
- Steinberg VST3 Validator: 47/47 PASS
- Canonical VST3 Package Validation: PASS
- Reload/Lifecycle Diagnostic: 5/5 PASS
- 125A Plugin Tester: PASS
- Project-owned Compiler Warnings: 0

## Lizenz

Die Release-Version unterliegt der beiliegenden 125A End User License Agreement. Musik und Audio, die mit dem Plugin erzeugt wurden, dürfen kommerziell genutzt werden. Die Plugin-Binärdatei bzw. das Release-Paket darf nicht ohne Erlaubnis weitergegeben oder weiterverkauft werden.

125A / AUDIO SOFTWARE
