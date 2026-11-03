# VOCALYPSE – alle offenen Punkte mit Infos

Stand 3. November 2026. Preise wie auf den Seiten der Anbieter am 3.11.2026 (ohne MwSt., USD wo so angegeben).
✅ = heute erledigt · 🛠 = von mir vorbereitet, du musst noch etwas tun · 👤 = deine Entscheidung / dein Account

---

## A. Bevor du verkaufen kannst

### 1. Lizenzprüfung (Kopierschutz) 🛠
- **Stand:** fertig eingebaut, aber **ausgeschaltet** (`docs/LICENSING.md`). 14 Tage Testversion, dann Aktivierung
  mit dem Schlüssel aus der Bestell-Mail. Aktiviert bleibt aktiviert, auch offline; einmal pro Woche wird im
  Hintergrund nachgefragt. Gesperrt = die Vocal geht unbearbeitet durch, eine Karte zeigt „Schlüssel eingeben / Kaufen“.
  Projekte und Einstellungen werden nie angetastet. „Freigeben“ in den Einstellungen für den Umzug auf einen neuen PC.
- **Was du tun musst:** Lemon-Squeezy-Konto + Produkt mit „License keys“ (Aktivierungs-Limit: Vorschlag 2 Computer).
  Mir dann Store-ID, Produkt-ID und Checkout-Link geben → ich schalte es mit einer Zeile ein und teste mit einem
  Test-Schlüssel.
- **Ehrlich:** Das hält Gelegenheits-Kopierer ab, keine Cracker. Mehr (iLok o. ä.) lohnt erst bei viel Umsatz.

### 2. Shop: Lemon Squeezy 👤
- **Kosten:** keine Grundgebühr. Pro Verkauf **5 % + 0,50 USD**, dazu **+1,5 %** bei Käufern außerhalb der USA
  und **+1,5 %** bei PayPal. Auszahlung auf ein Konto außerhalb der USA: 1 % pro Auszahlung (Stripe).
  Lizenzschlüssel sind inklusive.
- **Beispiel:** Preis 39 € an einen Kunden in Deutschland, PayPal → etwa 8 % + 0,50 $ ≈ **3,60 € Gebühr**.
- **Wichtig:** Lemon Squeezy ist „Merchant of Record“: Sie verkaufen an den Kunden, kümmern sich um die
  EU-Mehrwertsteuer, Rechnungen, Rückerstattungen und das Widerrufsrecht im Checkout. Du verkaufst rechtlich an
  Lemon Squeezy. Das spart dir enorm viel Steuer-Bürokratie.
- **Empfehlung:** Lemon Squeezy nehmen (Alternative mit gleichem Modell: Paddle). Preisidee: 39–49 € Einführungspreis
  für die Newsletter-Liste (Rabattcode), danach 59–69 €.

### 3. JUCE-Lizenz (das Gerüst, auf dem das Plugin gebaut ist) 👤
- **Kosten (juce.com, 3.11.2026):**
  - **Starter: kostenlos** bis **20.000 USD Umsatz pro Jahr**, alle Funktionen, kein Pflicht-Logo.
  - **Indie:** 40 USD/Monat oder **800 USD einmalig** bis 300.000 USD Umsatz/Jahr.
  - **Pro:** 175 USD/Monat oder 3.500 USD einmalig, ohne Umsatzgrenze.
- **Empfehlung:** Mit **Starter (gratis)** starten – auf juce.com registrieren. Sobald du Richtung 20.000 USD/Jahr
  gehst, auf **Indie einmalig (800 USD)** wechseln. Wir nutzen JUCE 8; auf juce.com ist inzwischen JUCE 9 aktuell –
  vor dem Release kurz die Bedingungen für JUCE 8 prüfen (gleiche Stufen).

