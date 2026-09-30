#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""erstelle_analoganzeige_seite.py — die Seite „Analoganzeige lesen“ (Fassung aus VERSION): eine HTML mit Simulation und
eingebauter Dokumentation, nach der Vorgabe des Verfassers vom 29.09.2026 („eine HTML mit eingebauter Doku und wenn möglich
immer eine Simulation dazu“).

  * Simulation: ein gezeichnetes Zeigerinstrument (Skala des 44C2: 26 Teilstriche, 6 Hauptstriche, roter Zeiger mit
    Parallaxe), einstellbar in Strom, Drehung, Versatz, Rauschen, Helligkeit; darauf läuft die Erkennung der Version 2
    (analoganzeige_lesen.js = Zeile für Zeile skala_v2.py / zeiger_v2.py): Bogen, Striche, Rotbild, Zeigergerade,
    Schnittpunkt, Wert. Kurve 0 … 1 mA mit Abweichung je Punkt.
  * Nachweis: der Bildsatz vom Gerät (0 … 0,85 mA, 16.09.2026) mit der Ablesung der Firmware; die Seite liest dieselben
    Bilder mit der JavaScript-Fassung und stellt Sollstrom, Firmware und Browser nebeneinander.
  * Dokumentation: das Manuskript (MathML, Bilder eingebettet, große Bilder verkleinert), das LIESMICH der Auslieferung
    und der Registerplan.
