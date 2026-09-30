#!/usr/bin/env python3
"""Zeigerstellung eines analogen Messgeräts aus einem Bild lesen (Python-Referenz für die spätere C-Fassung).

Verfahren (Schritt 2, Ansatz aus dem Einstiegsblatt):
  1. Graubild. Kalibrierung liefert Drehpunkt (cx, cy), ein Radiusband [r_innen, r_aussen] zwischen Nabe
     und Skalenring sowie Skalenmarken (Winkel -> Wert).
  2. Polarabtastung: für jeden Winkel (Schritt 0,25°) die mittlere Helligkeit entlang des Radiusbands.
     Der Zeiger ist das dunkelste radiale Tal dieses Profils.
  3. Profil glätten (Kasten so breit wie der Zeiger), Minimum suchen, Parabel durch drei Punkte -> Winkel
     unter der Schrittweite. Suche nur im Skalenbereich (Marken ± Rand).
  4. Winkel -> Wert: stückweise linear zwischen den Marken (deckt gestauchte Skalen ab).

Winkel: mathematisch, 0° = nach rechts, positiv gegen den Uhrzeigersinn, y-Achse des Bildes zeigt nach unten.
Nur numpy + Pillow, alle Abtastungen mit nächstem Nachbarn (direkt nach C übertragbar).

Aufruf:  python3 zeiger_lesen.py BILD.jpg --kal kalibrierung.json [--zeige]
"""
import sys, json, math, argparse, pathlib
import numpy as np
from PIL import Image, ImageDraw

SCHRITT_GRAD = 0.25          # Winkelauflösung der Abtastung
RAND_GRAD = 8.0              # Suchbereich über die äußersten Marken hinaus
BEZUG_GRAD = 12.0            # halbe Fensterbreite des gleitenden Medians für die Bezugshelligkeit


def lade_grau(pfad):
    return np.asarray(Image.open(pfad).convert('L'), dtype=np.float32)


def zeigerbild(pfad_oder_rgb, kanal='grau'):
    """Bild, in dem der Zeiger dunkel ist.
    kanal 'grau': Graubild (dunkler Zeiger auf heller Skala).
    kanal 'rot' : Rotanteil R - max(G, B); roter Zeiger wird dunkel, schwarze Striche/Schrift und weiße Fläche
                  werden hell -> Teilstriche und Beschriftung stören nicht mehr. Auf dem ESP32 aus RGB565 billig."""
    if isinstance(pfad_oder_rgb, (str, pathlib.Path)):
        im = Image.open(pfad_oder_rgb)
        if kanal == 'grau':
            return np.asarray(im.convert('L'), dtype=np.float32)
        rgb = np.asarray(im.convert('RGB'), dtype=np.float32)
    else:
        rgb = pfad_oder_rgb.astype(np.float32)
        if rgb.ndim == 2:
            return rgb
    if kanal == 'rot':
        rotheit = rgb[..., 0] - np.maximum(rgb[..., 1], rgb[..., 2])
        return 255.0 - np.clip(rotheit, 0, 255)
    return rgb.mean(axis=2)


def kreis_durch_punkte(punkte):
    """Kreis (cx, cy, R) im Sinne kleinster Quadrate durch >= 3 Punkte (algebraischer Fit nach Kåsa)."""
    P = np.asarray(punkte, dtype=float)
    A = np.c_[2 * P[:, 0], 2 * P[:, 1], np.ones(len(P))]
    b = (P ** 2).sum(axis=1)
    (cx, cy, c), *_ = np.linalg.lstsq(A, b, rcond=None)
    return float(cx), float(cy), float(math.sqrt(c + cx * cx + cy * cy))


def marken_einrasten(grau, kal, fenster_grad=1.2, strichband=(0.93, 0.985)):
    """Markenwinkel auf die Mitte des nächsten Teilstrichs schieben: im Ring der Teilstriche (Anteil des
    Skalenradius) das dunkelste Winkeltal innerhalb ±fenster_grad um den Klick, mit Parabelverfeinerung.
    Gleicht ungenaue Klicks aus; der Klick muss näher am richtigen Strich liegen als am Nachbarn."""
    cx, cy = kal['drehpunkt']; R = kal['skalenradius']
    schritt = 0.05
    winkel, profil = polar_profil(grau, cx, cy, strichband[0] * R, strichband[1] * R, schritt=schritt, teile=1)
    p = np.where(np.isnan(profil), np.nanmax(profil), profil)
    n = len(winkel)
    for m in kal['marken']:
        rel = _entrollen_array(winkel, m['winkel']) - m['winkel']
        idx = np.where(np.abs(rel) <= fenster_grad)[0]
        if len(idx) < 3:
            continue
        i = idx[np.argmin(p[idx])]
        d0, d1, d2 = p[(i - 1) % n], p[i], p[(i + 1) % n]
        nenner = d0 - 2 * d1 + d2
        delta = float(np.clip(0.5 * (d0 - d2) / nenner, -1, 1)) if abs(nenner) > 1e-9 else 0.0
        m['winkel_klick'] = m['winkel']
        m['winkel'] = (winkel[i] + delta * schritt) % 360.0
    return kal


