// ---------------------------------------------------------------------------------------------------------------------
//  c_skala: Skalenerkennung in jedem Bild – unterer schwarzer Bogen, Teilstriche, Hauptstriche, Ziffern (Port von skala_v2.py)
//  (1) Bogen: dünne dunkle Strukturen im Graubild (Kern 9) → Schwelle → größter zusammenhängender Bereich mit > 30 % Bildbreite
//      → RANSAC-Kreis (Toleranz 1,5 px, Mittelpunkt unterhalb) → Verfeinerung nach kleinsten Quadraten
//  (2) Striche: Spitzen der Dunkelheit (Kern 5) im Winkelprofil 5–11 px außen am Bogen; radiale Länge → Hauptstriche > 1,5·Median
//  (3) Ziffern: dunkle Bereiche im Ring r+32 … r+115, je Hauptstrich höchstens ein Kasten (±9°)
//  Offline geprüft (16.09.): 46 von 76 Bildern exakt 26 Striche / 6 Hauptstriche, Fehlschläge nur bei Sättigung/Unterbelichtung.
// ---------------------------------------------------------------------------------------------------------------------
static const float BOGEN_TOL = 2.5f;                                             // Punkte aus halber Auflösung (±1 px Quantisierung)
static int komponenten(uint8_t schwelle, int& besteLab, int& bx0, int& bx1) {
  // Flutfüllung (4er-Nachbarschaft) über DUENN >= schwelle; LAB = Bereichsnummer (0 = keiner). Rückgabe: Zahl der Bereiche;
  // besteLab = größter Bereich mit Breite > 0,3·W, dessen Punkte oberhalb der Bildmitte liegen (Skala steht oben).
  memset(LAB, 0, WK * HK * 2); int n = 0, bestN = 0; besteLab = 0;            // arbeitet auf DUENNK (halbe Auflösung)
  for (int y = 0; y < HK; y++) for (int x = 0; x < WK; x++) {
    int i = y * WK + x; if (DUENNK[i] < schwelle || LAB[i]) continue;
    if (n >= 32000) return n;
    n++; int sp = 0; STACK[sp++] = x; STACK[sp++] = y; LAB[i] = n; int cnt = 0, xmin = x, xmax = x, ymax = y;
    while (sp > 0) {
      int py = STACK[--sp], px = STACK[--sp]; cnt++; if (px < xmin) xmin = px; if (px > xmax) xmax = px; if (py > ymax) ymax = py;
      const int nx[4] = {px - 1, px + 1, px, px}, ny[4] = {py, py, py - 1, py + 1};
      for (int k = 0; k < 4; k++) { int qx = nx[k], qy = ny[k]; if (qx < 0 || qy < 0 || qx >= WK || qy >= HK) continue; int j = qy * WK + qx;
        if (DUENNK[j] >= schwelle && !LAB[j]) { LAB[j] = n; if (sp + 2 <= W * H * 2) { STACK[sp++] = qx; STACK[sp++] = qy; } } }
    }
    if (cnt >= 100 && xmax - xmin > 0.3f * WK && cnt > bestN) { bestN = cnt; besteLab = n; bx0 = xmin; bx1 = xmax; }
  }
  return n;
}
static bool kreisDurch(float ax, float ay, float bx, float by, float qx, float qy, float& ux, float& uy, float& r) {
  float d = 2 * (ax * (by - qy) + bx * (qy - ay) + qx * (ay - by)); if (fabsf(d) < 1e-3f) return false;
  ux = ((ax * ax + ay * ay) * (by - qy) + (bx * bx + by * by) * (qy - ay) + (qx * qx + qy * qy) * (ay - by)) / d;
  uy = ((ax * ax + ay * ay) * (qx - bx) + (bx * bx + by * by) * (ax - qx) + (qx * qx + qy * qy) * (bx - ax)) / d;
  r = sqrtf((ax - ux) * (ax - ux) + (ay - uy) * (ay - uy)); return true;
}
static bool bogenFinden(Geometrie& g) {
  verkleinern(GRAU, GRAUK); duenneStrukturenK(GRAUK, 5, 0, (int)(0.75f * HK));    // halbe Auflösung, Skala steht oben
  uint8_t schwelle = (uint8_t)max(15, (int)(0.3f * perzentil(DUENNK, WK * HK, 2, 0.995f)));
  int lab, bx0 = 0, bx1 = 0; komponenten(schwelle, lab, bx0, bx1); if (!lab) return false;
  // Punkte des Bereichs sammeln (volle Koordinaten = 2·xk + 1), höchstens 4000
  static uint16_t PX[4000], PY[4000]; int np = 0, gesamt = 0, ymax = 0;
  for (int i = 0; i < WK * HK; i++) if (LAB[i] == lab) gesamt++;
  int stride = gesamt > 4000 ? (gesamt + 3999) / 4000 : 1, z = 0;
  for (int i = 0; i < WK * HK; i++) if (LAB[i] == lab) { if ((z++ % stride) == 0 && np < 4000) { PX[np] = 2 * (i % WK) + 1; PY[np] = 2 * (i / WK) + 1; if (PY[np] > ymax) ymax = PY[np]; np++; } }
  if (np < 60) return false;
  float bcx = 0, bcy = 0, br = 0; int bn = 0;
  for (int it = 0; it < 600; it++) {
    int i1 = rnd() % np, i2 = rnd() % np, i3 = rnd() % np; if (i1 == i2 || i2 == i3 || i1 == i3) continue;
    float ux, uy, r; if (!kreisDurch(PX[i1], PY[i1], PX[i2], PY[i2], PX[i3], PY[i3], ux, uy, r)) continue;
    if (r < 150 || r > 500 || uy < ymax) continue;                            // Mittelpunkt unterhalb des Bogens
    int n = 0; for (int k = 0; k < np; k += 2) { float dx = PX[k] - ux, dy = PY[k] - uy; if (fabsf(sqrtf(dx * dx + dy * dy) - r) < BOGEN_TOL) n++; }
    if (n > bn) { bn = n; bcx = ux; bcy = uy; br = r; }
  }
  if (bn < 30) return false;
  for (int runde = 0; runde < 2; runde++) {                                    // kleinste Quadrate: x²+y² + D x + E y + F = 0
    double S[3][3] = {{0}}, t[3] = {0}; int m = 0;
    for (int k = 0; k < np; k++) { float dx = PX[k] - bcx, dy = PY[k] - bcy; if (fabsf(sqrtf(dx * dx + dy * dy) - br) >= BOGEN_TOL) continue;
      double v[3] = {(double)PX[k], (double)PY[k], 1.0}, b = -((double)PX[k] * PX[k] + (double)PY[k] * PY[k]); m++;
      for (int a = 0; a < 3; a++) { t[a] += v[a] * b; for (int c = 0; c < 3; c++) S[a][c] += v[a] * v[c]; } }
    if (m < 30) return false;
    for (int c = 0; c < 3; c++) { int piv = c; for (int a = c + 1; a < 3; a++) if (fabs(S[a][c]) > fabs(S[piv][c])) piv = a; if (fabs(S[piv][c]) < 1e-9) return false;
      if (piv != c) { for (int j = 0; j < 3; j++) { double tmp = S[c][j]; S[c][j] = S[piv][j]; S[piv][j] = tmp; } double tmp = t[c]; t[c] = t[piv]; t[piv] = tmp; }
      for (int a = 0; a < 3; a++) if (a != c) { double f = S[a][c] / S[c][c]; for (int j = 0; j < 3; j++) S[a][j] -= f * S[c][j]; t[a] -= f * t[c]; } }
    double D = t[0] / S[0][0], E = t[1] / S[1][1], F = t[2] / S[2][2]; double ux = -D / 2, uy = -E / 2, rq = ux * ux + uy * uy - F; if (rq <= 0) return false;
    bcx = (float)ux; bcy = (float)uy; br = (float)sqrt(rq);
  }
  // Winkelbereich der Inlier (1-%-/99-%-Quantile über ein 1°-Histogramm)
  uint16_t hist[360] = {0}; int inl = 0;
  for (int k = 0; k < np; k++) { float dx = PX[k] - bcx, dy = PY[k] - bcy; if (fabsf(sqrtf(dx * dx + dy * dy) - br) >= BOGEN_TOL) continue; int w = (int)winkelGrad(dx, dy) % 360; hist[w]++; inl++; }
  int akk = 0; float a0 = 0, a1 = 180; for (int w = 0; w < 360; w++) { akk += hist[w]; if (akk >= inl / 100) { a0 = w; break; } }
  akk = 0; for (int w = 359; w >= 0; w--) { akk += hist[w]; if (akk >= inl / 100) { a1 = w + 1; break; } }
  g.cx = bcx; g.cy = bcy; g.r = br; g.inlier = inl * stride; g.a0 = a0; g.a1 = a1;
  return inl >= 30 && a1 - a0 > 40 && a1 - a0 < 170;
}
static void stricheFinden(Geometrie& g) {
  duenneStrukturen(GRAU, 5, (int)(g.cy - g.r - 65), (int)(g.cy - g.r * 0.55f));   // nur das Band der Striche (r+3 … r+60) im Sektor
  float lo = g.a0 - 2, hi = g.a1 + 2; int n = (int)((hi - lo) / 0.1f); if (n > 1800) n = 1800;
  static float prof[1800];
  for (int a = 0; a < n; a++) {
    float t = (lo + a * 0.1f) * M_PI / 180.0f, c = cosf(t), s = sinf(t), su = 0; int z = 0;
    for (float rr = g.r + 5; rr <= g.r + 11.1f; rr += 2) { int x = (int)(g.cx + rr * c), y = (int)(g.cy - rr * s); if (x >= 0 && y >= 0 && x < W && y < H) { su += DUENN[y * W + x]; z++; } }
    prof[a] = z ? su / z : 0;
  }
  for (int a = 1; a < n - 1; a++) prof[a] = (prof[a - 1] + prof[a] + prof[a + 1]) / 3.0f;   // 0,3° Glättung (in place, geringe Verzerrung)
  float p99 = 0; { static float tmp[1800]; memcpy(tmp, prof, n * sizeof(float)); std::nth_element(tmp, tmp + (n * 99) / 100, tmp + n); p99 = tmp[(n * 99) / 100]; }
  float sw = fmaxf(10.0f, 0.25f * p99);
  g.nStriche = 0; g.nHaupt = 0; static float laengen[64]; int ns = 0;
  for (int a = 1; a < n - 1 && ns < 64; a++) {
    if (prof[a] < sw || prof[a] < prof[a - 1] || prof[a] < prof[a + 1]) continue;
    int j = a; while (j + 1 < n && prof[j + 1] == prof[a]) j++;
    float mitte = lo + 0.5f * (a + j) * 0.1f, t = mitte * M_PI / 180.0f, laenge = 0;
    for (float rr = g.r + 3; rr < g.r + 60; rr += 1.0f) { int x = (int)(g.cx + rr * cosf(t)), y = (int)(g.cy - rr * sinf(t)); if (x < 0 || y < 0 || x >= W || y >= H) break;
      if (DUENN[y * W + x] > sw * 0.6f) laenge = rr - g.r; else if (rr - g.r > laenge + 3) break; }
    if (ns == 0 || fabsf(g.striche[ns - 1] - mitte) >= 0.6f) { g.striche[ns] = mitte; laengen[ns] = laenge; ns++; }
    else if (laenge > laengen[ns - 1]) { g.striche[ns - 1] = mitte; laengen[ns - 1] = laenge; }
    a = j + 6;
  }
  g.nStriche = ns; if (ns < 5) return;
  static float tmp[64]; memcpy(tmp, laengen, ns * sizeof(float)); std::nth_element(tmp, tmp + ns / 2, tmp + ns); float lmed = tmp[ns / 2];
  for (int i = 0; i < ns && g.nHaupt < 12; i++) if (laengen[i] > 1.5f * lmed) g.haupt[g.nHaupt++] = g.striche[i];
  // Striche fallend sortieren (vom Skalenanfang links = großer Winkel zum Ende rechts) und Abstand/Regelmäßigkeit
  std::sort(g.striche, g.striche + ns, [](float a, float b) { return a > b; }); std::sort(g.haupt, g.haupt + g.nHaupt, [](float a, float b) { return a > b; });
  static float diff[64]; for (int i = 0; i + 1 < ns; i++) diff[i] = g.striche[i] - g.striche[i + 1];
  memcpy(tmp, diff, (ns - 1) * sizeof(float)); std::nth_element(tmp, tmp + (ns - 1) / 2, tmp + ns - 1); g.pitch = tmp[(ns - 1) / 2];
  float s2 = 0; for (int i = 0; i + 1 < ns; i++) s2 += (diff[i] - g.pitch) * (diff[i] - g.pitch); g.regel = g.pitch > 0 ? sqrtf(s2 / (ns - 1)) / g.pitch : 9;
}
static void ziffernFinden(Geometrie& g) {
  // dunkle Bereiche im Ring r+30 … r+90 (Sektor ±7°): die Zahlen des 44C2 liegen bei r+45 … r+75, der Normdruck „GB/T7676-98“ bei r+120, Schwelle relativ zum Weiß (90-%-Wert) des Rings; Flutfüllung auf GRAU
  g.nZiffern = 0; for (int k = 0; k < 12; k++) g.ziffer[k][0] = g.ziffer[k][1] = g.ziffer[k][2] = g.ziffer[k][3] = -1;
  if (g.nHaupt < 2) return;
  int x0 = max(0, (int)(g.cx - g.r - 95)), x1 = min(W - 1, (int)(g.cx + g.r + 95)), y0 = max(0, (int)(g.cy - g.r - 95)), y1 = min(H - 1, (int)g.cy);
  uint32_t hist[256] = {0}; uint32_t n = 0;
  auto imRing = [&](int x, int y) { float ex = x - g.cx, ey = y - g.cy, rr = sqrtf(ex * ex + ey * ey); if (rr < g.r + 30 || rr > g.r + 90) return false; float w = winkelGrad(ex, ey); return w >= g.a0 - 7 && w <= g.a1 + 7; };   // Endziffern (0, 1.0) liegen weiter außen und leicht außerhalb des Bogens
  for (int y = y0; y <= y1; y += 4) for (int x = x0; x <= x1; x += 4) if (imRing(x, y)) { hist[GRAU[y * W + x]]++; n++; }
  if (n < 50) return;
  uint32_t akk = 0; int p90 = 255; for (int v = 0; v < 256; v++) { akk += hist[v]; if (akk >= n * 90 / 100) { p90 = v; break; } }
  int sw = (int)(p90 - 0.20f * p90);                                            // Endziffern „0“ und „1.0“ sind dünner gedruckt (nur ≈ 20–25 % dunkler als das Weiß)
  // Markierung der dunklen Ringpixel in LAB (0/1), dann Bereiche mit Flutfüllung, Kasten und Winkel je Bereich
  for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) { int i = y * W + x; LAB[i] = (GRAU[i] < sw && imRing(x, y)) ? -1 : 0; }   // imRing nur für dunkle Punkte (&&-Kurzschluss)
  struct K { int x0, y0, x1, y1, px; float w; } kaesten[40]; int nk = 0;
  for (int y = y0; y <= y1 && nk < 40; y++) for (int x = x0; x <= x1 && nk < 40; x++) {
    if (LAB[y * W + x] != -1) continue; int sp = 0; STACK[sp++] = x; STACK[sp++] = y; LAB[y * W + x] = 1; K k = {x, y, x, y, 0, 0};
    while (sp > 0) { int py = STACK[--sp], px = STACK[--sp]; k.px++; if (px < k.x0) k.x0 = px; if (px > k.x1) k.x1 = px; if (py < k.y0) k.y0 = py; if (py > k.y1) k.y1 = py;
      const int nx[4] = {px - 1, px + 1, px, px}, ny[4] = {py, py, py - 1, py + 1};
      for (int q = 0; q < 4; q++) { int qx = nx[q], qy = ny[q]; if (qx < x0 || qy < y0 || qx > x1 || qy > y1) continue; if (LAB[qy * W + qx] == -1) { LAB[qy * W + qx] = 1; STACK[sp++] = qx; STACK[sp++] = qy; } } }
    int hh = k.y1 - k.y0 + 1, bb = k.x1 - k.x0 + 1; if (k.px < 25 || hh < 8 || hh > 45 || bb < 4 || bb > 60) continue;
    k.w = winkelGrad(0.5f * (k.x0 + k.x1) - g.cx, 0.5f * (k.y0 + k.y1) - g.cy); kaesten[nk++] = k;
  }
  // je Hauptstrich: alle Kästen im Umkreis ±6° (Breite ≤ 70 px) zu einem Kasten vereinen
  for (int hI = 0; hI < g.nHaupt; hI++) {
    int X0 = 1 << 20, Y0 = 1 << 20, X1 = -1, Y1 = -1, pxs = 0;
    for (int i = 0; i < nk; i++) if (fabsf(kaesten[i].w - g.haupt[hI]) < 6 && kaesten[i].x1 - kaesten[i].x0 <= 70) { X0 = min(X0, kaesten[i].x0); Y0 = min(Y0, kaesten[i].y0); X1 = max(X1, kaesten[i].x1); Y1 = max(Y1, kaesten[i].y1); pxs += kaesten[i].px; }
    if (pxs >= 25) { g.ziffer[hI][0] = X0; g.ziffer[hI][1] = Y0; g.ziffer[hI][2] = X1; g.ziffer[hI][3] = Y1; g.nZiffern++; }
  }
}
// Gesamtbewertung: Bogen gefunden, Strichzahl passt (wenn bekannt), Hauptstriche passen, Abstände regelmäßig
static void skalaBewerten(Geometrie& g) {
  float p = 0; int m = 0;
  m++; if (g.inlier >= 200) p++;
  m++; if (g.nHaupt >= 2 && (KAL.nHaupt == 0 || g.nHaupt == KAL.nHaupt)) p++;
  m++; if (g.nStriche >= 5 && (KAL.nStriche == 0 || abs(g.nStriche - KAL.nStriche) <= 1)) p++;
  m++; if (g.regel >= 0 && g.regel < 0.08f) p++;
  m++; if (g.nZiffern >= max(2, g.nHaupt - 2)) p++;
  g.guete = p / m;
  // Gültig: Bogen mit ≥ 200 Inliern, regelmäßige Teilung; mit bekannter Referenz genau deren Hauptstrichzahl und Strichzahl ±1,
  // ohne Referenz mindestens 4 Hauptstriche und 10 Striche (16.09.: bei Übersteuerung galten 10/2 als „Skala ok“ – nie wieder).
  bool ref = KAL.nHaupt > 0;
  g.ok = g.inlier >= 200 && g.regel >= 0 && g.regel < 0.08f && (ref ? (g.nHaupt == KAL.nHaupt && abs(g.nStriche - KAL.nStriche) <= 1) : (g.nHaupt >= 4 && g.nStriche >= 10));
  // Referenz selbst lernen: 30 Bilder in Folge dieselbe Strich- und Hauptstrichzahl mit sehr regelmäßiger Teilung
  static int gleich = 0, letztH = -1, letztS = -1;
  if (!ref && g.ok && g.regel < 0.05f) {
    if (g.nHaupt == letztH && g.nStriche == letztS) gleich++; else { gleich = 1; letztH = g.nHaupt; letztS = g.nStriche; }
    if (gleich >= 30) { KAL.nHaupt = g.nHaupt; KAL.nStriche = g.nStriche; kalSpeichern(); KONSOLE.printf("[SKALA] Referenz gelernt: %d Hauptstriche, %d Striche\n", KAL.nHaupt, KAL.nStriche); }
  }
}
// Schneller Weg (Bildtakt): Bogen des letzten gültigen Bilds prüfen – radial ±5 px um den Kreis die dünne Struktur suchen;
// ≥ 80 % der Winkel belegt → Kreis aus diesen Punkten neu ausgleichen (kleinste Quadrate), sonst volle Suche.
static bool bogenPruefen(const Geometrie& alt, Geometrie& g) {
  int y0 = max(0, (int)((alt.cy - alt.r - 130) / 2)), y1 = min(HK, (int)((alt.cy - alt.r * 0.2f) / 2));
  uint32_t t0 = millis(); verkleinern(GRAU, GRAUK); duenneStrukturenK(GRAUK, 5, y0, y1); tDuenn = millis() - t0; t0 = millis();
  uint8_t schwelle = (uint8_t)max(15, (int)(0.3f * perzentil(DUENNK + y0 * WK, (y1 - y0) * WK, 2, 0.995f)));
  double S[3][3] = {{0}}, t[3] = {0}; int m = 0, gesamt = 0;
  for (float a = alt.a0; a <= alt.a1; a += 0.5f) {
    float th = a * M_PI / 180.0f, c = cosf(th), si = sinf(th); gesamt++; int bestv = -1; float bestr = 0;
    for (float rr = alt.r - 6; rr <= alt.r + 6.1f; rr += 2.0f) { int x = (int)((alt.cx + rr * c) / 2), y = (int)((alt.cy - rr * si) / 2); if (x < 0 || y < 0 || x >= WK || y >= HK) continue; int v = DUENNK[y * WK + x]; if (v >= schwelle && v > bestv) { bestv = v; bestr = rr; } }
    if (bestv < 0) continue;
    double px = alt.cx + bestr * c, py = alt.cy - bestr * si, v[3] = {px, py, 1.0}, b = -(px * px + py * py); m++;
    for (int i = 0; i < 3; i++) { t[i] += v[i] * b; for (int j = 0; j < 3; j++) S[i][j] += v[i] * v[j]; }
  }
  if (gesamt < 20 || m < 0.8f * gesamt) return false;
  for (int c = 0; c < 3; c++) { int piv = c; for (int a = c + 1; a < 3; a++) if (fabs(S[a][c]) > fabs(S[piv][c])) piv = a; if (fabs(S[piv][c]) < 1e-9) return false;
    if (piv != c) { for (int j = 0; j < 3; j++) { double tmp = S[c][j]; S[c][j] = S[piv][j]; S[piv][j] = tmp; } double tmp = t[c]; t[c] = t[piv]; t[piv] = tmp; }
    for (int a = 0; a < 3; a++) if (a != c) { double f = S[a][c] / S[c][c]; for (int j = 0; j < 3; j++) S[a][j] -= f * S[c][j]; t[a] -= f * t[c]; } }
  double D = t[0] / S[0][0], E = t[1] / S[1][1], F = t[2] / S[2][2]; double ux = -D / 2, uy = -E / 2, rq = ux * ux + uy * uy - F; if (rq <= 0) return false;
  g.cx = (float)ux; g.cy = (float)uy; g.r = (float)sqrt(rq); g.a0 = alt.a0; g.a1 = alt.a1; g.inlier = m * 4; tPruef = millis() - t0;
  return fabsf(g.r - alt.r) < 0.1f * alt.r && fabsf(g.cx - alt.cx) < 40 && fabsf(g.cy - alt.cy) < 40;
}
static uint32_t volleSuchen = 0, schnelleSuchen = 0;
static Geometrie skalaErkennen() {
  Geometrie g = {}; uint32_t t0 = millis(); bool ok = false;
  if (SKALA_GUT.ok && (bildzaehler % 50)) { ok = bogenPruefen(SKALA_GUT, g); if (ok) schnelleSuchen++; }   // alle 50 Bilder erzwungen volle Suche
  if (!ok) { g = {}; ok = bogenFinden(g); if (ok) volleSuchen++; }
  if (ok) { uint32_t t1 = millis(); stricheFinden(g); tStriche = millis() - t1; t1 = millis();
    // Ziffernkästen ändern sich nicht von Bild zu Bild: nur alle 10 Bilder (oder ohne gültige Vorlage) neu suchen, sonst übernehmen
    if (!SKALA_GUT.ok || (bildzaehler % 10) == 0 || SKALA_GUT.nHaupt != g.nHaupt) ziffernFinden(g);
    else { g.nZiffern = SKALA_GUT.nZiffern; memcpy(g.ziffer, SKALA_GUT.ziffer, sizeof(g.ziffer)); }
    tZiffern = millis() - t1; skalaBewerten(g); }
  g.ms = millis() - t0; return g;
}
// Einzeichnung wie im Beispielbild: Bogen orange, Teilstriche cyan, Hauptstriche magenta, Ziffernkästen grün, Werte unter den Hauptstrichen
static const uint16_t F_ORANGE = 0xFC60, F_CYAN = 0x07FF, F_MAGENTA = 0xF81F, F_GRUEN = 0x07E0, F_BLAU = 0x02DF, F_WEISS = 0xFFFF, F_ROT = 0xF800;
static void skalaZeichnen(uint8_t* f, const Geometrie& g) {
  if (!g.inlier) return;
  bogenZeichnen(f, g.cx, g.cy, g.r, g.a0, g.a1, F_ORANGE, 2);
  for (int i = 0; i < g.nStriche; i++) { float t = g.striche[i] * M_PI / 180.0f; bool haupt = false; for (int k = 0; k < g.nHaupt; k++) if (fabsf(g.haupt[k] - g.striche[i]) < 0.05f) haupt = true;
    float L = haupt ? 24 : 14; linieZeichnen(f, (int)(g.cx + (g.r + 2) * cosf(t)), (int)(g.cy - (g.r + 2) * sinf(t)), (int)(g.cx + (g.r + L) * cosf(t)), (int)(g.cy - (g.r + L) * sinf(t)), haupt ? F_MAGENTA : F_CYAN, haupt ? 3 : 1); }
  for (int k = 0; k < g.nHaupt; k++) {
    if (g.ziffer[k][0] >= 0) rechteckZeichnen(f, g.ziffer[k][0] - 2, g.ziffer[k][1] - 2, g.ziffer[k][2] + 2, g.ziffer[k][3] + 2, F_GRUEN);
    if (KAL.ok && g.nHaupt > 1) { float wert = KAL.bereich * k / (g.nHaupt - 1); char s[8]; snprintf(s, sizeof(s), wert < 0.995f ? "%.1f" : "%.0f", wert);
      float t = g.haupt[k] * M_PI / 180.0f; textZeichnen(f, (int)(g.cx + (g.r - 22) * cosf(t)) - 6, (int)(g.cy - (g.r - 22) * sinf(t)) - 4, s, F_MAGENTA, F_WEISS, 1); }
  }
  pixel(f, (int)g.cx, (int)g.cy, F_ORANGE); bogenZeichnen(f, g.cx, g.cy, 4, 0, 360, F_ORANGE, 1);
}
