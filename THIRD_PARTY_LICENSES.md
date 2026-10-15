# Abhängigkeiten und Lizenzen

Stand: 14.10.2026. Keine rechtliche Beratung – vor einer Veröffentlichung die aktuellen Originalbedingungen prüfen.

| Komponente | Version | Wird ausgeliefert? | Lizenz | Konsequenz für den Verkauf |
|---|---|---|---|---|
| JUCE Framework | 8.0.9 | Ja (statisch gelinkt) | Dual: AGPLv3 **oder** kommerzielle JUCE-Lizenz | Für ein closed-source verkauftes Plugin ist eine JUCE-Lizenz nötig (siehe unten) |
| Steinberg VST3 SDK (in JUCE enthalten) | wie in JUCE 8.0.9 gebündelt | Ja (Teile im Binary) | BSD-artige Steinberg-Lizenz (`JUCE/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt`) | Copyright-Hinweis in der Dokumentation des Produkts mitliefern; VST-Logo/Marke nur gemäß `VST3_Usage_Guidelines.pdf` |
| Signalsmith Stretch (Pitch-Shifter) | 1.4.0 (`a670068`), in `third_party/` | Ja (header-only, kompiliert ins Binary) | MIT | Copyright- und Lizenztext (`third_party/signalsmith-stretch/LICENSE.txt`) in der Produktdokumentation mitliefern |
| Signalsmith Linear (FFT, Abhängigkeit von Stretch) | 0.6.4 (`de55e6a`), in `third_party/` | Ja (header-only) | MIT | Copyright- und Lizenztext (`third_party/signalsmith-linear/LICENSE.txt`) mitliefern; optionale Beschleuniger (IPP, PFFFT, xsimd) sind **nicht** aktiviert |
| Microsoft Visual C++ Runtime | MSVC 14.44 | Statisch gelinkt (JUCE-Standard) | Microsoft-Lizenz, Weitergabe erlaubt | keine |
| pluginval (Tracktion) | 1.0.4 | **Nein** – nur Testwerkzeug | GPLv3 | keine (wird nicht verteilt) |
| Schrift „Inter“ (Regular, Medium, SemiBold, Display Light) | 4.1, in `Resources/Fonts/` | Ja (als Binärdaten eingebettet) | SIL Open Font License 1.1 | Lizenztext (`Resources/Fonts/Inter-LICENSE.txt`) mitliefern; die Schrift darf nicht einzeln verkauft werden (Einbetten in Software ist erlaubt) |

## Eigene Algorithmen

Alle DSP-Module sind eigenständig implementiert und beruhen auf frei veröffentlichten Lehrbuchverfahren:

- Biquad-Filter: Formeln aus R. Bristow-Johnson, „Audio EQ Cookbook“ (frei verwendbar)
- Kompressor: Feed-Forward-Gain-Computer mit Soft-Knee und log-domain Attack/Release-Glättung (Standardverfahren, u. a. beschrieben von Giannoulis/Massberg/Reiss, JAES 2012)
- De-Esser: dynamischer High-Shelf, gesteuert von einem Hochpass-Detektor
- Sättigung: asymmetrische tanh-Kurve, 2× Oversampling über `juce::dsp::Oversampling`
- Delay: Ringpuffer mit Hermite-Interpolation, Filter und Soft-Limiter in der Rückkopplung
- Reverb: Feedback Delay Network (8 Leitungen, Hadamard-Matrix, Allpass-Diffusion) – allgemein bekannte Struktur (Jot/Stautner-Puckette), eigene Parameterwahl

Es werden keine fremden Presets, Markennamen, Designs oder proprietären Algorithmen verwendet. Die Factory-Presets sind eigene Einstellungen.

## JUCE-Lizenz – was vor dem Verkauf zu tun ist

Laut https://juce.com/get-juce/ (abgerufen am 14.10.2026):

| Stufe | Preis | Umsatz-/Finanzierungsgrenze |
|---|---|---|
| Starter | kostenlos | bis 20.000 USD |
| Indie | 40 USD/Monat oder 800 USD einmalig | bis 300.000 USD |
| Pro | 175 USD/Monat oder 3.500 USD einmalig | unbegrenzt |

- Ohne JUCE-Lizenz gilt AGPLv3: dann müsste der vollständige Quellcode des Plugins unter AGPLv3 veröffentlicht werden. Für ein closed-source verkauftes Produkt ist das in der Regel nicht gewollt.
- Die Lizenz muss gelten, solange closed-source Binaries mit JUCE verteilt werden.
- Die Seite verweist inzwischen auf die **JUCE 9 EULA**. Dieses Projekt nutzt JUCE 8.0.9. Vor dem Verkauf klären, welche EULA-Version gilt bzw. ob ein Upgrade sinnvoll ist (sales@juce.com).
- Weitere Punkte vor Release: Markenrecherche für den Arbeitstitel „Velvox“ und „Lind Audio“, Code-Signing des Installers, Datenschutz-/Lizenztext im Produkt.
