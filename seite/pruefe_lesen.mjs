// Prüfung P1: die JavaScript-Fassung der Erkennung am Bildsatz 0 … 0,85 mA (Sollstrom aus dem Dateinamen) gegen die
// Ablesung der Firmware (Status-JSON je Bild). Eingabe: bildsatz_raw.json (RGB roh, aus den JPEGs).
import { createRequire } from 'node:module';
import fs from 'node:fs';
const require = createRequire(import.meta.url);
const A = require('/workspace/Analoganzeige_lesen/Seite/analoganzeige_lesen.js');
const pfad = process.argv[2] || '/tmp/claude-1000/-workspace/905236e7-aea4-4c03-805a-5e5668f5230d/scratchpad/bildsatz_raw.json';
const satz = JSON.parse(fs.readFileSync(pfad, 'utf8'));
let maxAbw = 0, sum = 0, n = 0, ungueltig = 0; const zeilen = [];
for (const b of satz) {
  const rgb = Buffer.from(b.rgb, 'base64');
  const e = A.lesen(new Uint8Array(rgb), b.w, b.h, { bereich: 1.0 });
  const abw = e.wert === null ? NaN : e.wert - b.soll;
  if (!isNaN(abw)) { maxAbw = Math.max(maxAbw, Math.abs(abw)); sum += Math.abs(abw); n++; }
  if (!e.gueltig) ungueltig++;
  const z = { name: b.name, soll: b.soll, firmware: b.wert, js: e.wert === null ? null : +e.wert.toFixed(3), abw: isNaN(abw) ? null : +abw.toFixed(3), gueltig: e.gueltig,
    bogen: e.bogen ? { r: +e.bogen.r.toFixed(1), inlier: e.bogen.inlier } : null, striche: e.striche ? e.striche.striche.length : null, haupt: e.striche ? e.striche.haupt.length : null,
    regel: e.striche && e.striche.regel !== null ? +e.striche.regel.toFixed(3) : null, zeigerpunkte: e.zeiger ? e.zeiger.n : null, winkel: e.schnitt ? +e.schnitt.winkel.toFixed(2) : null, ms: e.dauer_ms, hinweise: e.hinweise };
  zeilen.push(z); console.log(JSON.stringify(z));
}
const aus = { zeilen, max_abweichung: +maxAbw.toFixed(4), mittlere_abweichung: n ? +(sum / n).toFixed(4) : null, gueltig: satz.length - ungueltig, von: satz.length };
fs.writeFileSync('/workspace/Analoganzeige_lesen/Seite/pruefe_lesen.json', JSON.stringify(aus, null, 1));
console.log(`max ${aus.max_abweichung} mA, mittel ${aus.mittlere_abweichung} mA, gültig ${aus.gueltig} von ${aus.von}`);
