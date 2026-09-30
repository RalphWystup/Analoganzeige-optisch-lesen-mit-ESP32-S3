// analoganzeige_lesen.js — die Skalen- und Zeigererkennung der Version 2, Zeile für Zeile nach den Referenzprogrammen
// skala_v2.py und zeiger_v2.py (Offline-Referenz der Firmware ESP32S3_Zeiger_V2), für den Browser und für node.
//
// Eingabe: ein RGB-Bild (Breite w, Höhe h, Uint8Array rgb mit 3 Byte je Punkt). Ausgabe: Bogen (Kreis), Teil- und
// Hauptstriche, Zeigergerade, Schnittpunkt mit dem Bogen und der abgelesene Wert. Jeder Schritt trägt den Namen des
// Abschnitts im Manuskript (Kapitel „Verfahren“).
//
// Zufallszahlen: eigener Generator (xorshift) mit Saat, damit ein Lauf wiederholbar ist.
const ANALOG = (function () {
  const rad = Math.PI / 180, deg = 180 / Math.PI;

  function zufall(saat) { let s = saat >>> 0 || 1; return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; }; }

  // ------------------------------------------------------------------ Bilder
  function grau(rgb, w, h) {                     // g = (77 R + 150 G + 29 B) / 256, wie die Firmware
    const g = new Float32Array(w * h);
    for (let i = 0, j = 0; i < w * h; i++, j += 3) g[i] = (77 * rgb[j] + 150 * rgb[j + 1] + 29 * rgb[j + 2]) / 256;
    return g;
  }

  function maxfilter(a, w, h, k) {              // trennbar: Zeilen, dann Spalten; Fenster k (ungerade), Rand wie PIL (nächster Wert)
    const r = (k - 1) >> 1, t = new Float32Array(w * h), o = new Float32Array(w * h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      let m = -1e9; for (let d = -r; d <= r; d++) { const xx = Math.min(w - 1, Math.max(0, x + d)); const v = a[y * w + xx]; if (v > m) m = v; } t[y * w + x] = m;
    }
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      let m = -1e9; for (let d = -r; d <= r; d++) { const yy = Math.min(h - 1, Math.max(0, y + d)); const v = t[yy * w + x]; if (v > m) m = v; } o[y * w + x] = m;
    }
    return o;
  }
  function minfilter(a, w, h, k) {
    const r = (k - 1) >> 1, t = new Float32Array(w * h), o = new Float32Array(w * h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      let m = 1e9; for (let d = -r; d <= r; d++) { const xx = Math.min(w - 1, Math.max(0, x + d)); const v = a[y * w + xx]; if (v < m) m = v; } t[y * w + x] = m;
    }
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      let m = 1e9; for (let d = -r; d <= r; d++) { const yy = Math.min(h - 1, Math.max(0, y + d)); const v = t[yy * w + x]; if (v < m) m = v; } o[y * w + x] = m;
    }
    return o;
  }
  // „Dünne dunkle Strukturen“: Schließen (Minimum des Maximums) minus Bild — groß nur, wo etwas Dünnes und Dunkles liegt
  function duenneDunkleLinien(z, w, h, k) {
    const zc = new Float32Array(z.length); for (let i = 0; i < z.length; i++) zc[i] = Math.min(255, Math.max(0, Math.round(z[i])));
    const geschlossen = minfilter(maxfilter(zc, w, h, k), w, h, k), d = new Float32Array(z.length);
    for (let i = 0; i < z.length; i++) d[i] = geschlossen[i] - z[i];
    return d;
  }
  function perzentil(arr, p) {                   // wie numpy (lineare Zwischenwertbildung)
    const a = Float32Array.from(arr).sort(); if (!a.length) return 0;
    const k = (a.length - 1) * p / 100, lo = Math.floor(k), hi = Math.ceil(k); return a[lo] + (a[hi] - a[lo]) * (k - lo);
  }
  function median(arr) { return perzentil(arr, 50); }

  // ------------------------------------------------------------------ Der Skalenbogen
  function bogenFinden(d, w, h, zf) {
    const sw = Math.max(15, 0.3 * perzentil(d, 99.5));
    const B = new Uint8Array(w * h); for (let i = 0; i < w * h; i++) B[i] = d[i] > sw ? 1 : 0;
    // zusammenhängende Bereiche (4er-Nachbarschaft, Stapel statt Rekursion); größter mit Breite ≥ 30 % des Bilds
    const lab = new Int32Array(w * h); let n = 0, best = null; const stapel = new Int32Array(w * h);
    for (let s = 0; s < w * h; s++) {
      if (!B[s] || lab[s]) continue;
      n++; let sp = 0; stapel[sp++] = s; lab[s] = n; let cnt = 0, xmin = w, xmax = -1, ymax = -1; const xs = [], ys = [];
      while (sp) {
        const p = stapel[--sp], x = p % w, y = (p - x) / w; cnt++; if (x < xmin) xmin = x; if (x > xmax) xmax = x; if (y > ymax) ymax = y; xs.push(x); ys.push(y);
        if (x > 0 && B[p - 1] && !lab[p - 1]) { lab[p - 1] = n; stapel[sp++] = p - 1; }
        if (x < w - 1 && B[p + 1] && !lab[p + 1]) { lab[p + 1] = n; stapel[sp++] = p + 1; }
        if (y > 0 && B[p - w] && !lab[p - w]) { lab[p - w] = n; stapel[sp++] = p - w; }
        if (y < h - 1 && B[p + w] && !lab[p + w]) { lab[p + w] = n; stapel[sp++] = p + w; }
      }
      if (cnt < 300 || xmax - xmin < 0.3 * w) continue;
      if (!best || cnt > best.cnt) best = { cnt, xs, ys, ymax };
    }
    if (!best) return null;
    const P = best.xs.length, X = best.xs, Y = best.ys;
    // RANSAC-Kreis durch drei Punkte, Toleranz 1,5 px, Radius 150 … 500, Mittelpunkt unterhalb des Bogens
    let bx = null, bn = 0;
    for (let it = 0; it < 1500; it++) {
      const i = (zf() * P) | 0, j = (zf() * P) | 0, k = (zf() * P) | 0; if (i === j || j === k || i === k) continue;
      const ax = X[i], ay = Y[i], bx_ = X[j], by = Y[j], qx = X[k], qy = Y[k];
      const det = 2 * (ax * (by - qy) + bx_ * (qy - ay) + qx * (ay - by)); if (Math.abs(det) < 1e-6) continue;
      const ux = ((ax * ax + ay * ay) * (by - qy) + (bx_ * bx_ + by * by) * (qy - ay) + (qx * qx + qy * qy) * (ay - by)) / det;
      const uy = ((ax * ax + ay * ay) * (qx - bx_) + (bx_ * bx_ + by * by) * (ax - qx) + (qx * qx + qy * qy) * (bx_ - ax)) / det;
      const r = Math.hypot(ax - ux, ay - uy); if (r < 150 || r > 500 || uy < best.ymax) continue;
      let m = 0; for (let p = 0; p < P; p++) if (Math.abs(Math.hypot(X[p] - ux, Y[p] - uy) - r) < 1.5) m++;
      if (m > bn) { bn = m; bx = [ux, uy, r]; }
    }
    if (!bx) return null;
    for (let runde = 0; runde < 2; runde++) {       // Ausgleichskreis (kleinste Quadrate) auf den Inliern: x²+y² + a x + b y + c = 0
      const [ux, uy, r] = bx; let Sxx = 0, Sxy = 0, Sx = 0, Syy = 0, Sy = 0, N = 0, Rx = 0, Ry = 0, R1 = 0;
      for (let p = 0; p < P; p++) if (Math.abs(Math.hypot(X[p] - ux, Y[p] - uy) - r) < 1.5) {
        const x = X[p], y = Y[p], q = -(x * x + y * y); Sxx += x * x; Sxy += x * y; Syy += y * y; Sx += x; Sy += y; N++; Rx += x * q; Ry += y * q; R1 += q;
      }
      const s = loese3([[Sxx, Sxy, Sx], [Sxy, Syy, Sy], [Sx, Sy, N]], [Rx, Ry, R1]); if (!s) break;
      const cx = -s[0] / 2, cy = -s[1] / 2; bx = [cx, cy, Math.sqrt(cx * cx + cy * cy - s[2])];
    }
    const [cx, cy, r] = bx; const winkel = []; let inl = 0;
    for (let p = 0; p < P; p++) if (Math.abs(Math.hypot(X[p] - cx, Y[p] - cy) - r) < 1.5) { inl++; winkel.push(Math.atan2(-(Y[p] - cy), X[p] - cx) * deg); }
    return { cx, cy, r, inlier: inl, punkte: P, a0: perzentil(winkel, 1), a1: perzentil(winkel, 99), schwelle: sw };
  }
  function loese3(A, b) {                         // Gauß mit Spaltenpivot für 3×3
    const M = A.map((z, i) => [...z, b[i]]);
    for (let c = 0; c < 3; c++) {
      let p = c; for (let r = c + 1; r < 3; r++) if (Math.abs(M[r][c]) > Math.abs(M[p][c])) p = r;
      if (Math.abs(M[p][c]) < 1e-12) return null; [M[c], M[p]] = [M[p], M[c]];
      for (let r = 0; r < 3; r++) if (r !== c) { const f = M[r][c] / M[c][c]; for (let k = c; k < 4; k++) M[r][k] -= f * M[c][k]; }
    }
    return [M[0][3] / M[0][0], M[1][3] / M[1][1], M[2][3] / M[2][2]];
  }

  // ------------------------------------------------------------------ Teilstriche und Hauptstriche
  function stricheFinden(g, w, h, geo) {
    const d = duenneDunkleLinien(g, w, h, 5);     // feiner Kern: Strichabstand ≈ 9 px
    const { cx, cy, r } = geo; const W = []; for (let a = geo.a0 - 2; a < geo.a1 + 2; a += 0.1) W.push(a);
    const probe = (rr, t) => { const x = Math.min(w - 1, Math.max(0, (cx + rr * Math.cos(t)) | 0)), y = Math.min(h - 1, Math.max(0, (cy - rr * Math.sin(t)) | 0)); return d[y * w + x]; };
    const nah = W.map(a => { const t = a * rad; return (probe(r + 5, t) + probe(r + 7, t) + probe(r + 9, t) + probe(r + 11, t)) / 4; });
    const glatt = nah.map((v, i) => ((nah[i - 1] ?? 0) + v + (nah[i + 1] ?? 0)) / 3);   // Faltung mit [1/3,1/3,1/3], Rand 0 (wie np.convolve 'same')
    const sw = Math.max(10, 0.25 * perzentil(glatt, 99)); const striche = [];
    let i = 1;
    while (i < glatt.length - 1) {
      if (glatt[i] >= sw && glatt[i] >= glatt[i - 1] && glatt[i] >= glatt[i + 1]) {
        let j = i; while (j + 1 < glatt.length && glatt[j + 1] === glatt[i]) j++;
        const mitte = 0.5 * (W[i] + W[j]), t = mitte * rad; let laenge = 0;
        for (let rr = r + 3; rr < r + 60; rr += 1) {
          const x = Math.min(w - 1, Math.max(0, (cx + rr * Math.cos(t)) | 0)), y = Math.min(h - 1, Math.max(0, (cy - rr * Math.sin(t)) | 0));
          if (d[y * w + x] > sw * 0.6) laenge = rr - r; else if (rr - r > laenge + 3) break;
        }
        if (!striche.length || Math.abs(striche[striche.length - 1][0] - mitte) >= 0.6) striche.push([mitte, laenge]);
        else if (laenge > striche[striche.length - 1][1]) striche[striche.length - 1] = [mitte, laenge];
        i = j + 6;
      } else i++;
    }
    if (striche.length < 5) return { striche: [], haupt: [], pitch: null, regel: null, schwelle: sw };
    const la = striche.map(s => s[1]), lmed = median(la), haupt = striche.filter(s => s[1] > 1.5 * lmed).map(s => s[0]);
    const diff = []; for (let k = 1; k < striche.length; k++) diff.push(Math.abs(striche[k][0] - striche[k - 1][0]));
    const pitch = median(diff), mw = diff.reduce((a, b) => a + b, 0) / diff.length, sd = Math.sqrt(diff.reduce((a, b) => a + (b - mw) ** 2, 0) / diff.length);
    return { striche, haupt, pitch, regel: pitch > 0 ? sd / pitch : null, laenge_klein: lmed, schwelle: sw };
  }

  // ------------------------------------------------------------------ Rotbild mit rechnerischem Weißabgleich
  function rotbild(rgb, w, h, geo) {
    const { cx, cy, r } = geo; const R = [], G = [], B = [];
    for (let y = 0; y < h; y += 2) for (let x = 0; x < w; x += 2) {          // Ring 0,55 … 0,95 r im Skalensektor (jeder zweite Punkt genügt für den Median)
      const rr = Math.hypot(x - cx, y - cy), a = Math.atan2(-(y - cy), x - cx) * deg;
      if (rr > 0.55 * r && rr < 0.95 * r && a > geo.a0 && a < geo.a1) { const j = 3 * (y * w + x); R.push(rgb[j]); G.push(rgb[j + 1]); B.push(rgb[j + 2]); }
    }
    const wb = [median(R), median(G), median(B)], m = (wb[0] + wb[1] + wb[2]) / 3, f = wb.map(v => m / Math.max(v, 1));
    const rot = new Float32Array(w * h);
    for (let i = 0, j = 0; i < w * h; i++, j += 3) {
      const r_ = Math.min(255, rgb[j] * f[0]), g_ = Math.min(255, rgb[j + 1] * f[1]), b_ = Math.min(255, rgb[j + 2] * f[2]);
      rot[i] = 255 - 255 * Math.max(r_ - Math.max(g_, b_), 0) / Math.max(r_, 1);
    }
    return { rot, wb, faktoren: f };
  }

  // ------------------------------------------------------------------ Die Zeigergerade
  function zeigerGerade(rot, w, h, geo, zf) {
    const d = duenneDunkleLinien(rot, w, h, 9); const { cx, cy, r } = geo; const zoneWerte = [], zoneIdx = [];
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const rr = Math.hypot(x - cx, y - cy); if (rr <= 0.3 * r || rr >= r - 4) continue;
      const a = Math.atan2(-(y - cy), x - cx) * deg; if (a <= geo.a0 - 6 || a >= geo.a1 + 6) continue;
      zoneWerte.push(d[y * w + x]); zoneIdx.push(y * w + x);
    }
    if (!zoneWerte.length) return null;
    const sw = Math.max(12, perzentil(zoneWerte, 98.5)); const X = [], Y = [];
    for (let k = 0; k < zoneWerte.length; k++) if (zoneWerte[k] >= sw) { const p = zoneIdx[k]; X.push(p % w); Y.push((p - p % w) / w); }
    const P = X.length; if (P < 30) return null;
    let best = null, bn = 0;
    for (let it = 0; it < 600; it++) {
      const i = (zf() * P) | 0, j = (zf() * P) | 0; if (i === j) continue;
      let vx = X[j] - X[i], vy = Y[j] - Y[i]; const L = Math.hypot(vx, vy); if (L < 20) continue; vx /= L; vy /= L;
      const nx = -vy, ny = vx; let m = 0;
      for (let p = 0; p < P; p++) if (Math.abs((X[p] - X[i]) * nx + (Y[p] - Y[i]) * ny) < 2.0) m++;
      if (m > bn) { bn = m; best = [X[i], Y[i], nx, ny]; }
    }
    if (!best) return null;
    // Hauptachse der Inlier (Hauptkomponentenanalyse): ϑ = ½ atan2(2 s_xy, s_xx − s_yy)
    const [px, py, nx, ny] = best; let mx = 0, my = 0, n = 0;
    for (let p = 0; p < P; p++) if (Math.abs((X[p] - px) * nx + (Y[p] - py) * ny) < 2.0) { mx += X[p]; my += Y[p]; n++; }
    mx /= n; my /= n; let sxx = 0, sxy = 0, syy = 0;
    for (let p = 0; p < P; p++) if (Math.abs((X[p] - px) * nx + (Y[p] - py) * ny) < 2.0) { const dx = X[p] - mx, dy = Y[p] - my; sxx += dx * dx; sxy += dx * dy; syy += dy * dy; }
    const th = 0.5 * Math.atan2(2 * sxy, sxx - syy); let dx = Math.cos(th), dy = Math.sin(th);
    if ((mx - cx) * dx + (my - cy) * dy < 0) { dx = -dx; dy = -dy; }             // vom Skaleninneren nach außen
    return { px: mx, py: my, dx, dy, n, kandidaten: P, schwelle: sw, winkel: (Math.atan2(-dy, dx) * deg + 360) % 360 };
  }

  // ------------------------------------------------------------------ Ablesung am Schnittpunkt
  function schnittpunkt(L, geo) {
    const ox = L.px - geo.cx, oy = L.py - geo.cy, bq = 2 * (ox * L.dx + oy * L.dy), cq = ox * ox + oy * oy - geo.r * geo.r, disc = bq * bq - 4 * cq;
    if (disc < 0) return null;
    const t = Math.max((-bq + Math.sqrt(disc)) / 2, (-bq - Math.sqrt(disc)) / 2), sx = L.px + t * L.dx, sy = L.py + t * L.dy;
    return { sx, sy, winkel: (Math.atan2(-(sy - geo.cy), sx - geo.cx) * deg + 360) % 360 };
  }
  function wertAusWinkel(ws, haupt, bereich) {  // Hauptstriche nach fallendem Winkel = steigender Wert; stückweise linear, außen linear fortgesetzt
    const tw = [...haupt].sort((a, b) => b - a), n = tw.length, werte = tw.map((_, k) => bereich * k / (n - 1));
    const xs = tw.map(a => -a), x = -ws;            // monoton steigend
    if (x <= xs[0]) return werte[0] + (werte[1] - werte[0]) * (x - xs[0]) / (xs[1] - xs[0]);
    if (x >= xs[n - 1]) return werte[n - 2] + (werte[n - 1] - werte[n - 2]) * (x - xs[n - 2]) / (xs[n - 1] - xs[n - 2]);
    for (let k = 1; k < n; k++) if (x <= xs[k]) return werte[k - 1] + (werte[k] - werte[k - 1]) * (x - xs[k - 1]) / (xs[k] - xs[k - 1]);
    return werte[n - 1];
  }

  // ------------------------------------------------------------------ alles zusammen
  function lesen(rgb, w, h, opt) {
    opt = Object.assign({ bereich: 1.0, saat: 1, ref_striche: 26, ref_haupt: 6 }, opt || {});
    const zf = zufall(opt.saat), t0 = Date.now();
    const g = grau(rgb, w, h), d9 = duenneDunkleLinien(g, w, h, 9);
    const geo = bogenFinden(d9, w, h, zf);
    const erg = { bogen: geo, skala_gueltig: false, zeiger: null, schnitt: null, wert: null, gueltig: false, hinweise: [] };
    if (!geo) { erg.hinweise.push('kein Bogen gefunden'); erg.dauer_ms = Date.now() - t0; return erg; }
    const st = stricheFinden(g, w, h, geo); erg.striche = st;
    erg.skala_gueltig = geo.inlier >= 200 && st.regel !== null && st.regel < 0.08 && st.haupt.length === opt.ref_haupt && Math.abs(st.striche.length - opt.ref_striche) <= 1;
    if (!erg.skala_gueltig) erg.hinweise.push(`Skala unsicher: ${geo.inlier} Inlier, ${st.striche.length} Striche, ${st.haupt.length} Hauptstriche, Regelmäßigkeit ${st.regel === null ? '—' : st.regel.toFixed(3)}`);
    const rb = rotbild(rgb, w, h, geo); erg.weissabgleich = rb.faktoren; erg.rot = rb.rot;
    const L = zeigerGerade(rb.rot, w, h, geo, zf); erg.zeiger = L;
    if (!L) { erg.hinweise.push('kein Zeiger gefunden'); erg.dauer_ms = Date.now() - t0; return erg; }
    if (L.n < 40) erg.hinweise.push(`Zeiger unsicher: ${L.n} Punkte`);
    const S = schnittpunkt(L, geo); erg.schnitt = S;
    if (S && st.haupt.length >= 2) erg.wert = wertAusWinkel(S.winkel, st.haupt, opt.bereich);
    erg.gueltig = erg.skala_gueltig && L.n >= 40 && S !== null;
    erg.grau = g; erg.d = d9; erg.dauer_ms = Date.now() - t0;
    return erg;
  }

  return { lesen, grau, duenneDunkleLinien, bogenFinden, stricheFinden, rotbild, zeigerGerade, schnittpunkt, wertAusWinkel, zufall, perzentil };
})();
if (typeof module !== 'undefined') module.exports = ANALOG;
