// ---------------------------------------------------------------------------------------------------------------------
//  a_licht: Kamera, feste Belichtung als Photometer, Regelung auf das Weiß des Zifferblatts, RGB-LED
//  Belichtungsversuch 16.09. (Deckenlicht + Tischlampe, ohne LED): Verstärkung 8 mit Belichtung 700–1200 liefert Weiß
//  (95-%-Wert im Skalenbereich) 218–229 ohne Sättigung und den besten Druckkontrast; Verstärkung 16/24 sättigt.
//  Regelgröße ist deshalb das Weiß des Zifferblatts (Ziel 220), nicht der Mittelwert der Bildmitte; Sättigungswächter < 1 %.
// ---------------------------------------------------------------------------------------------------------------------
static void regelKamera() {
  sensor_t* s = esp_camera_sensor_get(); if (!s) return;
  s->set_whitebal(s, 0); s->set_awb_gain(s, 0); s->set_exposure_ctrl(s, 0); s->set_aec_value(s, regelAec); s->set_gain_ctrl(s, 0); s->set_agc_gain(s, regelGain);
}
static void kameraStart() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM; config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; config.pixel_format = PIXFORMAT_RGB565; config.frame_size = FRAMESIZE_VGA;
  config.fb_count = 2; config.fb_location = CAMERA_FB_IN_PSRAM; config.grab_mode = CAMERA_GRAB_LATEST;
  if (esp_camera_init(&config) != ESP_OK) { KONSOLE.println("[FEHLER] Kamera-Init fehlgeschlagen, Neustart in 5 s"); delay(5000); ESP.restart(); }
  sensor_t* s = esp_camera_sensor_get();
  KONSOLE.printf("[OK] Kamera-Sensor PID 0x%02X (%s), RGB565 %dx%d\n", s->id.PID, s->id.PID == OV3660_PID ? "OV3660" : s->id.PID == OV2640_PID ? "OV2640" : "?", W, H);
  if (s->id.PID == OV3660_PID) { s->set_brightness(s, 1); s->set_saturation(s, 0); }
  s->set_hmirror(s, prefs.getUChar("hmirror", 0)); s->set_vflip(s, prefs.getUChar("vflip", 0));
  regelAec = constrain(prefs.getInt("regelaec", 800), 20, 1200); regelGain = constrain(prefs.getInt("regelgain", 8), 0, 30);
  regelZiel = prefs.getFloat("regelziel", 220.0f); regelLedMax = prefs.getUChar("regelledmax", 0); regelAn = prefs.getUChar("regelan", 1) != 0;
  byteTausch = prefs.getUChar("bytetausch", 1) != 0; streamKlein = prefs.getUChar("streamklein", 1) != 0;
  delay(300); regelKamera();
}
static void ledSetzen(uint8_t r, uint8_t g, uint8_t b) { ledR = r; ledG = g; ledB = b; rgbLedWrite(LED_PIN, r, g, b); }

