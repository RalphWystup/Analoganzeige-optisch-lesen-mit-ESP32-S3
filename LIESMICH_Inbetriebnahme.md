# Analoganzeige lesen – Version 2 (Stand 16.09.2026)

Optisches Auslesen eines analogen Zeigerinstruments (44C2, 0–1 mA) mit einem Freenove ESP32-S3 WROOM N8R8 und Kameramodul OV3660, vollständig auf dem Board. Dieses Paket enthält alles, was läuft: die Firmware (Quelltext und fertige Binärdatei), die PC-Brücke, die Werkzeuge des Workspace, die Offline-Referenzen mit Bildsätzen, das Manuskript und die Protokolle.

## Inhalt

| Ordner / Datei | Inhalt |
|---|---|
| `Firmware/ESP32S3_Zeiger_V2/` | Firmware Version 2, Arduino-Sketch in zehn Dateien (`ESP32S3_Zeiger_V2.ino` + `a_licht` … `j_verarbeite`), `sketch.yaml` für arduino-cli |
| `Firmware/bin/ESP32S3_Zeiger_V2.bin` | fertige Binärdatei (Baugate-Freigabe 16.09. 11:40 UTC), für OTA oder USB |
| `Firmware/bin/espota.py` | OTA-Werkzeug aus dem esp32-Kern (wird von der Brücke benutzt) |
| `Firmware/ESP32S3_Zeiger_Live_V1/`, `Firmware/bin/ESP32S3_Zeiger_Live_V1.bin` | Version 1 (11.–15.09.), nur zur Referenz |
| `PC/esp_zum_workspace.py` | Brücke auf dem PC im Heimnetz (Thonny, F5): Bilder + Status hoch, Befehle/OTA/Modbus/USB hinunter |
| `PC/bilder_holen.py` | Standbilder direkt vom Board holen (ohne Workspace) |
| `Workspace/empfaenger.py` | Empfänger (Port 8085): Bilder, Befehlswarteschlange, Dateiausgabe; Token in `.empfaenger_token` (selbst anlegen) |
| `Workspace/esp_befehl.py` | Befehl an den ESP über die Brücke: URL, `--ota`, `--usb`, `--modbus`, `--seriell`, `--selbstupdate` |
| `Workspace/firmware_bauen.py` | Baugate: `python3 firmware_bauen.py ESP32S3_Zeiger_V2` (Formatprüfung, Kommentarprüfung, -Werror, Freigabe nach `firmware/`) |
| `Workspace/kurve_v2.py` | Kurve 0 → max → 0 mit Prüfstrom und Modbus-Schreibsperre |
| `Workspace/skala_v2.py`, `zeiger_v2.py` | Offline-Referenz der Skalen- und Zeigererkennung (Python, numpy/Pillow), erzeugt annotierte Bilder |
| `Workspace/belichtungstest.py`, `modbus_test.py`, `live_blick.py`, `bildsatz.py` | Belichtungsraster, Modbus-Prüfung vom PC, Livebild-Tabelle, Bildsatz aufnehmen |
| `Bilder/44C2_satz_0916/` | Bildsatz 0 … 0,85 mA mit Status-JSON je Bild (Eingabe für skala_v2.py / zeiger_v2.py) |
| `Bilder/skala_v2_0916/`, `Bilder/belichtung_deckenlicht_ohne_led_0916/` | 76 annotierte Skalenbilder mit Tabelle; Belichtungsraster mit Tabelle |
| `Bilder/Manuskript/` | Bilder des Manuskripts |
| `Manuskript/MANUSKRIPT_Analoganzeige.pdf` (.md, .docx) | Manuskript, 27 Seiten: Version 2, Anbindung, Betrieb, Weg über Version 1, Lehren, Ausblick |
| `Manuskript/Archiv/` | Manuskript der ersten Version |
| `AKTUELL_LESEN.md`, `FORTSETZUNG_2026-09-11.md` | Einstiegsblatt und vollständiges Protokoll 11.–16.09. |

## Firmware aufspielen