def kalibrierung_aus_marken(marken_xy_wert, band=(0.55, 0.92), einheit='A', zeigerbreite_px=6.0, kanal='grau',
                            bildgroesse=None, grau=None):
    """Drehpunkt aus dem Kreis durch die angeklickten Skalenmarken (für verdeckte Drehpunkte);
    Radiusband als Anteil des Skalenradius. Mit `grau` (Graubild) werden die Marken auf die Teilstriche eingerastet."""
    cx, cy, R = kreis_durch_punkte([(x, y) for x, y, _ in marken_xy_wert])
    kal = kalibrierung_aus_punkten(cx, cy, band[0] * R, band[1] * R, marken_xy_wert, einheit, zeigerbreite_px, bildgroesse)
    kal['skalenradius'] = R; kal['kanal'] = kanal
    if grau is not None:
        marken_einrasten(grau, kal)
    return kal


def winkel_von_punkt(cx, cy, x, y):
    """Winkel eines Bildpunkts um den Drehpunkt in Grad, mathematische Orientierung."""
    return math.degrees(math.atan2(-(y - cy), x - cx)) % 360.0


def kalibrierung_aus_punkten(cx, cy, r_innen, r_aussen, marken_xy_wert, einheit='A', zeigerbreite_px=6.0, bildgroesse=None):
    """marken_xy_wert: Liste (x, y, wert) angeklickter Skalenmarken."""
    marken = [{'x': float(x), 'y': float(y), 'winkel': winkel_von_punkt(cx, cy, x, y), 'wert': float(w)} for x, y, w in marken_xy_wert]
    return {'drehpunkt': [float(cx), float(cy)], 'radius_innen': float(r_innen), 'radius_aussen': float(r_aussen),
            'zeigerbreite_px': float(zeigerbreite_px), 'einheit': einheit, 'kanal': 'grau', 'marken': marken,
            'bildgroesse': list(bildgroesse) if bildgroesse else None}


def _entrollen(winkel, bezug):
    """Winkel so darstellen, dass er in (bezug-180, bezug+180] liegt (kein Sprung bei 0/360)."""
    return bezug + ((winkel - bezug + 180.0) % 360.0) - 180.0


def marken_kette(kal):
    """Markenwinkel fortlaufend (ohne 0/360-Sprung) und Werte, in der Reihenfolge der Kalibrierung."""
    m = kal['marken']
    if len(m) < 2:
        raise ValueError('mindestens zwei Skalenmarken nötig')
    w = [m[0]['winkel']]
    for k in range(1, len(m)):
        w.append(_entrollen(m[k]['winkel'], w[-1]))
    return np.array(w), np.array([q['wert'] for q in m])


def winkel_zu_wert(winkel, kal):
    """Stückweise lineare Kennlinie; außerhalb der äußersten Marken linear verlängert."""
    w, v = marken_kette(kal)
    x = _entrollen(winkel, w[0])
    if w[-1] < w[0]:                      # Skala läuft im Uhrzeigersinn -> Winkel fallen
        w, v = w[::-1], v[::-1]
    if x <= w[0]:
        return float(v[0] + (x - w[0]) * (v[1] - v[0]) / (w[1] - w[0]))
    if x >= w[-1]:
        return float(v[-1] + (x - w[-1]) * (v[-1] - v[-2]) / (w[-1] - w[-2]))
    return float(np.interp(x, w, v))