Aufruf: python3 Seite/erstelle_analoganzeige_seite.py   → Seite/Analoganzeige_lesen_<VERSION>.html
"""
from __future__ import annotations
import base64, io, json, re, subprocess
from pathlib import Path
from PIL import Image

H = Path(__file__).resolve().parent
P = H.parent
VERSION = (H / "VERSION").read_text(encoding="utf-8").strip()
DATUM = "29.09.2026"
NAMENSNENNUNG = "Prof. Dr.-Ing. Ralph Wystup M.Sc. — erstellt mit KI und Agent (Claude Code, Anthropic)"
ZIEL = H / f"Analoganzeige_lesen_{VERSION}.html"
BILDSATZ = P / "Bilder" / "44C2_satz_0916"


def daten_uri_bild(pfad: Path, max_breite=1200, qualitaet=85) -> str:
    """Bild als data:-Adresse; große PNG-Bildschirmfotos werden auf max_breite verkleinert und als JPEG gesetzt."""
    im = Image.open(pfad); im.load()
    if im.width > max_breite:
        im = im.convert("RGB"); im = im.resize((max_breite, round(im.height * max_breite / im.width)), Image.LANCZOS)
        b = io.BytesIO(); im.save(b, "JPEG", quality=qualitaet); return "data:image/jpeg;base64," + base64.b64encode(b.getvalue()).decode()
    typ = {"png": "image/png", "jpg": "image/jpeg", "jpeg": "image/jpeg"}[pfad.suffix.lower().lstrip(".")]
    return f"data:{typ};base64," + base64.b64encode(pfad.read_bytes()).decode()


def maskiere_system_bild() -> Path:
    """system.png trägt unten Netzangaben (Heimnetzbereich, Teil einer Domäne) — die Zeile wird in der Kopie überdeckt."""
    q = H / "_system_maskiert.png"; im = Image.open(P / "Bilder/Manuskript/system.png").convert("RGB")
    from PIL import ImageDraw
    z = ImageDraw.Draw(im); w, h = im.size
    z.rectangle([0, int(h * 0.76), w, int(h * 0.86)], fill=(255, 255, 255))
    z.text((int(w * 0.02), int(h * 0.79)), "Heimnetz (WLAN)                                                                  Heimnetz (LAN/WLAN)                                                                     Internet (Arbeitsumgebung des Assistenten)", fill=(90, 90, 90))
    im.save(q); return q


def manuskript_html() -> str:
    md = (P / "Manuskript/MANUSKRIPT_Analoganzeige.md").read_text(encoding="utf-8")
    kopf = re.match(r"---\n(.*?)\n---\n", md, re.S); meta = kopf.group(1) if kopf else ""; md = md[kopf.end():] if kopf else md
    titel = re.search(r'title:\s*"([^"]+)"', meta); datum = re.search(r'date:\s*"([^"]+)"', meta)
    md = md.replace("\\newpage", "")
    html = subprocess.run(["pandoc", "-f", "markdown", "-t", "html", "--mathml", "--number-sections", "--toc", "--toc-depth=2"],
                          input=md, capture_output=True, text=True, check=True).stdout
    maske = maskiere_system_bild()
    def ersetze(m):
        rel = m.group(1); pfad = (P / "Manuskript" / rel).resolve()
        if pfad.name == "system.png": pfad = maske
        return f'src="{daten_uri_bild(pfad)}"' if pfad.is_file() else m.group(0)
    html = re.sub(r'src="([^"]+\.(?:png|jpg|jpeg))"', ersetze, html)
    return f'<p class="small"><b>{titel.group(1) if titel else ""}</b> · {datum.group(1) if datum else ""}</p>\n' + html


def liesmich_html() -> str:
    md = (P / "Ergebnisse/LIESMICH_Inbetriebnahme.md").read_text(encoding="utf-8")
    return subprocess.run(["pandoc", "-f", "markdown", "-t", "html"], input=md, capture_output=True, text=True, check=True).stdout


def bildsatz_js() -> str:
    """Die zehn Bilder des Geräts mit der Ablesung der Firmware — als JSON in der Seite (JPEG data:-Adressen, 160 kB)."""
    aus = []
    for p in sorted(BILDSATZ.glob("0p*.jpg")):
        j = json.loads(p.with_suffix(".json").read_text(encoding="utf-8"))
        aus.append(dict(name=p.name, soll=float(p.stem.replace("p", ".")), firmware=j.get("wert"), median=j.get("wert_median"), gueltig=bool(j.get("gueltig")),
                        winkel=j.get("linie", {}).get("winkel180"), punkte=j.get("linie", {}).get("punkte"), dauer_ms=j.get("dauer_ms"), bild=daten_uri_bild(p)))
    return json.dumps(aus)


LESER = (H / "analoganzeige_lesen.js").read_text(encoding="utf-8").replace("if (typeof module !== 'undefined') module.exports = ANALOG;", "")

SEITE_JS = r"""
// ---------------------------------------------------------------- gezeichnetes Instrument (44C2-Skala)
const SIM = { w: 640, h: 480 };
function instrumentZeichnen(ctx, p) {
  // p: strom [mA], drehung [°], dx, dy [px], rausch [0..], hell [0.6..1.4], zeigerbreite [px], schatten [0..1]
  const { w, h } = SIM; ctx.save(); ctx.fillStyle = '#d8d3c8'; ctx.fillRect(0, 0, w, h);
  ctx.translate(w / 2 + p.dx, h / 2 + p.dy); ctx.rotate(-p.drehung * Math.PI / 180); ctx.translate(-w / 2, -h / 2);
  // Zifferblatt
  ctx.fillStyle = '#f7f6f1'; ctx.fillRect(40, 30, 560, 400);        // Zifferblatt ohne dünnen Rahmen (ein Gehäuserand wäre breit, keine dünne Struktur)
  const cx = 320, cy = 372, r = 284, a0 = 132.6, a1 = 49.2;           // Geometrie wie am Gerät (Bogenmittelpunkt unterhalb der Skala)
  const rad = Math.PI / 180, ang = v => (a0 - (a0 - a1) * v) * rad;   // Wert 0 … 1 mA → Winkel
  ctx.strokeStyle = '#111'; ctx.lineWidth = 2.2; ctx.beginPath(); ctx.arc(cx, cy, r, -ang(0), -ang(1), false); ctx.stroke();
  ctx.font = '15px sans-serif'; ctx.fillStyle = '#111'; ctx.textAlign = 'center';
  for (let k = 0; k <= 25; k++) {
    const v = k / 25, t = ang(v), haupt = k % 5 === 0, L = haupt ? 26 : 12;
    ctx.lineWidth = haupt ? 2.4 : 1.4; ctx.beginPath(); ctx.moveTo(cx + (r + 3) * Math.cos(t), cy - (r + 3) * Math.sin(t)); ctx.lineTo(cx + (r + 3 + L) * Math.cos(t), cy - (r + 3 + L) * Math.sin(t)); ctx.stroke();
    if (haupt) ctx.fillText((v).toFixed(1).replace('.', ','), cx + (r + 52) * Math.cos(t), cy - (r + 52) * Math.sin(t) + 6);
  }
  ctx.font = 'bold 16px sans-serif'; ctx.fillText('mA', cx, cy - r * 0.45); ctx.font = '11px sans-serif'; ctx.fillText('44C2 · Klasse 1,5', cx, cy - r * 0.35);
  // Zeiger: Drehpunkt mit Parallaxe (3 % von r unter dem Bogenmittelpunkt), rot, mit Spitze knapp vor dem Bogen
  const px = cx, py = cy + 0.03 * r, t = ang(Math.min(1.02, Math.max(-0.01, p.strom)));
  ctx.strokeStyle = '#b8161a'; ctx.lineWidth = p.zeigerbreite; ctx.lineCap = 'round'; ctx.beginPath();
  ctx.moveTo(px - 30 * Math.cos(t), py + 30 * Math.sin(t)); ctx.lineTo(px + (r - 8) * Math.cos(t), py - (r - 8) * Math.sin(t)); ctx.stroke();
  ctx.fillStyle = '#222'; ctx.beginPath(); ctx.arc(px, py, 9, 0, 2 * Math.PI); ctx.fill();
  ctx.restore();
  // Beleuchtung: Schatten von links (Verlauf) und Helligkeit, dann Rauschen
  const img = ctx.getImageData(0, 0, w, h), d = img.data, zf = ANALOG.zufall(7);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const i = 4 * (y * w + x), f = p.hell * (1 - p.schatten * (1 - x / w) * 0.6);
    const n = p.rausch * (zf() + zf() + zf() - 1.5) * 2 * 12;
    d[i] = Math.min(255, Math.max(0, d[i] * f + n)); d[i + 1] = Math.min(255, Math.max(0, d[i + 1] * f * 0.98 + n)); d[i + 2] = Math.min(255, Math.max(0, d[i + 2] * f * 0.94 + n));
  }
  ctx.putImageData(img, 0, 0);
  return { cx, cy, r, px, py };
}
function rgbAus(ctx) { const { w, h } = SIM; const d = ctx.getImageData(0, 0, w, h).data, rgb = new Uint8Array(w * h * 3); for (let i = 0, j = 0; i < w * h; i++, j += 4) { rgb[3 * i] = d[j]; rgb[3 * i + 1] = d[j + 1]; rgb[3 * i + 2] = d[j + 2]; } return rgb; }
function einzeichnen(ctx, e, bereich) {
  const rad = Math.PI / 180; if (!e.bogen) return;
  const { cx, cy, r } = e.bogen; ctx.save(); ctx.lineWidth = 2; ctx.strokeStyle = '#ff8c00'; ctx.beginPath();
  for (let a = e.bogen.a0; a <= e.bogen.a1; a += 0.5) { const x = cx + r * Math.cos(a * rad), y = cy - r * Math.sin(a * rad); a === e.bogen.a0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y); }
  ctx.stroke();
  if (e.striche) {
    const hauptSorted = [...e.striche.haupt].sort((a, b) => b - a), n = hauptSorted.length;
    for (const [a, l] of e.striche.striche) { const t = a * rad, haupt = e.striche.haupt.includes(a); ctx.strokeStyle = haupt ? '#ff00ff' : '#00c8ff'; ctx.lineWidth = haupt ? 3 : 1;
      ctx.beginPath(); ctx.moveTo(cx + (r + 2) * Math.cos(t), cy - (r + 2) * Math.sin(t)); ctx.lineTo(cx + (r + 3 + l) * Math.cos(t), cy - (r + 3 + l) * Math.sin(t)); ctx.stroke(); }
    ctx.font = '11px sans-serif'; ctx.textAlign = 'center';
    hauptSorted.forEach((a, k) => { const t = a * rad, x = cx + (r - 16) * Math.cos(t), y = cy - (r - 16) * Math.sin(t); ctx.fillStyle = '#fff'; ctx.fillRect(x - 12, y - 7, 24, 14); ctx.fillStyle = '#c800c8'; ctx.fillText((bereich * k / (n - 1)).toFixed(1), x, y + 4); });
  }
  if (e.zeiger && e.schnitt) { ctx.strokeStyle = '#005aff'; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(e.zeiger.px - 0.4 * r * e.zeiger.dx, e.zeiger.py - 0.4 * r * e.zeiger.dy); ctx.lineTo(e.schnitt.sx, e.schnitt.sy); ctx.stroke();
    ctx.fillStyle = '#005aff'; ctx.beginPath(); ctx.arc(e.schnitt.sx, e.schnitt.sy, 5, 0, 2 * Math.PI); ctx.fill(); }
  ctx.strokeStyle = '#ff8c00'; ctx.beginPath(); ctx.arc(cx, cy, 4, 0, 2 * Math.PI); ctx.stroke(); ctx.restore();
}
function karteZeigen(canvas, feld, w, h, invert) {
  canvas.width = w; canvas.height = h; const ctx = canvas.getContext('2d'), img = ctx.createImageData(w, h); let mx = 1e-6; for (let i = 0; i < w * h; i++) if (feld[i] > mx) mx = feld[i];
  for (let i = 0; i < w * h; i++) { const v = invert ? 255 - Math.min(255, 255 * feld[i] / mx) : Math.min(255, feld[i]); img.data[4 * i] = img.data[4 * i + 1] = img.data[4 * i + 2] = v; img.data[4 * i + 3] = 255; }
  ctx.putImageData(img, 0, 0);
}
const $ = id => document.getElementById(id);
function parameter() { return { strom: +$('strom').value, drehung: +$('drehung').value, dx: +$('dx').value, dy: +$('dy').value, rausch: +$('rausch').value, hell: +$('hell').value, zeigerbreite: +$('zbreite').value, schatten: +$('schatten').value }; }
let LETZT = null;
function lesen(anzeigen = true) {
  const p = parameter(), c = $('cBild'); c.width = SIM.w; c.height = SIM.h; const ctx = c.getContext('2d'); const geo = instrumentZeichnen(ctx, p);
  const rgb = rgbAus(ctx); const t0 = performance.now(); const e = ANALOG.lesen(rgb, SIM.w, SIM.h, { bereich: 1.0 }); const ms = performance.now() - t0; LETZT = { p, e, geo };
  if (anzeigen) {
    einzeichnen(ctx, e, 1.0); karteZeigen($('cKarte'), e.d || new Float32Array(SIM.w * SIM.h), SIM.w, SIM.h, true); karteZeigen($('cRot'), e.rot || new Float32Array(SIM.w * SIM.h), SIM.w, SIM.h, false);
    const z = [['eingestellter Strom', p.strom.toFixed(3) + ' mA'], ['abgelesen', e.wert === null ? '—' : e.wert.toFixed(3) + ' mA'], ['Abweichung', e.wert === null ? '—' : ((e.wert - p.strom) * 1000).toFixed(1) + ' µA'],
      ['Bogen', e.bogen ? `r = ${e.bogen.r.toFixed(1)} px, ${e.bogen.inlier} Inlier, Sektor ${e.bogen.a0.toFixed(1)}° … ${e.bogen.a1.toFixed(1)}°` : '—'],
      ['Striche', e.striche ? `${e.striche.striche.length} Teilstriche, ${e.striche.haupt.length} Hauptstriche, Teilung ${e.striche.pitch ? e.striche.pitch.toFixed(2) + '°' : '—'}, Regelmäßigkeit ${e.striche.regel === null ? '—' : e.striche.regel.toFixed(3)}` : '—'],
      ['Weißabgleich', e.weissabgleich ? e.weissabgleich.map(f => f.toFixed(2)).join(' / ') : '—'], ['Zeiger', e.zeiger ? `${e.zeiger.n} Punkte auf der Geraden (von ${e.zeiger.kandidaten} Kandidaten), Richtung ${e.zeiger.winkel.toFixed(2)}°` : '—'],
      ['Schnittpunkt', e.schnitt ? `Winkel ${e.schnitt.winkel.toFixed(2)}° vom Bogenmittelpunkt` : '—'], ['gültig', e.gueltig ? 'ja (Skala und Zeiger im selben Bild)' : 'nein — ' + e.hinweise.join('; ')], ['Rechenzeit im Browser', ms.toFixed(0) + ' ms']];
    $('ergebnis').innerHTML = z.map(([a, b]) => `<tr><td>${a}</td><td>${b}</td></tr>`).join('');
  }
  return e;
}
function kurve() {
  const p0 = parameter(), pts = []; const c = $('cKurve'); const ctx = c.getContext('2d'); c.width = c.clientWidth * devicePixelRatio; c.height = 260 * devicePixelRatio; ctx.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
  const W = c.clientWidth, Hh = 260; ctx.clearRect(0, 0, W, Hh); $('stand').textContent = 'Kurve läuft …'; window.LABOR.kurveErgebnis = null;
  let k = 0; const werte = []; for (let s = 0; s <= 1.0001; s += 0.05) werte.push(+s.toFixed(2));
  (function schritt() {
    if (k >= werte.length) {
      const abw = pts.filter(q => q.wert !== null).map(q => Math.abs(q.wert - q.strom)); const mx = Math.max(...abw), mw = abw.reduce((a, b) => a + b, 0) / abw.length;
      $('stand').textContent = `Kurve: ${pts.length} Punkte, größte Abweichung ${(mx * 1000).toFixed(1)} µA, mittlere ${(mw * 1000).toFixed(1)} µA, gültig ${pts.filter(q => q.gueltig).length} von ${pts.length}`;
      window.LABOR.kurveErgebnis = pts; return;
    }
    $('strom').value = werte[k]; $('stromWert').textContent = werte[k].toFixed(2); const e = lesen(k === werte.length - 1);
    pts.push({ strom: werte[k], wert: e.wert, gueltig: e.gueltig });
    // zeichnen
    ctx.clearRect(0, 0, W, Hh); const L = 56, R = 16, T = 18, B = 34; const X = v => L + (W - L - R) * v, Y = v => Hh - B - (Hh - T - B) * (v + 20) / 40;
    ctx.strokeStyle = '#ccc'; ctx.fillStyle = '#555'; ctx.font = '11px sans-serif'; ctx.textAlign = 'right';
    for (let g = -20; g <= 20; g += 10) { ctx.beginPath(); ctx.moveTo(L, Y(g)); ctx.lineTo(W - R, Y(g)); ctx.stroke(); ctx.fillText(g + ' µA', L - 6, Y(g) + 4); }
    ctx.textAlign = 'center'; for (let v = 0; v <= 1.0001; v += 0.2) ctx.fillText(v.toFixed(1) + ' mA', X(v), Hh - 12);
    ctx.strokeStyle = '#1b3a8f'; ctx.lineWidth = 2; ctx.beginPath(); let erst = true;
    for (const q of pts) if (q.wert !== null) { const x = X(q.strom), y = Y(Math.max(-20, Math.min(20, (q.wert - q.strom) * 1000))); erst ? ctx.moveTo(x, y) : ctx.lineTo(x, y); erst = false; }
    ctx.stroke(); for (const q of pts) { ctx.fillStyle = q.gueltig ? '#1b3a8f' : '#b0171f'; ctx.beginPath(); ctx.arc(X(q.strom), Y(Math.max(-20, Math.min(20, ((q.wert ?? q.strom) - q.strom) * 1000))), 3.5, 0, 2 * Math.PI); ctx.fill(); }
    ctx.fillStyle = '#333'; ctx.textAlign = 'left'; ctx.fillText('Abweichung abgelesen − eingestellt', L, 12);
    k++; setTimeout(schritt, 10);
  })();
}
function bildsatzAuswerten() {
  const tb = $('satz'); tb.innerHTML = ''; const zeilen = []; let k = 0; $('satzStand').textContent = 'liest …'; window.LABOR.satzErgebnis = null;
  (function schritt() {
    if (k >= BILDSATZ.length) { const abw = zeilen.map(z => Math.abs(z.js - z.soll)); const mx = Math.max(...abw), mw = abw.reduce((a, b) => a + b, 0) / abw.length;
      $('satzStand').textContent = `${zeilen.length} Bilder: größte Abweichung Browser ${(mx * 1000).toFixed(1)} µA, mittlere ${(mw * 1000).toFixed(1)} µA, gültig ${zeilen.filter(z => z.gueltig).length} von ${zeilen.length}`; window.LABOR.satzErgebnis = zeilen; return; }
    const b = BILDSATZ[k], img = new Image(); img.onload = () => {
      const c = document.createElement('canvas'); c.width = img.width; c.height = img.height; const ctx = c.getContext('2d'); ctx.drawImage(img, 0, 0);
      const d = ctx.getImageData(0, 0, c.width, c.height).data, rgb = new Uint8Array(c.width * c.height * 3); for (let i = 0, j = 0; i < c.width * c.height; i++, j += 4) { rgb[3 * i] = d[j]; rgb[3 * i + 1] = d[j + 1]; rgb[3 * i + 2] = d[j + 2]; }
      const e = ANALOG.lesen(rgb, c.width, c.height, { bereich: 1.0 }); einzeichnen(ctx, e, 1.0);
      const z = { name: b.name, soll: b.soll, firmware: b.firmware, js: e.wert, gueltig: e.gueltig, punkte: e.zeiger ? e.zeiger.n : 0, striche: e.striche ? e.striche.striche.length : 0 }; zeilen.push(z);
      const tr = document.createElement('tr'); tr.innerHTML = `<td><img src="${c.toDataURL('image/jpeg', 0.8)}" style="width:200px"></td><td>${b.soll.toFixed(2)}</td><td>${b.firmware === null ? '—' : b.firmware.toFixed(3)}</td><td>${e.wert === null ? '—' : e.wert.toFixed(3)}</td><td>${e.wert === null ? '—' : ((e.wert - b.soll) * 1000).toFixed(1)}</td><td>${z.striche} / ${e.striche ? e.striche.haupt.length : 0}</td><td>${z.punkte}</td><td>${e.gueltig ? 'ja' : 'nein'}</td>`;
      tb.appendChild(tr); k++; setTimeout(schritt, 10);
    }; img.src = b.bild;
  })();
}
window.addEventListener('DOMContentLoaded', () => {
  for (const id of ['strom', 'drehung', 'dx', 'dy', 'rausch', 'hell', 'zbreite', 'schatten']) { const el = $(id); const out = $(id + 'Wert'); const z = () => { if (out) out.textContent = (+el.value).toFixed(id === 'strom' ? 2 : id === 'hell' || id === 'rausch' || id === 'schatten' ? 2 : 0); }; el.addEventListener('input', () => { z(); lesen(); }); z(); }
  $('lesen').onclick = () => lesen(); $('kurve').onclick = kurve; $('satzKnopf').onclick = bildsatzAuswerten;
  lesen();
  window.LABOR = { ANALOG, lesen, kurve, bildsatzAuswerten, get letzt() { return LETZT; }, BILDSATZ };
});
"""


def seite() -> str:
    doku = manuskript_html(); lies = liesmich_html(); satz = bildsatz_js()
    css = """
