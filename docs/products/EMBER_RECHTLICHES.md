# EMBER – Rechtliches und Vertrieb vor dem Release (notiert 2026-11-23, NICHT erledigt)

Aus dem Audit vom 23.11.2026. Jakob: „das rechtliche notieren aber nicht machen“. Im Code ist hierzu nichts geändert.

## Muss vor dem Release
1. **EULA fertig machen** – `Products/Ember/Installer/EULA.txt`
   - Zeile 1 steht noch „ENTWURF/DRAFT“.
   - Postadresse fehlt (Platzhalter in Zeile ~94 und ~184).
   - Der Text ist für ein Bezahlprodukt geschrieben: Haftung „bis zum Kaufpreis“ (~66/157), „Verkauf“. EMBER ist gratis.
   - Es fehlt eine Klausel zur Online-Aktivierung und zur wöchentlichen Prüfung (Datenübertragung).
   - Anwaltlich prüfen lassen.
2. **Datenschutzerklärung + Impressum** (DSGVO Art. 13; österreichische Impressumspflicht) für Website, Checkout und EULA.
   - Das Plugin kontaktiert `api.polar.sh` (USA) bei der Aktivierung und danach alle 7 Tage (`Source/Licensing/License.h:31,84`).
   - Gesendet werden der Lizenzschlüssel, die Organisations-ID, die Aktivierungs-ID und der **Windows-Computername** als Label (`License.cpp:302`).
   - Die E-Mail-Adressen sammelt Polar. Polar ist Auftragsverarbeiter, es findet eine US-Übermittlung statt.
   - Das Handbuch (`EMBER_Manual.html` ~214) sagt nur „braucht einmal Internet“; die Wochenprüfung wird nicht erwähnt.
3. **Computername nicht mehr senden** (Datenschutz): Er enthält oft den echten Namen („Jakobs-PC“). Statt dessen ein festes Label „EMBER“ oder einen Hash senden – oder den Computernamen offenlegen. (Code-Änderung in `License.cpp:302`, bewusst noch nicht gemacht.)
4. **Support-Adresse und Produktseite müssen existieren**: `support@lind-audio.com` (`License.cpp:43`, EULA, Shop-Text) und `https://lind-audio.com/ember` (`EmberEditor.cpp` ~1130, `EMBER.iss:26-27`) sind fest im Plugin eingebaut.
5. **Code-Signing**: Installer, `.vst3` und Standalone-EXE sind nicht signiert (`make-installer.ps1:11-12`). Folge: SmartScreen zeigt „Unbekannter Herausgeber“, dazu kommt die Admin-Abfrage (`EMBER.iss:33`). Zertifikat kaufen (OV/EV) oder die Warnung im Handbuch erklären.
6. **Polar: Aktivierungslimit 2** beim Lizenz-Benefit setzen. Ohne Limit lehnt Polar „activate“ mit 403 ab, und die EULA erlaubt 2 Computer.

## Sollte
- **Deinstallation**: Die Aktivierung bei Polar bleibt belegt, und `%APPDATA%\Lind Audio\EMBER\EMBER.license` bleibt liegen (`EMBER.iss:89-91`). Im Handbuch erklären: „vorher ≡ > Lizenz > Computer freigeben“.
- **Release-Build**: Mit `-DEMBER_LICENSING=ON` bauen. Ein `-NoLicence`-Installer bekommt denselben Dateinamen – Verwechslungsgefahr beim Hochladen.
- **Doku veraltet**: `docs/products/EMBER.md` (~184-185) nennt noch Sandbox und offene IDs. In `EmberLicence.h` stehen bereits die Live-IDs, `polarSandbox = false`.

## Technisch geprüft, in Ordnung (keine Aktion)
- TLS-Zertifikatsprüfung aktiv.
- JSON-Antworten werden robust geparst.
- Keine Geheimnisse im Repo.
- Presets und Projektzustand können keine URL und keinen Befehl einschleusen.
- Uhr-Manipulation wird abgefangen.
- Netzfehler sperren nicht.
- Die Lizenzdatei ist leicht fälschbar. Für ein Gratis-Produkt ist das bewusst akzeptiert.
