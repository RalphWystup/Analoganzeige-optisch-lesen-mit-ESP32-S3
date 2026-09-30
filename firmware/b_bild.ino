// ---------------------------------------------------------------------------------------------------------------------
//  b_bild: Bildkanäle und dünne dunkle Strukturen in voller Auflösung
// ---------------------------------------------------------------------------------------------------------------------
static void graubild(const uint8_t* rgb565) {
  const uint16_t* p = (const uint16_t*)rgb565;
  for (int i = 0; i < W * H; i++) { uint16_t v = px565(p[i]); int r = (v >> 11) << 3, g = ((v >> 5) & 0x3F) << 2, b = (v & 0x1F) << 3; GRAU[i] = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8); }
}
static void rotbild(const uint8_t* rgb565) {                 // 255 − 255·(R−max(G,B))/R nach Weißabgleich: roter Zeiger dunkel, Druck weiß
  const uint16_t* p = (const uint16_t*)rgb565;
  for (int i = 0; i < W * H; i++) {
    uint16_t v = px565(p[i]); int r = (v >> 11) << 3, g = ((v >> 5) & 0x3F) << 2, b = (v & 0x1F) << 3;
    r = min(255, (r * wbR) >> 8); g = min(255, (g * wbG) >> 8); b = min(255, (b * wbB) >> 8);
    int rot = r - (g > b ? g : b); rot = rot <= 0 ? 0 : (rot * 255) / (r > 0 ? r : 1); if (rot > 255) rot = 255; ROT[i] = 255 - rot;
  }
}
static void verkleinern(const uint8_t* q, uint8_t* z) {                       // 2×2-Mittel → halbe Auflösung
  for (int y = 0; y < HK; y++) { const uint8_t* a = q + (2 * y) * W; const uint8_t* b = a + W; uint8_t* aus = z + y * WK; for (int x = 0; x < WK; x++) aus[x] = (a[2 * x] + a[2 * x + 1] + b[2 * x] + b[2 * x + 1] + 2) >> 2; }
}
// Trennbare Max-/Min-Filter über die Zeilen y0..y1 eines Bilds der Breite w; Spaltenpass zeilenweise (PSRAM-Sprünge sind langsam)
static void zeilenfilter(const uint8_t* q, uint8_t* z, bool maxi, int kern, int w, int y0, int y1) {
  const int h = kern / 2;
  for (int y = y0; y < y1; y++) { const uint8_t* zeile = q + y * w; uint8_t* aus = z + y * w;
    for (int x = 0; x < w; x++) { int a = x - h < 0 ? 0 : x - h, b = x + h >= w ? w - 1 : x + h; int v = zeile[a];
      for (int i = a + 1; i <= b; i++) { int u = zeile[i]; if (maxi ? u > v : u < v) v = u; } aus[x] = v; } }
}
static void spaltenfilter(const uint8_t* q, uint8_t* z, bool maxi, int kern, int w, int y0, int y1) {
  const int h = kern / 2;
  for (int y = y0; y < y1; y++) {
    int a = y - h < y0 ? y0 : y - h, b = y + h >= y1 ? y1 - 1 : y + h; uint8_t* aus = z + y * w; memcpy(aus, q + a * w, w);
    for (int i = a + 1; i <= b; i++) { const uint8_t* zeile = q + i * w; if (maxi) { for (int x = 0; x < w; x++) if (zeile[x] > aus[x]) aus[x] = zeile[x]; } else { for (int x = 0; x < w; x++) if (zeile[x] < aus[x]) aus[x] = zeile[x]; } }
  }
}
// Dünne dunkle Strukturen: Schließen (Max, dann Min) mit kern×kern, Differenz zum Bild. Volle Auflösung nur im Zeilenband
// (Teilstriche, Kern 5); Bogen und Zeiger in halber Auflösung (Kern 5 ≙ 9–10 px voll).
static void duenneStrukturen(const uint8_t* q, int kern, int y0 = 0, int y1 = H) {
  y0 = max(0, y0); y1 = min(H, y1); if (y1 - y0 < kern + 2) { y0 = 0; y1 = H; }
  zeilenfilter(q, TMP1, true, kern, W, y0, y1); spaltenfilter(TMP1, TMP2, true, kern, W, y0, y1);
  zeilenfilter(TMP2, TMP1, false, kern, W, y0, y1); spaltenfilter(TMP1, TMP2, false, kern, W, y0, y1);
  memset(DUENN, 0, W * H);
  for (int i = y0 * W; i < y1 * W; i++) { int d = (int)TMP2[i] - (int)q[i]; DUENN[i] = d < 0 ? 0 : d; }
}
static void duenneStrukturenK(const uint8_t* qK, int kern, int y0 = 0, int y1 = HK) {   // halbe Auflösung → DUENNK
  y0 = max(0, y0); y1 = min(HK, y1); if (y1 - y0 < kern + 2) { y0 = 0; y1 = HK; }
  zeilenfilter(qK, TMP1, true, kern, WK, y0, y1); spaltenfilter(TMP1, TMP2, true, kern, WK, y0, y1);
  zeilenfilter(TMP2, TMP1, false, kern, WK, y0, y1); spaltenfilter(TMP1, TMP2, false, kern, WK, y0, y1);
  memset(DUENNK, 0, WK * HK);
  for (int i = y0 * WK; i < y1 * WK; i++) { int d = (int)TMP2[i] - (int)qK[i]; DUENNK[i] = d < 0 ? 0 : d; }
}
static int perzentil(const uint8_t* q, int n, int schritt, float p) {         // Perzentil über jeden schritt-ten Wert
  uint32_t hist[256] = {0}; uint32_t z = 0; for (int i = 0; i < n; i += schritt) { hist[q[i]]++; z++; }
  uint32_t akk = 0; for (int v = 0; v < 256; v++) { akk += hist[v]; if (akk >= z * p) return v; } return 255;
}
// Zeichnen ins RGB565-Bild
static inline void pixel(uint8_t* f, int x, int y, uint16_t farbe) { if (x >= 0 && y >= 0 && x < W && y < H) ((uint16_t*)f)[y * W + x] = px565(farbe); }
static void linieZeichnen(uint8_t* f, int x0, int y0, int x1, int y1, uint16_t farbe, int dicke) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) { for (int i = -dicke / 2; i <= dicke / 2; i++) for (int j = -dicke / 2; j <= dicke / 2; j++) pixel(f, x0 + i, y0 + j, farbe);
    if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
}
static void rechteckZeichnen(uint8_t* f, int x0, int y0, int x1, int y1, uint16_t farbe) { linieZeichnen(f, x0, y0, x1, y0, farbe, 1); linieZeichnen(f, x1, y0, x1, y1, farbe, 1); linieZeichnen(f, x1, y1, x0, y1, farbe, 1); linieZeichnen(f, x0, y1, x0, y0, farbe, 1); }
static void bogenZeichnen(uint8_t* f, float cx, float cy, float r, float a0, float a1, uint16_t farbe, int dicke) {
  float schritt = 57.3f / fmaxf(r, 1.0f);
  for (float a = a0; a <= a1; a += schritt) { float t = a * M_PI / 180.0f; int x = (int)(cx + r * cosf(t)), y = (int)(cy - r * sinf(t)); for (int i = -dicke / 2; i <= dicke / 2; i++) pixel(f, x + i, y, farbe), pixel(f, x, y + i, farbe); }
}
// Kleine 5×7-Ziffern für die Wertanzeige im Bild (0-9, Punkt)
static const uint8_t ZIFFERN5x7[11][7] = {{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}};
static void textZeichnen(uint8_t* f, int x, int y, const char* s, uint16_t farbe, uint16_t grund, int skala) {
  for (const char* c = s; *c; c++) {
    int idx = (*c >= '0' && *c <= '9') ? *c - '0' : (*c == '.' || *c == ',') ? 10 : -1;
    for (int yy = -1; yy < 8 * skala + 1; yy++) for (int xx = -1; xx < 6 * skala; xx++) pixel(f, x + xx, y + yy, grund);
    if (idx >= 0) for (int r = 0; r < 7; r++) for (int cbit = 0; cbit < 5; cbit++) if (ZIFFERN5x7[idx][r] & (1 << (4 - cbit))) for (int i = 0; i < skala; i++) for (int j = 0; j < skala; j++) pixel(f, x + cbit * skala + i, y + r * skala + j, farbe);
    x += 6 * skala;
  }
}
