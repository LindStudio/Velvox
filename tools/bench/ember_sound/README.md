# EMBER sound measurements (2026-11-23)

Messen, was EMBER mit echten Takes macht (Klangtest `docs/products/EMBER_KLANG_TEST.md`).

- Ordner anlegen mit `data/takes/*.wav` (die 15 Klangtest-Takes) und optional `data/prods/*.wav` (Pro-DS-Renderings),
  diese Skripte hineinkopieren (`pip install numpy scipy soundfile`).
- `python run.py <EmberTests.exe> name:key=wert,key=wert ...` rendert alle Takes mit `EmberTests --process` und zeigt
  die Mittelwerte (S-Kern, Luft über dem S, S-Schwerpunkt, unberührte S, Vokale ...). Beispiel:
  `python run.py EmberTests.exe def: auto:auto=1 tief:freq=5000,thr=-35`
- `python grp.py` dieselben Werte nach Stimmlage (tiefe / mittlere / hohe S) und die AUTO-Werte je Ausschnitt.
- `freq.py` / `det.py`: Experimente zur S-Frequenz und zur Detektor-Neigung; `snips2.py <ziel>` schneidet Hörbeispiele.

2026-11-23 (fix/ember-dsp):
- `run.py` zeigt auch Vokal-Präsenz, Delta-Reinheit (`deess_metrics.py`) und Neu-Auslösungen pro S; Konfigurationen mit
  Präfix `B_` laufen mit `base/EmberTests.exe` (z. B. der Stand vor einer Änderung): `python run.py neu.exe B_def: def:`
- `mix.py <exe> name:args ...`: die Takes in einem künstlichen Mix (lautes Bett bis 2,5 kHz) – prüft Allround (`det=3`)
- `cpu.py exe[@args] ...`: CPU-Zeit des Prozesses (Minimum aus 12 Läufen); `noengine=1` misst nur das Datei-I/O
- `zip.py base cur`: Zipper-Seitenbänder bei fs/16 (Ton in der Bandmitte mit schwellendem Pegel)
- `lev.py <exe>`: Detektorpegel der S-Laute je Take (`levels=`)
