// =====================================================================================================================
//  Analoganzeige lesen – Version 2 (16.09.2026)
//  Reihenfolge nach Nutzervorgabe: (1) Belichtung so, dass Striche und Ziffern lesbar sind, (2) Skala eindeutig erkennen
//  (unterer schwarzer Bogen, Teilstriche, Hauptstriche, Ziffern) und markieren, (3) erst dann der Zeiger.
//  Gegenüber Version 1: kein gespeichertes Ringmuster, kein Hintergrundmodell, keine Lageverfolgung – die Geometrie kommt
//  in jedem Bild aus der Skala selbst. Gültigkeit zweistufig: Skala gültig UND Zeiger gültig. Die Kalibrierung mit dem
//  Prüfstrom liefert nur noch den Parallaxenversatz zwischen Bogenmittelpunkt und Zeigerdrehpunkt (relativ zum Radius).
//  Module (Arduino-Reiter, alphabetisch angehängt): a_licht, b_bild, c_skala, d_zeiger, e_kalibrierung, f_strom, g_netz,
//  h_modbus, i_schutz. Alle Typen und gemeinsamen Variablen stehen hier (Arduino-Prototypen brauchen die Typen zuerst).
//  Schnittstellen (Webseite, /status, /control, /capture, Stream :81, Modbus :502, OTA) sind mit Version 1 verträglich.
// =====================================================================================================================
#include "esp_camera.h"
#include "img_converters.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <math.h>
#include <algorithm>
#include "esp_http_server.h"
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <lwip/sockets.h>

#ifndef BOARD_HAS_PSRAM
  #error "IDE: Werkzeuge -> PSRAM -> 'OPI PSRAM' waehlen (Bildpuffer liegen im PSRAM)."
#endif
#ifndef CONFIG_IDF_TARGET_ESP32S3
  #error "IDE: Werkzeuge -> Board -> esp32 -> 'ESP32S3 Dev Module' waehlen."
#endif

// ---------- Konsole auf beiden USB-C-Buchsen (UART und USB) ----------
#include "HWCDC.h"
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  #define KONSOLE_USB  Serial
  #define KONSOLE_UART Serial0
#else
  static HWCDC usbKonsole;
  #define KONSOLE_USB  usbKonsole
  #define KONSOLE_UART Serial
#endif
class Doppelkonsole : public Print {
 public:
  void begin(unsigned long baud) { KONSOLE_USB.begin(baud); KONSOLE_UART.begin(baud); }
  size_t write(uint8_t c) override { KONSOLE_UART.write(c); if (KONSOLE_USB) KONSOLE_USB.write(c); return 1; }
  size_t write(const uint8_t* b, size_t n) override { KONSOLE_UART.write(b, n); if (KONSOLE_USB) KONSOLE_USB.write(b, n); return n; }
  bool usbVerbunden() { return (bool)KONSOLE_USB; }
};
static Doppelkonsole KONSOLE;

const char* WIFI_SSID     = "MeinNetz";       // eigenen Netznamen eintragen
const char* WIFI_PASSWORT = "xxxxxxxx";   // hier das eigene Kennwort eintragen (im Quelltext steht keines)
const char* OTA_HOSTNAME  = "analogcam";
static const char* VERSION_TEXT = "V2 2026-09-16";

// ---------- Kamerapins Freenove ESP32-S3 WROOM ----------
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// ---------- Bildgröße und Puffer (PSRAM) ----------
static const int W = 640, H = 480;
static uint8_t* GRAU = nullptr;      // Graubild
static uint8_t* ROT  = nullptr;      // Rotbild mit Weißabgleich (Zeiger dunkel, Druck weiß)
static uint8_t* DUENN = nullptr;     // dünne dunkle Strukturen (Schließen − Bild) des jeweils bearbeiteten Kanals
static uint8_t* TMP1 = nullptr;        // Zwischenpuffer der trennbaren Filter
static uint8_t* TMP2 = nullptr;
static const int WK = W / 2, HK = H / 2;   // halbe Auflösung für Bogen- und Zeigersuche (PSRAM-Bandbreite; V1-Erfahrung)
static uint8_t* GRAUK = nullptr; static uint8_t* ROTK = nullptr; static uint8_t* DUENNK = nullptr;
static int16_t* LAB = nullptr;       // Markierung zusammenhängender Bereiche (Skalenbogen-Suche)
static uint8_t* klein565 = nullptr;  // 320×240 RGB565 für das kleine JPEG
static uint16_t* STACK = nullptr;    // Stapel der Flutfüllung (x,y-Paare)