def polar_profil(g, cx, cy, r_innen, r_aussen, schritt=SCHRITT_GRAD, teile=3):
    """Helligkeit je Winkel entlang des Radiusbands (nächster Nachbar).
    Das Band wird in `teile` radiale Abschnitte geteilt; je Winkel wird der dunkelste Abschnitt verworfen
    und der Rest gemittelt. Der Zeiger ist in allen Abschnitten dunkel und bleibt dunkel; Schrift oder
    Teilstriche, die nur einen Abschnitt berühren, fallen heraus (wichtig für schwarze Zeiger)."""
    winkel = np.arange(0.0, 360.0, schritt)
    th = np.deg2rad(winkel)
    n_r = max(8 * teile, int(round(r_aussen - r_innen)))
    r = np.linspace(r_innen, r_aussen, n_r)
    # Pixel i deckt den Bereich [i, i+1) ab, sein Mittelpunkt liegt bei i+0,5: daher floor statt round.
    X = np.floor(cx + np.outer(np.cos(th), r)).astype(int)
    Y = np.floor(cy - np.outer(np.sin(th), r)).astype(int)
    h, b = g.shape
    gueltig = (X >= 0) & (X < b) & (Y >= 0) & (Y < h)
    X = np.clip(X, 0, b - 1); Y = np.clip(Y, 0, h - 1)
    werte = g[Y, X]
    werte[~gueltig] = np.nan
    if teile <= 1:
        return winkel, np.nanmean(werte, axis=1)
    grenzen = np.linspace(0, n_r, teile + 1).astype(int)
    with np.errstate(all='ignore'):
        mittel = np.stack([np.nanmean(werte[:, grenzen[k]:grenzen[k + 1]], axis=1) for k in range(teile)], axis=1)
    # dunkelsten Abschnitt verwerfen (dort sitzt eine Schrift oder ein Strich), die übrigen mitteln
    mittel = np.sort(np.where(np.isnan(mittel), np.inf, mittel), axis=1)[:, 1:]
    mittel = np.where(np.isinf(mittel), np.nan, mittel)
    return winkel, np.nanmean(mittel, axis=1)


def zeiger_winkel(winkel, profil, kal, schritt=SCHRITT_GRAD):
    """Dunkelstes Tal im Suchbereich mit Parabelverfeinerung. Liefert (winkel, sicherheit, glatt, suchmaske)."""
    w, _ = marken_kette(kal)
    lo, hi = min(w) - RAND_GRAD, max(w) + RAND_GRAD
    rel = _entrollen_array(winkel, 0.5 * (lo + hi))
    maske = (rel >= lo) & (rel <= hi)
    # Zeigerbreite in Winkelschritten am mittleren Radius
    r_mitte = 0.5 * (kal['radius_innen'] + kal['radius_aussen'])
    breite_grad = math.degrees(kal['zeigerbreite_px'] / r_mitte)
    n_kern = max(1, int(round(breite_grad / schritt)))
    kern = np.ones(n_kern) / n_kern
    p = np.where(np.isnan(profil), np.nanmedian(profil), profil)
    glatt = np.convolve(np.concatenate([p[-n_kern:], p, p[:n_kern]]), kern, mode='same')[n_kern:-n_kern]
    # Bezugshelligkeit lokal: gleitender Median über ±BEZUG_GRAD, folgt Helligkeitsverläufen und Schatten,
    # nicht aber dem schmalen Zeiger (Fenster >> Zeigerbreite). Unabhängig von Belichtung und Blende.
    dunkel = gleitender_median(glatt, int(round(BEZUG_GRAD / schritt))) - glatt
    dunkel_such = np.where(maske, dunkel, -np.inf)
    i = int(np.argmax(dunkel_such))
    n = len(winkel)
    d0, d1, d2 = dunkel[(i - 1) % n], dunkel[i], dunkel[(i + 1) % n]
    nenner = d0 - 2 * d1 + d2
    delta = 0.5 * (d0 - d2) / nenner if abs(nenner) > 1e-9 else 0.0
    delta = float(np.clip(delta, -1, 1))
    wink = (winkel[i] + delta * schritt) % 360.0
    # Sicherheit = Tiefe des Tals / Rauschen des ungeglätteten Profils (robuste Streuung von Profil − geglättet)
    rest = (p - glatt)[maske]
    rauschen = np.median(np.abs(rest - np.median(rest))) * 1.4826 + 1e-3
    sicherheit = float(d1 / rauschen)
    return wink, sicherheit, glatt, maske


def gleitender_median(p, halbfenster):
    """Gleitender Median über 2*halbfenster+1 Stützstellen, zyklisch (Winkel 0/360 hängen zusammen)."""
    n = len(p); k = halbfenster
    erw = np.concatenate([p[-k:], p, p[:k]])
    fenster = np.lib.stride_tricks.sliding_window_view(erw, 2 * k + 1)
    return np.median(fenster, axis=1)[:n]


