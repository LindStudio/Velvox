# Lizenzprüfung (Lemon Squeezy) – vorbereitet, noch AUS

Stand 2026-11-03. Code: `Source/Licensing/License.{h,cpp}`, Tests: `Tests/LicenseTests.cpp`,
Oberfläche: `Source/UI/SettingsOverlay.{h,cpp}` (Zeile „Lizenz“, Aktivieren-Fenster, `LicenceLock`).

## Regeln

- **Aus**, solange nicht mit `-DVOCALYPSE_LICENSING=ON` gebaut wird: keine Datei, kein Thread, kein Netz,
  keine Einstellungszeile. Der heutige Build ist so.
- **An:** 14 Tage Testversion ab dem ersten Start auf einem Computer, alles funktioniert.
- **Aktivieren:** Einstellungen → Lizenz → „Aktivieren“, Schlüssel aus der Bestell-Mail. Lemon Squeezy zählt
  die Computer (Aktivierungs-Limit im Produkt einstellen, Vorschlag: 2).
- **Aktiviert bleibt aktiviert**, auch offline. Online wird höchstens einmal pro Woche im Hintergrund
  nachgefragt; nur ein ausdrückliches „ungültig / deaktiviert / abgelaufen“ (z. B. nach Rückerstattung)
  sperrt wieder.
- **Gesperrt** (Testzeit vorbei oder Schlüssel ungültig): VOCALYPSE lässt die Vocal unbearbeitet durch
  (wie Bypass, Latenz bleibt) und zeigt eine Karte mit „Lizenzschlüssel eingeben“ und „Kaufen“.
  Projekte, Presets und Einstellungen werden nie verändert.
- **Freigeben:** Einstellungen → Lizenz → „Freigeben“ meldet diesen Computer ab (Umzug auf neuen PC).
  Die Testzeit beginnt dadurch nicht neu.
- Gespeichert wird in `%APPDATA%\Lind Audio\VOCALYPSE\VOCALYPSE.license` (signiert: eine von Hand
  bearbeitete Datei verliert den Schlüssel, die Testzeit startet nicht neu).
- Netz nur im Hintergrund-Thread (Zeitlimit 8 s), nie im Audio-Thread, nie beim Laden blockierend.

## Einschalten, wenn der Shop steht

1. Lemon Squeezy: Produkt anlegen, „License keys“ aktivieren, Aktivierungs-Limit setzen.
2. `Source/ProductInfo.h`: `lemonStoreId`, `lemonProductId` (aus dem Dashboard) und `buyUrl` (Checkout-Link)
   eintragen. Mit den IDs wird ein Schlüssel eines anderen Produkts abgelehnt.
3. Neu konfigurieren: `cmake -S . -B build -DVOCALYPSE_LICENSING=ON`, bauen, Tests, pluginval.
4. Mit einem echten Test-Schlüssel (Lemon Squeezy „Test mode“) aktivieren, freigeben, offline starten.

## Grenzen (bewusst)

Das ist eine Hürde für Gelegenheits-Kopierer, kein Schutz gegen Cracker: Wer die DLL patcht, kommt
vorbei. Mehr Aufwand (iLok, Obfuskierung) lohnt sich erst bei nennenswertem Umsatz. Uhr zurückstellen
verlängert die Testzeit nicht über den gespeicherten Start hinaus; Datei löschen startet sie neu – auch
das ist bewusst einfach gehalten.