:root { --tinte:#1a1a1a; --leise:#666; --blau:#1b3a8f; --rot:#b0171f; --gruen:#2e7d32; --karte:#fff; --grund:#fbfaf7; --linie:#d8d4cc; }
body { font-family: "DejaVu Serif", Georgia, serif; color: var(--tinte); background: var(--grund); margin: 0; line-height: 1.45; }
header { padding: 14px 24px 6px; border-bottom: 1px solid var(--linie); background: #f3f1ea; }
h1 { margin: 0 0 4px; font-size: 22px; } h2 { font-size: 17px; margin: 18px 0 8px; } h3 { font-size: 15px; margin: 14px 0 6px; }
.small { color: var(--leise); font-size: 13px; }
main { display: grid; grid-template-columns: 400px 1fr; gap: 16px; padding: 16px 24px; }
@media (max-width: 1000px) { main { grid-template-columns: 1fr; } }
.karte { background: var(--karte); border: 1px solid var(--linie); border-radius: 8px; padding: 12px 16px; }
label.zeile { display: grid; grid-template-columns: 150px 1fr 70px; gap: 8px; align-items: center; font-size: 14px; margin: 4px 0; }
input[type=range] { width: 100%; }
button { font: inherit; padding: 6px 12px; margin: 4px 6px 4px 0; border: 1px solid var(--blau); background: #eef2fa; border-radius: 6px; cursor: pointer; }
button.haupt { background: var(--blau); color: #fff; }
canvas.bild { width: 100%; max-width: 640px; border: 1px solid var(--linie); background: #fff; display: block; }
canvas.klein { width: 100%; max-width: 320px; border: 1px solid var(--linie); display: block; }
table { border-collapse: collapse; font-size: 13px; } td, th { border-bottom: 1px solid var(--linie); padding: 3px 8px; text-align: left; vertical-align: top; }
details.doku { margin: 16px 24px; background: var(--karte); border: 1px solid var(--linie); border-radius: 8px; padding: 8px 16px; }
details.doku > summary { cursor: pointer; font-weight: bold; }
article.documentation { max-width: 900px; } article.documentation img { max-width: 100%; height: auto; }
article.documentation table { font-size: 13px; } article.documentation h1 { font-size: 20px; margin-top: 24px; }
.zwei { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
"""
    return f"""<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8"><title>Analoganzeige lesen · Fassung {VERSION}</title>
<meta name="viewport" content="width=device-width, initial-scale=1"><style>{css}</style></head>
<body>
<header>
<h1>Analoganzeige lesen: ein Zeigerinstrument optisch auslesen — Simulation, Nachweis, Dokumentation</h1>
<p class="small" id="fassung">Fassung {VERSION} · {DATUM} · {NAMENSNENNUNG}</p>
<p class="small">Das Verfahren der Version 2 (Skalenbogen, Teilstriche, Rotbild, Zeigergerade, Ablesung am Schnittpunkt) läuft hier im Browser — dieselben Schritte wie in der Firmware des ESP32-S3, Zeile für Zeile nach den Referenzprogrammen.
Links ein gezeichnetes Instrument zum Verstellen, darunter der Bildsatz vom Gerät mit der Ablesung der Firmware. Alles rechnet ohne Netz.</p>
</header>
<main>
  <section class="karte">
    <h2 style="margin-top:0">Einstellen</h2>
    <button id="lesen" class="haupt">Lesen</button><button id="kurve">Kurve 0 … 1 mA</button>
    <p class="small" id="stand">Das Instrument wird bei jeder Änderung neu gezeichnet und gelesen.</p>
    <label class="zeile">Strom <input type="range" id="strom" min="0" max="1" step="0.01" value="0.5"><span><span id="stromWert"></span> mA</span></label>
    <label class="zeile">Drehung des Instruments <input type="range" id="drehung" min="-15" max="15" step="1" value="0"><span><span id="drehungWert"></span>°</span></label>
    <label class="zeile">Versatz waagerecht <input type="range" id="dx" min="-80" max="80" step="1" value="0"><span><span id="dxWert"></span> px</span></label>
    <label class="zeile">Versatz senkrecht <input type="range" id="dy" min="-60" max="60" step="1" value="0"><span><span id="dyWert"></span> px</span></label>
    <label class="zeile">Rauschen <input type="range" id="rausch" min="0" max="1.5" step="0.05" value="0.3"><span id="rauschWert"></span></label>
    <label class="zeile">Helligkeit <input type="range" id="hell" min="0.6" max="1.4" step="0.02" value="1"><span id="hellWert"></span></label>
    <label class="zeile">Schatten von links <input type="range" id="schatten" min="0" max="1" step="0.05" value="0.2"><span id="schattenWert"></span></label>
    <label class="zeile">Zeigerbreite <input type="range" id="zbreite" min="3" max="9" step="1" value="5"><span><span id="zbreiteWert"></span> px</span></label>
    <h3>Ergebnis</h3>
    <table><tbody id="ergebnis"></tbody></table>
    <p class="small">Die Skala gilt als gültig bei ≥ 200 Inliern des Bogens, 26 ± 1 Teilstrichen, 6 Hauptstrichen und einer Regelmäßigkeit unter 8 %; der Zeiger bei ≥ 40 Punkten auf der Geraden. Nur wenn beides im selben Bild stimmt, ist der Wert gültig — wie in der Firmware.</p>
  </section>
  <section class="karte">
    <h2 style="margin-top:0">Das Bild und was die Erkennung darin findet</h2>
    <canvas id="cBild" class="bild"></canvas>
    <p class="small">Bogen orange, Teilstriche cyan, Hauptstriche magenta mit zugeordnetem Wert, Zeigergerade blau bis zum Schnittpunkt mit dem Bogen.</p>
    <div class="zwei">
      <div><canvas id="cKarte" class="klein"></canvas><p class="small">dünne dunkle Strukturen (Schließen minus Bild), Kern 9 — die Karte, in der der Bogen gesucht wird</p></div>
      <div><canvas id="cRot" class="klein"></canvas><p class="small">Rotbild nach rechnerischem Weißabgleich — die Karte, in der der Zeiger gesucht wird</p></div>
    </div>
    <h2>Kurve über den Messbereich</h2>
    <canvas id="cKurve" style="width:100%;height:260px;border:1px solid var(--linie);background:#fff"></canvas>
    <p class="small">„Kurve 0 … 1 mA“ stellt den Strom in 21 Schritten ein und liest jedes Bild; gezeichnet wird die Abweichung in Mikroampere (rot: ungültig).</p>
  </section>
</main>
<section class="karte" style="margin:0 24px 16px">
  <h2 style="margin-top:0">Nachweis am Bildsatz vom Gerät (16.09.2026, 0 … 0,85 mA)</h2>
  <p class="small">Zehn Aufnahmen der Kamera des ESP32-S3 bei bekanntem Prüfstrom. Spalte „Firmware“: die Ablesung des Geräts aus dem Status-JSON dieses Bilds. Spalte „Browser“: dieselbe Erkennung in JavaScript in dieser Seite, einschließlich Einzeichnung.</p>
  <button id="satzKnopf" class="haupt">Bildsatz auswerten</button> <span class="small" id="satzStand"></span>
  <table><thead><tr><th>Bild (mit Einzeichnung)</th><th>Prüfstrom [mA]</th><th>Firmware [mA]</th><th>Browser [mA]</th><th>Abweichung Browser [µA]</th><th>Striche / Haupt</th><th>Zeigerpunkte</th><th>gültig</th></tr></thead><tbody id="satz"></tbody></table>
</section>
<details class="doku">
<summary>Dokumentation — das Manuskript in dieser Seite: Aufgabe, Aufbau, Verfahren Schritt für Schritt, Ergebnisse am Gerät, Anbindung an die Leitwarte, Betrieb, der Weg über Version 1, Lehren, Ausblick</summary>
<article class="documentation">
{doku}
</article>
</details>
<details class="doku">
<summary>Inbetriebnahme und Registerplan — das LIESMICH der Auslieferung Version 2</summary>
<article class="documentation">
{lies}
</article>
</details>
<script>
{LESER}
const BILDSATZ = {satz};
{SEITE_JS}
</script>
</body></html>
"""


if __name__ == "__main__":
    t = seite(); ZIEL.write_text(t, encoding="utf-8")
    print(f"{ZIEL.name}: {len(t.encode('utf-8'))/1024/1024:.2f} MB (Fassung {VERSION}, {DATUM})")