def _entrollen_array(winkel, bezug):
    return bezug + ((winkel - bezug + 180.0) % 360.0) - 180.0


def lesen(bild, kal):
    """bild: Graubild (numpy) oder Pfad. Liefert dict mit winkel, wert, sicherheit."""
    g = zeigerbild(bild, kal.get('kanal', 'grau')) if isinstance(bild, (str, pathlib.Path)) else bild
    cx, cy = kal['drehpunkt']
    if kal.get('bildgroesse') and tuple(kal['bildgroesse']) != (g.shape[1], g.shape[0]):
        # Kalibrierung in anderer Auflösung: Geometrie skalieren
        f = g.shape[1] / kal['bildgroesse'][0]
        kal = dict(kal, drehpunkt=[cx * f, cy * f], radius_innen=kal['radius_innen'] * f,
                   radius_aussen=kal['radius_aussen'] * f, zeigerbreite_px=kal['zeigerbreite_px'] * f)
        cx, cy = kal['drehpunkt']
    winkel, profil = polar_profil(g, cx, cy, kal['radius_innen'], kal['radius_aussen'], teile=kal.get('radial_teile', 3))
    wink, sicher, glatt, maske = zeiger_winkel(winkel, profil, kal)
    return {'winkel': wink, 'wert': winkel_zu_wert(wink, kal), 'sicherheit': sicher, 'einheit': kal.get('einheit', ''),
            '_profil': (winkel, profil, glatt, maske), '_kal': kal}


def zeichne(bild_pfad, ergebnis, aus_pfad):
    """Kontrollbild: Drehpunkt, Radiusband, Marken, gefundener Zeiger, Wert."""
    im = Image.open(bild_pfad).convert('RGB'); d = ImageDraw.Draw(im)
    kal = ergebnis['_kal']; cx, cy = kal['drehpunkt']; r1, r2 = kal['radius_innen'], kal['radius_aussen']
    for r in (r1, r2):
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(10, 143, 159), width=2)
    for m in kal['marken']:
        t = math.radians(m['winkel']); x, y = cx + r2 * math.cos(t), cy - r2 * math.sin(t)
        d.ellipse([x - 6, y - 6, x + 6, y + 6], outline=(193, 107, 35), width=3)
    t = math.radians(ergebnis['winkel'])
    d.line([cx, cy, cx + (r2 + 30) * math.cos(t), cy - (r2 + 30) * math.sin(t)], fill=(107, 79, 160), width=3)
    d.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=(107, 79, 160))
    text = f"{ergebnis['wert']:.3f} {ergebnis['einheit']}   {ergebnis['winkel']:.2f}°   Sicherheit {ergebnis['sicherheit']:.0f}"
    d.rectangle([8, 8, 8 + 9 * len(text), 30], fill=(255, 255, 255)); d.text((12, 12), text, fill=(0, 0, 0))
    im.save(aus_pfad)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bild'); ap.add_argument('--kal', required=True, help='kalibrierung.json aus kalibrieren.py')
    ap.add_argument('--zeige', action='store_true', help='Kontrollbild nach ../Bilder/<name>_gelesen.png schreiben')
    a = ap.parse_args()
    kal = json.load(open(a.kal))
    e = lesen(a.bild, kal)
    print(f"Winkel {e['winkel']:.2f}°  ->  {e['wert']:.4f} {e['einheit']}   (Sicherheit {e['sicherheit']:.1f}; unter 8 unsicher)")
    if a.zeige:
        ziel = pathlib.Path(__file__).resolve().parent.parent / 'Bilder' / (pathlib.Path(a.bild).stem + '_gelesen.png')
        zeichne(a.bild, e, ziel); print('Kontrollbild:', ziel)


# =====================================================================================================
#  Selbstkalibrierung über eingestellte Zeigerausschläge (ohne Klicks, ohne Teilstriche)
#  Der Nutzer stellt bekannte Ströme ein (z. B. 0 und Vollausschlag). Je Bild wird der Zeiger als
#  Gerade gefunden; die Geraden schneiden sich im Drehpunkt; die Winkel bekommen die eingestellten Werte.
# =====================================================================================================