### 4. Installer 🛠 (später, wie du gesagt hast)
- **Wie:** Inno Setup (kostenlos). Ergibt `VOCALYPSE-Setup.exe`: installiert VST3 + Standalone, räumt alte
  Headliner/Velvox-Reste weg, hat einen Deinstaller. Etwa ein halber Tag Arbeit für mich, sobald du es willst.
- Bis dahin geht auch ein ZIP mit `VOCALYPSE.vst3` und einer Kurzanleitung.

### 5. Signatur (gegen „Unbekannter Herausgeber“) 👤
- **Ohne Signatur:** Windows SmartScreen warnt beim Installer („Der Computer wurde geschützt“ → „Trotzdem ausführen“).
  Bei einem ZIP mit der `.vst3` merkt man meist nichts.
- **Möglichkeiten:**
  - **Microsoft Azure Artifact Signing:** 9,99 USD/Monat. Ob Einzelpersonen in Österreich zugelassen sind, war auf
    Microsofts Seiten nicht zu finden (früher nur Firmen bzw. Einzelpersonen in USA/Kanada) → bei Interesse prüfe
    ich es mit dir.
  - **Klassisches Zertifikat (OV)** von Certum, Sectigo o. ä.: grob 100–400 € pro Jahr, Schlüssel liegt heute
    immer auf einem USB-Token oder in der Cloud.
- **Empfehlung:** zum Start ohne Signatur (ZIP), wenn der Installer kommt → Signatur dazunehmen.

### 6. Icon 👤
- Das Wellen-Icon im Plugin und das Favicon der Website sind Platzhalter. Du wolltest „später schauen“.
  Vorschlag für die nächste Runde: Icon aus dem **V** des Schriftzugs (Metal-Mania-V mit Feuerverlauf),
  dazu eine Variante mit Memphys Kopf. Vorschläge N01–N12 liegen in `VOCALYPSE_Logo\Icon_Vorschlaege`.

---

## B. Klang (Auto Mix)

