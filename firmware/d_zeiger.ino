// ---------------------------------------------------------------------------------------------------------------------
//  d_zeiger: Zeiger relativ zur erkannten Skala (Port von zeiger_v2.py)
//  Rotbild mit Weißabgleich → dünne dunkle Strukturen (Kern 9) innerhalb des Bogens (0,3 r … r−4, Sektor ±6°) →
//  Kandidaten (98,5-%-Quantil) → RANSAC-Gerade (Toleranz 2 px) → PCA über die Inlier; Richtung vom Mittelpunkt nach außen.
//  Ablesung nach Nutzervorschlag (16.09.): Schnittpunkt der Zeigergeraden mit dem Skalenbogen; dort der Winkel vom Bogen-
//  mittelpunkt, stückweise linear zwischen den Hauptstrichen – wie ein Mensch an der Zeigerspitze abliest. Kein Drehpunkt,
//  keine Parallaxe, keine Geometrie-Kalibrierung. Offline: 10 Prüfströme max 0,011 mA, Mittel 0,004 (Drehpunktvariante 0,013).
// ---------------------------------------------------------------------------------------------------------------------
static const int ZK_MAX = 5000; static uint16_t ZKX[ZK_MAX], ZKY[ZK_MAX];
static bool zeigerGerade(const Geometrie& g, Linie& L) {
  // halbe Auflösung: Rotbild verkleinern, dünne Strukturen (Kern 5 ≙ 9 px), Kandidaten in halben Koordinaten → volle (2·x+1)
  uint32_t t0 = millis(); verkleinern(ROT, ROTK); duenneStrukturenK(ROTK, 5, (int)((g.cy - g.r - 4) / 2), (int)((g.cy + 0.2f * g.r) / 2)); tZDuenn = millis() - t0; t0 = millis();
  int x0 = max(0, (int)((g.cx - g.r) / 2)), x1 = min(WK - 1, (int)((g.cx + g.r) / 2)), y0 = max(0, (int)((g.cy - g.r) / 2)), y1 = min(HK - 1, (int)((g.cy + 0.2f * g.r) / 2));
  uint32_t hist[256] = {0}; uint32_t n = 0;
  auto inZone = [&](int xk, int yk) { float ex = 2 * xk + 1 - g.cx, ey = 2 * yk + 1 - g.cy, rr = sqrtf(ex * ex + ey * ey); if (rr < 0.3f * g.r || rr > g.r - 4) return false; float w = winkelGrad(ex, ey); return w >= g.a0 - 6 && w <= g.a1 + 6; };
  for (int y = y0; y <= y1; y += 2) for (int x = x0; x <= x1; x += 2) { int v = DUENNK[y * WK + x]; if (v >= 6 && inZone(x, y)) { hist[v]++; n++; } else if (v < 6) { hist[0]++; n++; } }
  if (n < 100) return false;
  uint32_t akk = 0; int sw = 12; for (int v = 0; v < 256; v++) { akk += hist[v]; if (akk >= n * 985 / 1000) { sw = max(12, v); break; } }
  int k = 0; for (int y = y0; y <= y1 && k < ZK_MAX; y++) for (int x = x0; x <= x1 && k < ZK_MAX; x++) { int v = DUENNK[y * WK + x]; if (v >= sw && inZone(x, y)) { ZKX[k] = 2 * x + 1; ZKY[k] = 2 * y + 1; k++; } }
  if (k < 30) return false; tZKand = millis() - t0; t0 = millis();
  int bn = 0; float bpx = 0, bpy = 0, bdx = 0, bdy = 0;
  for (int it = 0; it < 500; it++) {
    int i = rnd() % k, j = rnd() % k; if (i == j) continue; float vx = (float)ZKX[j] - ZKX[i], vy = (float)ZKY[j] - ZKY[i], Ln = sqrtf(vx * vx + vy * vy); if (Ln < 20) continue;
    vx /= Ln; vy /= Ln; float nx = -vy, ny = vx; int m = 0;
    for (int q = 0; q < k; q += 2) { float d = fabsf((ZKX[q] - (float)ZKX[i]) * nx + (ZKY[q] - (float)ZKY[i]) * ny); if (d < 3.0f) m++; }
    if (m > bn) { bn = m; bpx = ZKX[i]; bpy = ZKY[i]; bdx = vx; bdy = vy; }
  }
  if (bn < 15) return false;
  // PCA über die Inlier
  float nx = -bdy, ny = bdx, sx = 0, sy = 0; int m = 0;
  for (int q = 0; q < k; q++) { float d = fabsf((ZKX[q] - bpx) * nx + (ZKY[q] - bpy) * ny); if (d < 3.0f) { sx += ZKX[q]; sy += ZKY[q]; m++; } }
  float mx = sx / m, my = sy / m, sxx = 0, syy = 0, sxy = 0;
  for (int q = 0; q < k; q++) { float d = fabsf((ZKX[q] - bpx) * nx + (ZKY[q] - bpy) * ny); if (d < 3.0f) { float ex = ZKX[q] - mx, ey = ZKY[q] - my; sxx += ex * ex; syy += ey * ey; sxy += ex * ey; } }
  float th = 0.5f * atan2f(2 * sxy, sxx - syy), dx = cosf(th), dy = sinf(th);
  if ((mx - g.cx) * dx + (my - g.cy) * dy < 0) { dx = -dx; dy = -dy; }
  L.px = mx; L.py = my; L.dx = dx; L.dy = dy; L.n = m; L.ok = true; tZRansac = millis() - t0; return true;
}
// Wert aus dem Zeigerwinkel: Hauptstrichfüße auf dem Bogen, Winkel vom Drehpunkt P aus; stückweise linear
static float wertAusWinkel(const Geometrie& g, float zw, float bereich) {   // zw: Winkel des Schnittpunkts vom Bogenmittelpunkt
  if (g.nHaupt < 2) return NAN;
  const float* tw = g.haupt;
  // tw fällt mit k; Wert steigt mit k
  if (zw >= tw[0]) return bereich * ((tw[0] - zw) / (tw[0] - tw[1]));                 // vor dem Anfang: linear extrapoliert (negativ)
  for (int k = 0; k + 1 < g.nHaupt; k++) if (zw <= tw[k] && zw >= tw[k + 1]) { float f = (tw[k] - zw) / (tw[k] - tw[k + 1]); return bereich * (k + f) / (g.nHaupt - 1); }
  int k = g.nHaupt - 2; float f = (tw[k] - zw) / (tw[k] - tw[k + 1]); return bereich * (k + f) / (g.nHaupt - 1);
}
static bool schnittMitBogen(const Geometrie& g, const Linie& L, float& sx, float& sy) {
  float ox = L.px - g.cx, oy = L.py - g.cy, b = 2 * (ox * L.dx + oy * L.dy), c = ox * ox + oy * oy - g.r * g.r, disc = b * b - 4 * c;
  if (disc < 0) return false; float t = (-b + sqrtf(disc)) / 2;                       // in Zeigerrichtung (nach außen)
  sx = L.px + t * L.dx; sy = L.py + t * L.dy; return true;
}
static Zeiger zeigerMessen(const Geometrie& g) {
  Zeiger z = {}; uint32_t t0 = millis();
  if (!zeigerGerade(g, z.linie)) { dauerZeigerMs = millis() - t0; return z; }
  float sx, sy;
  if (schnittMitBogen(g, z.linie, sx, sy)) {
    z.winkel = winkelGrad(sx - g.cx, sy - g.cy);                                     // Winkel des Schnittpunkts vom Bogenmittelpunkt
    z.wert = KAL.ok ? wertAusWinkel(g, z.winkel, KAL.bereich) : NAN;
  }
  z.sicherheit = z.linie.n; z.ok = KAL.ok && !isnan(z.wert) && z.linie.n >= 40;
  dauerZeigerMs = millis() - t0; return z;
}
static void zeigerZeichnen(uint8_t* f, const Geometrie& g, const Zeiger& z) {
  if (!z.linie.ok) return;
  float sx, sy; if (!schnittMitBogen(g, z.linie, sx, sy)) return;
  const Linie& L = z.linie; float t0 = -0.7f * g.r;                                  // Gerade vom Inneren bis zum Schnittpunkt
  linieZeichnen(f, (int)(L.px + t0 * L.dx), (int)(L.py + t0 * L.dy), (int)sx, (int)sy, z.ok ? F_BLAU : F_ROT, 2);
  bogenZeichnen(f, sx, sy, 6, 0, 360, F_BLAU, 2);                                     // Ablesepunkt auf dem Bogen
  if (z.ok) { char s[12]; snprintf(s, sizeof(s), "%.3f", z.wert); textZeichnen(f, 8, 8, s, F_BLAU, F_WEISS, 2); }
}