def duenne_dunkle_linien(z, breite_px=9):
    """Dunkelheit nur dünner Strukturen: morphologisches Schließen (Min von Max) mit Kern > Zeigerbreite
    entfernt dünne dunkle Linien, die Differenz zum Original zeigt genau sie. Flächige dunkle Bereiche
    (Gehäusekanten, Schatten, Nabe) fallen heraus."""
    from PIL import Image as _I, ImageFilter as _F
    im = _I.fromarray(np.clip(z, 0, 255).astype(np.uint8))
    geschlossen = np.asarray(im.filter(_F.MaxFilter(breite_px)).filter(_F.MinFilter(breite_px)), dtype=np.float32)
    return geschlossen - z


def zeiger_linie(z=None, dunkel=None, k_schwelle=4.0, versuche=400, toleranz_px=2.0, seed=0, kanal='grau'):
    """Zeiger als Gerade ohne Kenntnis des Drehpunkts.
    Entweder Zeigerbild z (Zeiger dunkel): Kandidaten = dünne dunkle Linien (duenne_dunkle_linien);
    oder fertiges Dunkelheitsbild `dunkel` (z. B. Differenz zweier Ausschläge: nur der Zeiger bleibt).
    RANSAC-Gerade mit den meisten Punkten -> PCA-Verfeinerung.
    Liefert dict: punkt (Schwerpunkt), richtung (Einheitsvektor), inlier (N×2), anteil (Inlier/Kandidaten)."""
    if dunkel is None:
        # Einzelbild: nur dünne dunkle Strukturen (Kern ~ Bildbreite/80, mindestens 5 px), dann die obersten
        # 1 % als Kandidaten (mindestens aber k·MAD über dem Rauschen). So bleibt der Zeiger die größte
        # kollineare Punktmenge, auch bei Farbstich, Unschärfe und rötlichen Störflächen.
        dunkel = duenne_dunkle_linien(z, breite_px=max(5, int(z.shape[1] / 80) | 1))
        rausch = np.median(np.abs(dunkel - np.median(dunkel))) * 1.4826 + 1e-3
        schwelle = max(np.percentile(dunkel, 99.0), k_schwelle * rausch)
    else:
        rausch = np.median(np.abs(dunkel - np.median(dunkel))) * 1.4826 + 1e-3
        schwelle = k_schwelle * rausch
    ys, xs = np.nonzero(dunkel >= schwelle)
    if len(xs) < 50:
        raise ValueError('zu wenige dunkle Punkte für eine Zeigerlinie')
    P = np.c_[xs, ys].astype(np.float64)
    if len(P) > 20000:                       # Rechenzeit begrenzen
        P = P[np.random.default_rng(seed).choice(len(P), 20000, replace=False)]
    rng = np.random.default_rng(seed)
    beste, beste_n = None, 0
    for _ in range(versuche):
        i, j = rng.choice(len(P), 2, replace=False)
        d = P[j] - P[i]; L = np.hypot(*d)
        if L < 20:                            # zu kurze Basis
            continue
        d /= L; nrm = np.array([-d[1], d[0]])
        abst = np.abs((P - P[i]) @ nrm)
        n = int((abst < toleranz_px).sum())
        if n > beste_n:
            beste_n, beste = n, (P[i].copy(), d.copy())
    p0, d = beste
    nrm = np.array([-d[1], d[0]])
    inl = P[np.abs((P - p0) @ nrm) < toleranz_px]
    # PCA-Verfeinerung auf den Inliern, dann Inlier neu bestimmen (zweimal)
    for _ in range(2):
        m = inl.mean(axis=0); _, _, vt = np.linalg.svd(inl - m, full_matrices=False); d = vt[0]
        nrm = np.array([-d[1], d[0]]); inl = P[np.abs((P - m) @ nrm) < toleranz_px]
        inl = laengster_abschnitt(inl, m, d)
    m = inl.mean(axis=0)
    return {'punkt': m, 'richtung': d, 'inlier': inl, 'anteil': len(inl) / len(P), 'n': len(inl)}


def laengster_abschnitt(inl, m, d, luecke_px=12):
    """Nur den längsten zusammenhängenden Abschnitt der Inlier entlang der Geraden behalten
    (weit entfernte Störpunkte, die zufällig auf der Geraden liegen, fallen heraus)."""
    if len(inl) < 3:
        return inl
    t = np.sort((inl - m) @ d)
    schnitte = np.where(np.diff(t) > luecke_px)[0]
    grenzen = np.r_[0, schnitte + 1, len(t)]
    k = np.argmax(np.diff(grenzen))
    lo, hi = t[grenzen[k]], t[grenzen[k + 1] - 1]
    tt = (inl - m) @ d
    return inl[(tt >= lo) & (tt <= hi)]


