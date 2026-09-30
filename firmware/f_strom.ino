// ---------------------------------------------------------------------------------------------------------------------
//  f_strom: Prüfstromquelle (Sigma-Delta an GPIO 21 → Vorwiderstand → Instrument), aus Version 1 übernommen
// ---------------------------------------------------------------------------------------------------------------------
static bool quelleEinrichten(uint8_t q) {
  sigmaDeltaDetach(STROM_PIN); ledcDetach(STROM_PIN); stromQuelle = q;
  if (q == 1) return ledcAttachChannel(STROM_PIN, 1000, 12, 6);
  return sigmaDeltaAttach(STROM_PIN, 312500);
}
static void stromSetzen(float w) {
  if (w < 0) w = 0; if (w > stromImax) w = stromImax; stromSoll = w; stromAktiv = w > 0;
  if (pinTest >= 0) return;
  if (stromQuelle == 1) ledcWrite(STROM_PIN, (uint32_t)roundf(w / stromImax * 4095.0f)); else sigmaDeltaWrite(STROM_PIN, (uint32_t)roundf(w / stromImax * 255.0f));
}
// JPEG des aktuellen Bilds (klein 320×240 oder voll) in den Austauschpuffer
static void nurJpeg(camera_fb_t* fb) {
  uint32_t tj = millis(); uint8_t* out = nullptr; size_t len = 0; bool ok;
  if (streamKlein) {
    const uint16_t* q = (const uint16_t*)fb->buf; uint16_t* z = (uint16_t*)klein565; const int WK = W / 2, HK = H / 2;
    for (int y = 0; y < HK; y++) for (int x = 0; x < WK; x++) z[y * WK + x] = q[(2 * y) * W + 2 * x];
    camera_fb_t kf = *fb; kf.buf = klein565; kf.len = WK * HK * 2; kf.width = WK; kf.height = HK; ok = frame2jpg(&kf, jpegQualitaet, &out, &len);
  } else ok = frame2jpg(fb, jpegQualitaet, &out, &len);
  dauerJpegMs = millis() - tj;
  if (ok && out) { if (xSemaphoreTake(jpegMutex, portMAX_DELAY) == pdTRUE) { if (jpegPuffer) free(jpegPuffer); jpegPuffer = out; jpegLaenge = len; xSemaphoreGive(jpegMutex); } else free(out); }
}
static uint8_t* jpegKopie(size_t& len) {
  uint8_t* k = nullptr; len = 0;
  if (xSemaphoreTake(jpegMutex, pdMS_TO_TICKS(500)) == pdTRUE) { if (jpegPuffer && jpegLaenge) { k = (uint8_t*)malloc(jpegLaenge); if (k) { memcpy(k, jpegPuffer, jpegLaenge); len = jpegLaenge; } } xSemaphoreGive(jpegMutex); }
  return k;
}