// ---------- Typen ----------
struct Geometrie {                   // Ergebnis der Skalenerkennung eines Bildes
  bool ok;                           // Skala eindeutig: Bogen + Strichzahl + Hauptstriche plausibel
  float cx, cy, r;                   // Bogenmittelpunkt und -radius (Bildkoordinaten)
  float a0, a1;                      // Winkelbereich des Bogens (Grad, mathematisch, 0 = rechts, gegen den Uhrzeigersinn)
  int inlier;                        // Bogenpunkte auf dem Kreis
  int nStriche, nHaupt;              // erkannte Teilstriche / Hauptstriche
  float pitch, regel;                // Strichabstand (Grad) und relative Streuung
  float haupt[12];                   // Hauptstrichwinkel, fallend (vom Skalenanfang links zum Ende rechts)
  float striche[64];                 // alle Strichwinkel, fallend
  int nZiffern;                      // erkannte Ziffernkästen (je Hauptstrich höchstens einer)
  int16_t ziffer[12][4];             // x0,y0,x1,y1 je Hauptstrich (−1 = keiner)
  float guete;                       // 0…1: Anteil erfüllter Erwartungen
  uint32_t ms;
};
struct Linie { float px, py, dx, dy; int n; bool ok; };
struct Zeiger {                      // Ergebnis der Zeigermessung
  bool ok; float winkel;             // Winkel vom Drehpunkt aus (Grad)
  float wert; float sicherheit;      // Wert in Einheit; Sicherheit (Punktezahl der Geraden / Übereinstimmung)
  Linie linie;
};
struct Kal2 {                        // Kalibrierung V2: nur der Parallaxenversatz und der Messbereich
  uint32_t magic;                    // 0x4B414C32 "KAL2"
  float vx, vy;                      // Versatz Drehpunkt − Bogenmittelpunkt, relativ zum Bogenradius
  float bereich;                     // Wert am letzten Hauptstrich (z. B. 1,0)
  int nHaupt;                        // erwartete Hauptstriche
  int nStriche;                      // erwartete Teilstriche (0 = unbekannt, dann nur Plausibilität)
  char einheit[8];
  bool ok;
};
struct Kalpunkt { float wert; Linie l; bool ok; Geometrie g; };   // festgehaltener Ausschlag für die Zweipunkt-Kalibrierung

// ---------- gemeinsame Zustände ----------
static Preferences prefs;
static Geometrie SKALA = {};         // letzte Skalenerkennung
static Geometrie SKALA_GUT = {};     // letzte gültige Skala (für Übergangsbilder)
static Zeiger ZEIGER = {};
static Kal2 KAL = {};
static Kalpunkt kalpunkte[2] = {};
static volatile bool kalAnfrage = false; static volatile float kalWert = 0; static volatile bool kalFertig = false; static char kalAntwort[260];
static bool byteTausch = true;
static uint32_t bildzaehler = 0, dauerMs = 0, dauerSkalaMs = 0, dauerZeigerMs = 0, dauerJpegMs = 0;
static uint32_t tDuenn = 0, tPruef = 0, tStriche = 0, tZiffern = 0, tGrau = 0, tRot = 0, tZDuenn = 0, tZKand = 0, tZRansac = 0;   // Zeitmessung je Stufe (ms)
static bool overlay = true; static bool streamKlein = true; static uint8_t jpegQualitaet = 60; static uint8_t ansicht = 0;
static float wertRing[5]; static int wertRingN = 0, wertRingI = 0; static float wertMedian = NAN;
static uint32_t ueberbelichtet = 0;
static uint32_t skalaOkZaehler = 0, skalaFehlZaehler = 0;

// Licht / Kamera
static const int LED_PIN = 48; static uint8_t ledR = 0, ledG = 0, ledB = 0;
static bool regelAn = true; static float regelZiel = 220.0f; static uint8_t regelLedMax = 0;   // Regelgröße: Weiß (p95) des Zifferblatts; LED standardmäßig aus (Nutzer: LEDs abgedeckt)
static int regelAec = 800, regelGain = 8; static float weiss95 = 0, schwarz5 = 0, saettigung = 0; static int regelLed = 0;
static int wbR = 256, wbG = 256, wbB = 256;

// Prüfstromquelle
static const int STROM_PIN = 21; static const int STROM_BITS = 8;
static float stromImax = 0.887f; static float stromSoll = 0; static bool stromAktiv = false; static bool pwmOk = false; static int pinTest = -1; static uint8_t stromQuelle = 0;

// Selbstschutz
static bool sicherheitsmodus = false; static uint8_t abstuerze = 0; static bool appBestaetigt = false; static const uint32_t BESTAETIGUNG_MS = 90000;