def schnittpunkt_geraden(linien):
    """Punkt mit kleinster Summe quadrierter Abstände zu den Geraden (punkt, richtung)."""
    A = np.zeros((2, 2)); b = np.zeros(2)
    for l in linien:
        d = l['richtung']; M = np.eye(2) - np.outer(d, d)
        A += M; b += M @ l['punkt']
    if np.linalg.cond(A) > 1e6:
        raise ValueError('Zeigerstellungen zu ähnlich, kein eindeutiger Drehpunkt')
    return np.linalg.solve(A, b)


def kalibrierung_aus_zeigerstellungen(bilder_werte, kanal='auto', einheit='A', zeigerbreite_px=None, bildgroesse=None):
    """bilder_werte: Liste (Bild oder Pfad, eingestellter Wert). Mindestens zwei deutlich verschiedene Ausschläge.
    kanal 'auto': 'rot' und 'grau' probieren, den Kanal mit dem größeren Inlier-Anteil nehmen (rote vs. schwarze Zeiger)."""
    def _lade(b, k):
        return zeigerbild(b, k) if isinstance(b, (str, pathlib.Path)) else (zeigerbild(b, k) if b.ndim == 3 else b)
    kanaele = ('rot', 'grau') if kanal == 'auto' else (kanal,)
    bester = None
    for k in kanaele:
        try:
            zs = [_lade(b, k) for b, _ in bilder_werte]
            if len(zs) >= 2:
                # Bewegungsbild: je Bild die Karte dünner dunkler Linien (Zeiger, Striche, Schrift), Helligkeit
                # auf gleichen Median normiert (Belichtung darf sich ändern). Was in ALLEN anderen Ausschlägen
                # auch dunkel ist (Striche, Schrift), wird abgezogen; übrig bleibt der Zeiger dieses Ausschlags.
                kern = max(9, int(zs[0].shape[1] / 100) | 1)
                karten = [duenne_dunkle_linien(z * (200.0 / max(np.median(z), 1.0)), kern) for z in zs]
                linien = []
                for i, (_, w) in enumerate(bilder_werte):
                    statisch = np.min(np.stack([kk for j, kk in enumerate(karten) if j != i]), axis=0)
                    linien.append(dict(zeiger_linie(dunkel=karten[i] - statisch), wert=w))   # ohne Abschneiden: Rauschmaß bleibt gültig
            else:
                linien = [dict(zeiger_linie(_lade(b, k), kanal=k), wert=w) for b, w in bilder_werte]
        except ValueError:
            continue
        guete = min(l['anteil'] for l in linien)
        if bester is None or guete > bester[0]:
            bester = (guete, k, linien)
    if bester is None:
        raise ValueError('keine Zeigerlinie gefunden')
    _, k, linien = bester
    pivot = schnittpunkt_geraden(linien)
    marken, r_min, r_max, breiten = [], [], [], []
    for l in linien:
        rel = l['inlier'] - pivot
        # Richtung vom Drehpunkt nach außen (Vorzeichen der Richtung festlegen)
        d = l['richtung'] if (rel @ l['richtung']).mean() > 0 else -l['richtung']
        r = rel @ d
        winkel = math.degrees(math.atan2(-d[1], d[0])) % 360.0
        marken.append({'x': float(pivot[0] + r.max() * d[0]), 'y': float(pivot[1] + r.max() * d[1]), 'winkel': winkel, 'wert': float(l['wert'])})
        r_min.append(np.percentile(r, 2)); r_max.append(np.percentile(r, 98))
        nrm = np.array([-d[1], d[0]]); breiten.append(2 * np.std(rel @ nrm) * 2)   # ~ volle Breite
    L_min, L_max = float(max(r_min)), float(min(r_max))
    band = (L_min + 0.15 * (L_max - L_min), L_max - 0.08 * (L_max - L_min))
    kal = {'drehpunkt': [float(pivot[0]), float(pivot[1])], 'radius_innen': band[0], 'radius_aussen': band[1],
           'zeigerbreite_px': float(zeigerbreite_px or max(3.0, np.median(breiten))), 'einheit': einheit, 'kanal': k,
           'marken': marken, 'bildgroesse': list(bildgroesse) if bildgroesse else None,
           'skalenradius': L_max, 'verfahren': 'zeigerstellungen',
           'linien': [{'wert': l['wert'], 'inlier': l['n'], 'anteil': round(l['anteil'], 3)} for l in linien]}
    # Markenwinkel mit dem Betriebsverfahren (Polarprofil um den gefundenen Drehpunkt) nachmessen,
    # damit Kalibrierung und spätere Messung dieselbe Winkeldefinition haben.
    for m, (b, _) in zip(kal['marken'], bilder_werte):
        e = lesen(_lade(b, k), kal)
        if abs(_entrollen(e['winkel'], m['winkel']) - m['winkel']) < 3.0:      # nur plausible Korrekturen
            m['winkel_linie'] = m['winkel']; m['winkel'] = e['winkel']
    return kal