// Photometrie im Skalenbereich: Weiß = 95-%-Wert, Schwarz = 5-%-Wert der Grauwerte im Ring r−15 … r+60 des Bogens (Sektor),
// Sättigung = Anteil ≥ 250. Ohne gültige Skala: Bildmitte (mittleres Drittel).
static void photometrie(const Geometrie& g) {
  uint32_t hist[256] = {0}; uint32_t n = 0;
  if (g.ok) {
    int x0 = max(0, (int)(g.cx - g.r - 60)), x1 = min(W - 1, (int)(g.cx + g.r + 60)), y0 = max(0, (int)(g.cy - g.r - 60)), y1 = min(H - 1, (int)g.cy);
    for (int y = y0; y <= y1; y += 2) for (int x = x0; x <= x1; x += 2) {
      float ex = x - g.cx, ey = y - g.cy, rr = sqrtf(ex * ex + ey * ey); if (rr < g.r - 15 || rr > g.r + 60) continue;
      float w = winkelGrad(ex, ey); if (w < g.a0 - 2 || w > g.a1 + 2) continue;
      hist[GRAU[y * W + x]]++; n++;
    }
  } else { for (int y = H / 3; y < 2 * H / 3; y += 2) for (int x = W / 3; x < 2 * W / 3; x += 2) { hist[GRAU[y * W + x]]++; n++; } }
  if (n < 100) return;
  uint32_t akk = 0; int p5 = 0, p95 = 255; for (int v = 0; v < 256; v++) { akk += hist[v]; if (akk >= n * 5 / 100) { p5 = v; break; } }
  akk = 0; for (int v = 0; v < 256; v++) { akk += hist[v]; if (akk >= n * 95 / 100) { p95 = v; break; } }
  uint32_t sat = 0; for (int v = 250; v < 256; v++) sat += hist[v];
  weiss95 = p95; schwarz5 = p5; saettigung = (float)sat / n;
}
// Regelung alle 6 Bilder: Weiß auf Ziel (Totband ±8), Sättigung < 1 %. Stellglieder: LED (falls freigegeben, sonst 0),
// dann Belichtungszeit (20…1200), dann Verstärkung (2…16). Werte werden im Flash gemerkt (höchstens alle 60 s).
static void regelung() {
  // Alle 3 Bilder. Sättigung hat Vorrang und wird schnell abgebaut (Verstärkung zuerst zurück, dann Belichtung × 0,7);
  // sonst proportional: die Belichtung skaliert linear mit der Helligkeit, also Faktor Ziel/Ist (0,6 … 1,5) je Schritt.
  // Im Totband wandert die Verstärkung zur Nennstufe 8 zurück (Rauschen), die Belichtung gleicht aus. (16.09.: die alte
  // Schrittregelung brauchte nach dem Einschalten der Tischlampe 90 s bei 79 % Sättigung.)
  if (!regelAn || (bildzaehler % 3)) return;
  bool aenderung = false;
  if (saettigung > 0.01f) {
    if (regelGain > 8) { regelGain = max(8, regelGain - 2); aenderung = true; }
    else if (regelLedMax > 0 && regelLed > 0) { regelLed = max(0, regelLed - 40); ledSetzen(regelLed, regelLed, regelLed); aenderung = true; }
    else if (regelAec > 20) { regelAec = max(20, (int)(regelAec * 0.7f)); aenderung = true; }
    else if (regelGain > 2) { regelGain--; aenderung = true; }
  } else {
    float e = regelZiel - weiss95;
    if (fabsf(e) > 8.0f) {
      float f = constrain(regelZiel / fmaxf(weiss95, 30.0f), 0.6f, 1.5f);
      int neuLed = regelLed + (int)constrain(e / 6.0f, -8.0f, 8.0f);
      if (regelLedMax > 0 && neuLed >= 0 && neuLed <= regelLedMax && neuLed != regelLed) { regelLed = neuLed; ledSetzen(regelLed, regelLed, regelLed); aenderung = true; }
      else if (e > 0) { if (regelAec < 1200) { regelAec = min(1200, max(regelAec + 10, (int)(regelAec * f))); aenderung = true; } else if (regelGain < 16) { regelGain++; aenderung = true; } }
      else { if (regelGain > 8 && regelAec <= 600) { regelGain--; aenderung = true; } else if (regelAec > 20) { regelAec = max(20, min(regelAec - 10, (int)(regelAec * f))); aenderung = true; } else if (regelGain > 2) { regelGain--; aenderung = true; } }
    } else if (regelGain > 8 && regelAec <= 700) { regelGain--; regelAec = min(1200, (int)(regelAec * 1.12f)); aenderung = true; }
  }
  if (aenderung) regelKamera();
  static uint32_t letzteSicherung = 0; static int gAec = -1, gGain = -1;
  if ((regelAec != gAec || regelGain != gGain) && millis() - letzteSicherung > 60000UL) { prefs.putInt("regelaec", regelAec); prefs.putInt("regelgain", regelGain); gAec = regelAec; gGain = regelGain; letzteSicherung = millis(); }
}
// Rechnerischer Weißabgleich für das Rotbild: Zifferblatt im Ring innerhalb des Bogens (0,55…0,95 r) neutral setzen
static void weissabgleich(const uint8_t* rgb565, const Geometrie& g) {
  const uint16_t* p = (const uint16_t*)rgb565; uint32_t sr = 0, sg = 0, sb = 0, n = 0;
  if (g.ok) {
    int x0 = max(0, (int)(g.cx - g.r)), x1 = min(W - 1, (int)(g.cx + g.r)), y0 = max(0, (int)(g.cy - g.r)), y1 = min(H - 1, (int)g.cy);
    for (int y = y0; y <= y1; y += 4) for (int x = x0; x <= x1; x += 4) {
      float ex = x - g.cx, ey = y - g.cy, rr = sqrtf(ex * ex + ey * ey); if (rr < 0.55f * g.r || rr > 0.95f * g.r) continue;
      float w = winkelGrad(ex, ey); if (w < g.a0 || w > g.a1) continue;
      uint16_t v = px565(p[y * W + x]); sr += (v >> 11) << 3; sg += ((v >> 5) & 0x3F) << 2; sb += (v & 0x1F) << 3; n++;
    }
  } else for (int y = H / 4; y < 3 * H / 4; y += 4) for (int x = W / 4; x < 3 * W / 4; x += 4) { uint16_t v = px565(p[y * W + x]); sr += (v >> 11) << 3; sg += ((v >> 5) & 0x3F) << 2; sb += (v & 0x1F) << 3; n++; }
  if (n < 50) return;
  float mr = (float)sr / n, mg = (float)sg / n, mb = (float)sb / n, m = (mr + mg + mb) / 3.0f;
  wbR = constrain((int)(256.0f * m / fmaxf(mr, 1.0f)), 128, 512); wbG = constrain((int)(256.0f * m / fmaxf(mg, 1.0f)), 128, 512); wbB = constrain((int)(256.0f * m / fmaxf(mb, 1.0f)), 128, 512);
}
