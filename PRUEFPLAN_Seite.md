# Prüfplan — Seite „Analoganzeige lesen“ (Fassung 1.0, 29.09.2026)

Prof. Dr.-Ing. Ralph Wystup M.Sc. — erstellt mit KI und Agent (Claude Code, Anthropic)

Die Seite trägt die Erkennung der Firmware Version 2 als JavaScript (`analoganzeige_lesen.js`, Zeile für Zeile nach `skala_v2.py` und `zeiger_v2.py`), ein gezeichnetes Instrument als Simulation, den Bildsatz vom Gerät als Nachweis und das Manuskript als Dokumentation. Jede Zeile hat eine Schranke.

| Nr. | Kriterium | Prüfmittel | Schranke | Ergebnis |
|:--|:--|:--|:--|:--|
| P1 | Die JavaScript-Erkennung liest den Bildsatz vom Gerät (0 … 0,85 mA, zehn Bilder) | `pruefe_lesen.mjs` (node, RGB roh aus den JPEGs) | Abweichung ≤ 0,020 mA je Bild, alle gültig, 26/6 Striche | größte Abweichung 0.0133 mA, mittlere 0.0044 mA, gültig 10 von 10 (Manuskript offline: 0,011 / 0,004 mA) |
| P2… | keine Konsolenfehler | `pruefe_seite.mjs` (Playwright, Chromium) | siehe Text | ok |
| P2… | Werkzeug vor Text: Knopf „Lesen“ bei 214 px | `pruefe_seite.mjs` (Playwright, Chromium) | siehe Text | ok |
| P2… | gezeichnetes Instrument bei 0,50 mA: gelesen 0.498 mA, 26/6 Striche, 742 Zeigerpunkte, gültig true | `pruefe_seite.mjs` (Playwright, Chromium) | siehe Text | ok |
| P2… | Kurve 0 … 1 mA: 21 Punkte, größte Abweichung 14.0 µA, gültig 21 | `pruefe_seite.mjs` (Playwright, Chromium) | siehe Text | ok |
| P2… | Bildsatz vom Gerät: 10 Bilder, größte Abweichung Browser 13.3 µA, gültig 10 | `pruefe_seite.mjs` (Playwright, Chromium) | siehe Text | ok |
| P2… | Dokumentation in der Seite: Bogen, Zeigergerade, Schnittpunkt, Modbus | `pruefe_seite.mjs` (Playwright, Chromium) | siehe Text | ok |
| P2… | Kopf: Fassung 1.0 · 29.09.2026 · Prof. Dr.-Ing. Ralph Wystup M.Sc. | `pruefe_seite.mjs` (Playwright, Chromium) | siehe Text | ok |

Stand: 2026-09-29T16:48 · 0 Beanstandung(en). Bildschirmfotos angesehen (Instrument mit Einzeichnung, Bildsatz-Tabelle).