# =====================================================================================================
#  Verfolgung: Instrument darf sich gegenüber der Kamera verschieben und leicht verdrehen
#  Bezug ist das Muster des Skalenrings (Teilstriche) im Graubild. Bei der Kalibrierung wird das Muster
#  gemerkt; im Betrieb werden Verschiebung (dx, dy) und Verdrehung (dθ) gesucht, bei denen das aktuelle
#  Muster am besten zum gemerkten passt. Drehpunkt und Markenwinkel werden entsprechend mitgeführt.
# =====================================================================================================

MUSTER_SCHRITT = 0.25          # Winkelauflösung des Ringmusters
MUSTER_RAND = 12.0             # Ringmuster reicht so weit über die äußersten Marken hinaus


def _sektor(kal):
    w, _ = marken_kette(kal)
    return min(w) - MUSTER_RAND, max(w) + MUSTER_RAND


def ring_finden(grau, kal):
    """Radiusband der Teilstriche: dort wechselt die Helligkeit entlang des Winkels am stärksten."""
    cx, cy = kal['drehpunkt']; L = kal['skalenradius']
    lo, hi = _sektor(kal)
    winkel = np.arange(lo, hi, 0.5); th = np.deg2rad(winkel)
    radien = np.arange(0.85 * L, 1.35 * L, 2.0)
    staerke = []
    for r in radien:
        X = np.floor(cx + r * np.cos(th)).astype(int); Y = np.floor(cy - r * np.sin(th)).astype(int)
        ok = (X >= 0) & (X < grau.shape[1]) & (Y >= 0) & (Y < grau.shape[0])
        if ok.sum() < len(th) * 0.8:
            staerke.append(0.0); continue
        p = grau[Y[ok], X[ok]]
        staerke.append(float(np.std(np.diff(p))))          # Striche: viele Hell-Dunkel-Wechsel
    staerke = np.array(staerke)
    if staerke.max() <= 0:
        raise ValueError('kein Skalenring gefunden')
    gut = radien[staerke > 0.5 * staerke.max()]
    return float(gut.min()), float(gut.max())


MUSTER_TEILE = 3               # radiale Abschnitte des Rings (innen/mitte/außen), hintereinander gehängt


def ring_muster(grau, cx, cy, r_a, r_b, lo, hi, schritt=MUSTER_SCHRITT, teile=MUSTER_TEILE):
    """Helligkeitsmuster des Skalenrings über dem Winkel, je radialem Abschnitt eines (hintereinander), mittelwertfrei.
    Mehrere Abschnitte machen das Muster auch gegen radiale Verschiebung empfindlich (Strichenden wandern
    zwischen den Abschnitten), sonst ließe sich eine Verschiebung längs des Bogens kaum von einer Drehung trennen."""
    winkel = np.arange(lo, hi, schritt); th = np.deg2rad(winkel)
    n_r = max(2 * teile, int(r_b - r_a))
    r = np.linspace(r_a, r_b, n_r)
    X = np.floor(cx + np.outer(np.cos(th), r)).astype(int); Y = np.floor(cy - np.outer(np.sin(th), r)).astype(int)
    h, b = grau.shape
    X = np.clip(X, 0, b - 1); Y = np.clip(Y, 0, h - 1)
    werte = grau[Y, X]
    grenzen = np.linspace(0, n_r, teile + 1).astype(int); k = int(round(8.0 / schritt))
    teile_p = []
    for t in range(teile):
        p = werte[:, grenzen[t]:grenzen[t + 1]].mean(axis=1)
        teile_p.append(p - gleitender_median(p, k))            # langsame Helligkeitsverläufe entfernen
    return np.concatenate(teile_p)


def _korrelation_teile(a, b, maxlag, teile=MUSTER_TEILE):
    """Kreuzkorrelation über den Winkel, je Abschnitt getrennt verschoben und dann gemittelt."""
    n = len(a) // teile; lags = np.arange(-maxlag, maxlag + 1); out = np.zeros(len(lags))
    for t in range(teile):
        _, k = _korrelation(a[t * n:(t + 1) * n], b[t * n:(t + 1) * n], maxlag); out += k / teile
    return lags, out