### 7. Dein Hörtest in FL 👤
- Neuer Auto Mix (lauterer Hall/Delay, 5 Stile, Feuer-Regler) – bitte anhören, am besten mit deinen eigenen Takes.
- Hör-Kit: `%USERPROFILE%\dev\velvox-bench\2026-11-03\HOERTEST_stile\`.

### 8. EQ der Stile ✅ (deutlich besser, noch nicht ganz am Ziel)
- **Problem:** Die EQ-Verschiebung der Stile kam nur zu ~⅓ an. Ursache: Die Anhebung war auf 4 dB gedeckelt, die
  normale Korrektur der Stimme hat diese 4 dB schon verbraucht.
- **Gelöst:** Der Stil-Anteil kommt jetzt **zusätzlich** zur Korrektur-Grenze (nicht bei verrauschten Aufnahmen).
  Aggressiv hat jetzt im Mittel +1,3 dB mehr Präsenz als Druckvoll (vorher +0,35; Ziel +3). Floaty bekommt
  kaum mehr Luft, weil die meisten Testaufnahmen dafür zu verrauscht/zischend sind – Floaty unterscheidet sich jetzt
  vor allem über den Raum (Punkt 10).

### 9. Rap · Aggressiv: zu wenig verdichtet – teilweise
- **Geändert:** Wenn der Kompressor das Phrasen-Ziel nicht schafft, bekommt der Level Ride mehr Spielraum
  (+10 / −12 dB statt +6 / −8 dB), nur wenn es messbar näher ans Ziel kommt. Wirkung klein (−0,2 bis −0,5 dB).
- **Befund:** Das Ziel (1–3 dB zwischen Phrasen) ist vermutlich zu streng: Die fertigen Profi-Vocals im Test-Set
  behalten selbst rund 12 dB Crest. Vorschlag: nach deinem Hörtest entscheiden, ob Aggressiv noch dichter klingen soll –
  dann stelle ich das Ziel nach Gehör ein statt nach Zahl.

### 10. Floaty und Ballade · Warm klangen räumlich gleich ✅
- **Gelöst:** Der Stil färbt jetzt auch den Raum: **Floaty** = offene, helle Hall mit späterem Einsatz (+20 ms),
  **Warm** = dunklere, kleinere Plate, **Aggressiv** = enger Raum.

### 11. SPACE-Regler stand nach dem Auto Mix am Anschlag ✅
- **Gelöst:** Der Regler deckt jetzt 0–70 % Hall ab (vorher 0–30 %). Nach einem Auto Mix mit 42 % Hall steht er
  bei 60 % und du kannst nach oben und unten verfeinern. Alte Projekte klingen unverändert.

### 12. Fertige (schon gemischte) Vocals 👤
- Der Auto Mix legt auf eine fertige Vocal trotzdem Hall/Delay. Sicher erkennen lässt sich „schon nass“ nur mit
  Risiko (trockene Vocals in halligen Räumen sehen ähnlich aus). **Empfehlung:** so lassen, im Tutorial/Website sagen:
  „Gib die trockene Aufnahme hinein.“ Die Analyse erkennt deutlichen Raum schon jetzt und lässt dann den Hall weg.

### 13. Deine Acapellas für das Ziel 👤
- Wenn du trockene + fertige Versionen eigener Songs hast (gleicher Take), rechne ich damit das Klangziel neu.
  Das ist besser als SONOVOX (die dürfen wir nur testen, nicht verwenden/weitergeben).

### 14. Analyse dauert mit laufendem FL 35–42 s statt 25 s
- Ursache: FL belegt den Prozessor, die Analyse rendert bis zu ~20 Varianten. Möglich wäre weniger Varianten oder
  paralleles Rechnen. **Empfehlung:** erst nach deinem Hörtest anfassen – jede Abkürzung kann den Klang kosten.

### 15. Saturation 👤
- Steht in deiner TODO als „verzerrt stark“. Seit 0.7 sind Drive/Mix begrenzt; bitte in FL einmal Warmth / Punch
  hochziehen und sagen, ob es noch zu viel ist.

---

## C. Bedienung & Sonstiges

### 16. Sprache ✅ (gab es schon)
- Deutsch/Englisch ist seit 1.0 komplett drin: automatisch nach Windows-Sprache, umschaltbar im Zahnrad-Menü.

### 17. Mac / AU-Version 👤
- **Was es braucht:** einen Mac (z. B. Mac mini M4, ab ca. 700 €) zum Testen und das **Apple Developer Program
  (99 USD/Jahr)** für die Notarisierung (sonst blockiert macOS das Plugin).
- Der Code ist größtenteils schon plattformunabhängig (JUCE); Bauen ginge auch über GitHub-Actions-Macs,
  testen aber nur auf einem echten Mac. **Empfehlung:** nach dem Windows-Release.

### 18. Module im finalen Design
- Du wolltest „jedes Modul später final bauen“. Offen, kein Termin.

### 19. GitHub 🛠
- Hochgeladen (alle aktuellen Branches). **Umbenennen** in „Vocalypse“ geht auf diesem PC nicht automatisch
  (kein GitHub-Programm installiert): github.com/LindStudio/Velvox → Settings → Repository name → „Vocalypse“ →
  Rename. Danach sag Bescheid, dann stelle ich die Adresse lokal um (alte Links leitet GitHub weiter).

---

## D. Website

### 20. Newsletter-Anbieter 👤
- **Empfehlung: Brevo** (Firma in Frankreich, DSGVO-freundlich, Double-Opt-in eingebaut, kostenloser Plan mit
  300 Mails/Tag). Alternative MailerLite (kostenlos bis zu einer kleinen Abonnentenzahl).
- Du: Konto anlegen, Formular „Double Opt-in“ erstellen, mir die Formular-Adresse schicken → ich trage sie ein
  (`NEWSLETTER_ACTION`) und fülle den Datenschutz-Platzhalter aus.

### 21. Hosting & Domain 👤
- **Achtung:** Vercel „Hobby“ (gratis) ist nur für **nicht-kommerzielle** Seiten erlaubt; Vercel Pro = 20 USD/Monat.
- **Empfehlung:** **Cloudflare Pages** oder **Netlify** – beide kostenlos und kommerziell erlaubt, die Seite ist
  rein statisch. Domain (z. B. `vocalypse.com` / `.at`, falls frei) ca. 10–40 €/Jahr.

### 22. Datenschutz-Platzhalter 🛠
- Drei Lücken in `datenschutz.html`: Hosting-Anbieter, Speicherdauer der Server-Logs (eingetragen: 30 Tage),
  Löschfrist der Liste (12 Monate). Füllt sich, sobald 20 und 21 entschieden sind.

### 23. Impressum & Gewerbe 👤
- Prüfen, ob „Lind Audio“ im Impressum so stimmt.
- **Gewerbe:** Software verkaufen ist in Österreich in der Regel ein freies Gewerbe (IT-Dienstleistungen bzw.
  Handel). Kostenlos fragen: **WKO Gründerservice**. Steuer: Kleinunternehmerregelung bis 55.000 € Umsatz;
  mit Lemon Squeezy als Merchant of Record verkaufst du an eine US-Firma – mit Steuerberater:in kurz klären.

### 24. AGB / Widerruf / Lizenzbedingungen 🛠
- Kaufvertrag, Widerruf und Rechnungen laufen über Lemon Squeezy (deren Bedingungen gelten beim Kauf).
- Du brauchst noch **Lizenzbedingungen für das Plugin (EULA)**: Entwurf liegt bei (`docs/EULA_ENTWURF.md`) –
  bitte rechtlich prüfen lassen, bevor er verwendet wird.

### 25. Hörbeispiele 👤
- Du lieferst echte Vocals. Austausch: Dateien in `assets/audio/` ersetzen (Namen siehe `assets/audio/LIESMICH.md`),
  ich mische sie mit VOCALYPSE in den 5 Stilen und baue sie ein.

### 26. Plugin-Screenshots ✅
- Neu aufgenommen mit den Feuer-Farben.

### 27. Belohnung für Rezensionen (Idee) 👤
- **Wichtig, rechtlich:** In der EU darfst du **nicht nur gute** Rezensionen belohnen (unlautere Geschäftspraxis,
  Omnibus-Richtlinie). Erlaubt: **jede ehrliche** Rezension belohnen und das offenlegen („für eine Rezension gab es
  ein Preset-Pack“).
- **Vorschlag:** Nach 14 Tagen eine Mail: „Wie klingt's? Schreib eine ehrliche Rezension (KVR, Plugin Boutique,
  Google) – als Dankeschön bekommst du das Memphy-Preset-Pack.“

---

## Messung nach den heutigen Änderungen

11 SONOVOX-Takes je Stil, Änderung durch VOCALYPSE (Median, dB, Ausgang minus Eingang):

| Stil | Höhen | Präsenz | Körper | Tiefmitten | Luft |
|---|---|---|---|---|---|
| Party · Druckvoll | 3,9 | 4,0 | 2,2 | −1,0 | 3,5 |
| Rap · Aggressiv (vorher) | 3,9 | 4,4 | 2,6 | −0,9 | 3,8 |
| **Rap · Aggressiv (jetzt)** | **4,7** | **5,4** | 2,3 | −1,1 | 2,8 |
| Party · Floaty | 3,9 | 3,6 | 2,1 | −0,9 | 4,4 |
| Ballade · Warm | 3,3 | 3,3 | 1,9 | −1,2 | 1,2 |
| Pop · Natürlich | 3,3 | 3,7 | 1,5 | −1,4 | 4,5 |

Dazu neu beim Raum: Floaty = offene Hall (Größe 80 %, hell, +20 ms Pre-Delay), Warm = dunkle Plate (Größe 45 %),
Aggressiv = enger Raum (Größe 40 %).
