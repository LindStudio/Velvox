# EMBER sound measurements (2026-11-23)

Messen, was EMBER mit echten Takes macht (Klangtest `docs/products/EMBER_KLANG_TEST.md`).

- Ordner anlegen mit `data/takes/*.wav` (die 15 Klangtest-Takes) und optional `data/prods/*.wav` (Pro-DS-Renderings),
  diese Skripte hineinkopieren (`pip install numpy scipy soundfile`).
- `python run.py <EmberTests.exe> name:key=wert,key=wert ...` rendert alle Takes mit `EmberTests --process` und zeigt
  die Mittelwerte (S-Kern, Luft über dem S, S-Schwerpunkt, unberührte S, Vokale ...). Beispiel:
  `python run.py EmberTests.exe def: auto:auto=1 tief:freq=5000,thr=-35`
- `python grp.py` dieselben Werte nach Stimmlage (tiefe / mittlere / hohe S) und die AUTO-Werte je Ausschnitt.
- `freq.py` / `det.py`: Experimente zur S-Frequenz und zur Detektor-Neigung; `snips2.py <ziel>` schneidet Hörbeispiele.