// Modbus
static bool modbusSchreiben = true;
static uint32_t modbusAnfragen = 0, modbusFehler = 0, modbusVoll = 0, modbusVerworfen = 0, modbusLetzteMs = 0; static char modbusLetzter[32] = "-", modbusLetzterFehler[64] = "-";

// JPEG-Austausch zwischen Bildtakt und Netz
static uint8_t* jpegPuffer = nullptr; static size_t jpegLaenge = 0; static SemaphoreHandle_t jpegMutex = nullptr;

// ---------- Hilfsfunktionen (klein, vor allen Modulen nutzbar) ----------
static inline uint16_t px565(uint16_t v) { return byteTausch ? (uint16_t)((v >> 8) | (v << 8)) : v; }
static float winkelGrad(float dx, float dy) { float w = atan2f(-dy, dx) * 180.0f / M_PI; if (w < 0) w += 360; return w; }
static uint32_t zufallsZustand = 12345;
static inline uint32_t rnd() { zufallsZustand ^= zufallsZustand << 13; zufallsZustand ^= zufallsZustand >> 17; zufallsZustand ^= zufallsZustand << 5; return zufallsZustand; }

void setup() {
  KONSOLE.begin(115200);
  for (uint32_t t0 = millis(); !KONSOLE.usbVerbunden() && millis() - t0 < 3000; ) delay(50);
  delay(300);
  KONSOLE.printf("\n[START] Analoganzeige lesen · %s\n", VERSION_TEXT);
  KONSOLE.printf("[INFO] Chip %s, Flash %u kB, PSRAM %u kB, Core %s\n", ESP.getChipModel(), (unsigned)(ESP.getFlashChipSize() / 1024), (unsigned)(ESP.getPsramSize() / 1024), ESP_ARDUINO_VERSION_STR);
  if (!psramFound()) { KONSOLE.println("[FEHLER] Kein PSRAM erkannt (IDE: 'PSRAM: OPI PSRAM'). Neustart in 10 s"); delay(10000); ESP.restart(); }
  schutzStart();                                  // Absturzzähler, Sicherheitsmodus, Watchdog
  if (sicherheitsmodus) { netzStart(true); return; }
  GRAU = (uint8_t*)ps_malloc(W * H); ROT = (uint8_t*)ps_malloc(W * H); DUENN = (uint8_t*)ps_malloc(W * H); TMP1 = (uint8_t*)ps_malloc(W * H); TMP2 = (uint8_t*)ps_malloc(W * H);
  GRAUK = (uint8_t*)ps_malloc(WK * HK); ROTK = (uint8_t*)ps_malloc(WK * HK); DUENNK = (uint8_t*)ps_malloc(WK * HK);
  LAB = (int16_t*)ps_malloc(W * H * 2); klein565 = (uint8_t*)ps_malloc((W / 2) * (H / 2) * 2); STACK = (uint16_t*)ps_malloc(W * H * 2 * 2);
  jpegMutex = xSemaphoreCreateMutex();
  if (!GRAU || !ROT || !DUENN || !TMP1 || !TMP2 || !LAB || !klein565 || !STACK || !GRAUK || !ROTK || !DUENNK) { KONSOLE.println("[FEHLER] PSRAM-Zuteilung fehlgeschlagen"); delay(10000); ESP.restart(); }
  kalLaden();                                     // Kalibrierung V2 aus dem Flash
  kameraStart();                                  // Kamera + feste Belichtung
  pwmOk = quelleEinrichten(prefs.getUChar("quelle", 0)); if (!pwmOk) KONSOLE.println("[FEHLER] Prüfstromquelle nicht einrichtbar");
  stromSetzen(0); ledSetzen(0, 0, 0);
  netzStart(false);
  KONSOLE.printf("[BEREIT] Seite http://%s/  ·  Stream http://%s:81/stream  ·  Standbild http://%s/capture\n", WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
}

void loop() {
  ArduinoOTA.handle(); esp_task_wdt_reset();
  if (!appBestaetigt && millis() > BESTAETIGUNG_MS) { appBestaetigt = true; esp_ota_mark_app_valid_cancel_rollback(); prefs.putUChar("abstuerze", 0); KONSOLE.println("[SCHUTZ] Firmware nach 90 s bestaetigt (kein Rueckfall)"); }
  if (sicherheitsmodus) { delay(20); return; }
  netzPflege();                                   // WLAN-Wiederverbindung, serielle Statuszeile, Netzdiagnose
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) { verarbeite(fb); esp_camera_fb_return(fb); } else delay(10);
}