Arduino IDE 2.3.x mit esp32-Kern 3.3.11: Board „ESP32S3 Dev Module", USB CDC On Boot Enabled, CPU 240 MHz, Flash QIO 80 MHz, Flash Size 8 MB, Partition „8M with spiffs (3MB APP/1.5MB SPIFFS)", PSRAM „OPI PSRAM", Upload Speed 921600, USB Mode „Hardware CDC and JTAG". Vor dem Übersetzen in `ESP32S3_Zeiger_V2.ino` WLAN-Name und -Passwort (`WIFI_SSID`, `WIFI_PASSWORT`) eintragen. Ordner `ESP32S3_Zeiger_V2` öffnen, übersetzen, per Kabel hochladen (Buchse „USB"). Danach geht jedes Update per Funk: Baugate `firmware_bauen.py`, dann `esp_befehl.py --ota ESP32S3_Zeiger_V2.bin` über die Brücke oder `espota.py -i <IP> -p 3232 -f ESP32S3_Zeiger_V2.bin` direkt.

Nach dem ersten Start: Bild kontrollieren (`http://<IP>/`), bei gespiegeltem Bild `http://<IP>/control?var=vflip&val=1` bzw. `hmirror`. Messbereich und Einheit setzen: `/control?var=bereich&val=1.0`, `/control?var=einheit&val=mA`; oder mit bekanntem Prüfstrom `/kal?wert=0.887` (Einpunkt, Bereich wird glatt gerundet). Das Gerät lernt die Referenz-Strichzahl (26 Striche, 6 Hauptstriche beim 44C2) selbst nach 30 übereinstimmenden Bildern.

Prüfstromquelle: GPIO 21 → 3300 Ω → Instrument (+), Instrument (−) → GND; Maximalstrom 0,887 mA (`/control?var=imax&val=…`). Sie wird nur zum Nachweis gebraucht; im Einbau genügt Messbereich und Einheit.

## Bedienung

- Webseite `http://<IP>/`: Messwert, Livebild mit Einzeichnung (Bogen orange, Teilstriche cyan, Hauptstriche magenta mit Wert, Ziffernkästen grün, Zeiger blau bis zum Schnittpunkt), Kalibrierung, Licht.
- `/status` (JSON), `/capture` (JPEG 640×480), Stream `http://<IP>:81/stream`.
- Licht: fest eingestellte Kamera, Regelgröße Weiß des Zifferblatts (Ziel 220); `/control?var=lichtregel&val=1|0`, `regelziel`, `regelledmax` (0 = LED abgedeckt), `aec`, `gain` (schalten die Regelung aus).
- Ansichten für die Diagnose: `/control?var=ansicht&val=0|1|2` (Farbe, Rotbild, Graubild), `overlay=0|1`.
- Gültig ist ein Wert nur, wenn Skala (Inlier ≥ 200, Referenzzahl, regelmäßige Teilung, Sättigung < 3 %) und Zeiger (≥ 40 Punkte) im selben Bild stimmen; sonst „unsicher", Register 4 = 0.

## Modbus TCP (Port 502, Adresse 1, FC3/4 lesen, FC16 Register 8–9 und 15, FC6 Register 15)

| Register | Inhalt |
|---|---|
| 0–1 | Messwert Float (High-Word zuerst), letzter gültiger Median |
| 2–3 | Zeigerwinkel am Schnittpunkt (Grad) Float |
| 4 | gültig 0/1 |
| 5 | Zeigerpunkte auf der Geraden |
| 6 | Skalengüte × 100 |
| 7 | Bildzähler |
| 8–9 | Prüfstrom Float (lesen/schreiben) |
| 10 | Bilddauer ms |
| 11 | Weiß des Zifferblatts |
| 12 | LED-Stufe |
| 13 | Messbereich gesetzt 0/1 |
| 14 | Skala in diesem Bild gültig 0/1 |
| 15 | Prüfstrom in µA ganzzahlig (lesen/schreiben) |

Trendows: Element Lan-IO (IP, Port 502, Wartezeit 100 ms, Modbus, normale Word-Order); Kanaltabelle AI(3)×1 @0 Messwert, WI(3)×4 @4 (gültig, Zeigerpunkte, Skalengüte, Bildzähler), WO(16)×1 @15 Prüfstrom µA, empfohlen WI(3)×1 @14 Skala gültig. Schreibsperre für Kurven: `/control?var=modbusschreiben&val=0` (kurve_v2.py setzt sie selbst).

## PC-Brücke

`PC/esp_zum_workspace.py` in Thonny öffnen, oben `ESP` (IP des Geräts), `WORKSPACE` (Adresse des Empfängers) und `TOKEN` (Inhalt von `.empfaenger_token` des Empfängers) eintragen, F5. Das Skript läuft in einer Schleife (2 s), überträgt Bilder und Status und führt Befehle aus dem Workspace aus. Ohne Workspace: `bilder_holen.py` holt Standbilder direkt.

## Offline-Referenz

`python3 skala_v2.py <Bild.jpg>` zeichnet Bogen, Striche, Hauptstriche und Ziffernkästen ein; `python3 zeiger_v2.py` wertet den Bildsatz `Bilder/44C2_satz_0916/` aus (Soll aus dem Dateinamen, Ist aus dem Schnittpunkt). Beide Skripte brauchen nur numpy und Pillow (matplotlib für Tabellenbilder).