def kalibrierung_verfolgung_anlegen(grau, kal):
    """Ringband bestimmen und Bezugsmuster in die Kalibrierung schreiben (einmalig nach der Kalibrierung)."""
    r_a, r_b = ring_finden(grau, kal)
    lo, hi = _sektor(kal)
    cx, cy = kal['drehpunkt']
    kal['ring'] = [r_a, r_b]
    kal['muster'] = ring_muster(grau, cx, cy, r_a, r_b, lo, hi).round(2).tolist()
    kal['muster_sektor'] = [lo, hi]
    return kal


def _korrelation(a, b, maxlag):
    """Normierte Kreuzkorrelation für Verschiebungen −maxlag…+maxlag (Stützstellen). Liefert (lags, werte)."""
    n = len(a); lags = np.arange(-maxlag, maxlag + 1); out = np.empty(len(lags))
    for i, l in enumerate(lags):
        if l >= 0: x, y = a[l:], b[:n - l]
        else:      x, y = a[:n + l], b[-l:]
        x = x - x.mean(); y = y - y.mean()
        out[i] = float((x * y).sum() / (np.sqrt((x * x).sum() * (y * y).sum()) + 1e-9))
    return lags, out


def verfolgen(grau, kal, such_px=30, such_grad=6.0):
    """Verschiebung/Verdrehung suchen, bei der das Ringmuster wieder passt. Liefert (kal_korrigiert, lage).
    Grob (Schritt 6 px) → fein (Schritt 1 px) um das Optimum; Verdrehung aus der Korrelationsverschiebung
    mit Parabelverfeinerung. lage = dict(dx, dy, dtheta, guete)."""
    if 'muster' not in kal:
        return kal, None
    bezug = np.asarray(kal['muster'], dtype=float); r_a, r_b = kal['ring']; lo, hi = kal['muster_sektor']
    cx0, cy0 = kal['drehpunkt']; maxlag = int(round(such_grad / MUSTER_SCHRITT))

    def bewerte(dx, dy):
        p = ring_muster(grau, cx0 + dx, cy0 + dy, r_a, r_b, lo, hi)
        lags, k = _korrelation_teile(p, bezug, maxlag)
        i = int(np.argmax(k)); return k[i], lags[i], lags, k

    best = (-2, 0, 0, 0)
    for dx in range(-such_px, such_px + 1, 4):
        for dy in range(-such_px, such_px + 1, 4):
            g, lag, _, _ = bewerte(dx, dy)
            if g > best[0]: best = (g, dx, dy, lag)
    gx, gy = best[1], best[2]
    for dx in range(gx - 3, gx + 4):
        for dy in range(gy - 3, gy + 4):
            g, lag, _, _ = bewerte(dx, dy)
            if g > best[0]: best = (g, dx, dy, lag)
    g, dx, dy, lag = best
    _, _, lags, k = bewerte(dx, dy)
    i = int(np.argmax(k)); delta = 0.0
    if 0 < i < len(k) - 1:
        nenner = k[i - 1] - 2 * k[i] + k[i + 1]
        if abs(nenner) > 1e-12: delta = float(np.clip(0.5 * (k[i - 1] - k[i + 1]) / nenner, -1, 1))
    # Muster p(θ) = bezug(θ − dθ): positive Verschiebung des Musters heißt, das Instrument ist um +dθ gedreht
    dtheta = (lags[i] + delta) * MUSTER_SCHRITT
    neu = dict(kal, drehpunkt=[cx0 + dx, cy0 + dy],
               marken=[dict(m, winkel=(m['winkel'] + dtheta) % 360.0) for m in kal['marken']])
    return neu, {'dx': dx, 'dy': dy, 'dtheta': dtheta, 'guete': float(g)}


def lesen_verfolgt(bild, kal):
    """Bild lesen mit Verfolgung: Graubild für die Lage, Zeigerbild für den Zeiger."""
    if isinstance(bild, (str, pathlib.Path)):
        grau = lade_grau(bild); z = zeigerbild(bild, kal.get('kanal', 'grau'))
    else:
        grau = bild.mean(axis=2) if bild.ndim == 3 else bild; z = zeigerbild(bild, kal.get('kanal', 'grau'))
    kal2, lage = verfolgen(grau, kal)
    e = lesen(z, kal2); e['lage'] = lage; e['_kal'] = kal2
    return e
