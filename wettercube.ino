#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <time.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include "ui.h"
#include "images.h"
#include "colors.h"

#define FIRMWARE_VERSION     "1.8.2"
#define OTA_VERSION_URL      "https://jppeterson-lab.github.io/WetterCube/version.json"
#define OTA_FIRMWARE_URL     "https://jppeterson-lab.github.io/WetterCube/firmware/firmware.bin"

// --- SPEICHER & WEB-SERVER ---
Preferences preferences;
WebServer server(80);

String location  = "";
float  latitude  = 0.0;
float  longitude = 0.0;

unsigned long lastWeatherUpdate = 0;
const unsigned long weatherInterval = 900000; // 15 Minuten

// --- HARDWARE BUTTON (TTP223) ---
#define TOUCH_PIN 3
int currentScreen = 1;
unsigned long lastTouchTime = 0;

// --- REGEN-WARNUNG ---
bool regenWarnungAktiv       = false;
bool regenWarnungBestaetigt  = false;
unsigned long lastBlinkTime  = 0;
bool blinkState              = false;

// --- POLLEN-WARNUNG ---
bool pollenWarnungAktiv      = false;
bool pollenWarnungBestaetigt = false;
unsigned long lastPollenBlink = 0;
bool pollenBlinkState        = false;

// --- DISPLAY DIMMEN ---
#define BL_FREQ        5000
#define BL_RESOLUTION  8
#define BL_DIM         51         // 20% – fester Dimm-Wert
bool isDimmed = false;
unsigned long lastActivityTime = 0;
unsigned long dimTimeoutMs = 180000UL;

// --- WEBCONFIG-EINSTELLUNGEN ---
bool regenWarnungEnabled  = true;
bool pollenWarnungEnabled = true;
bool screen2Enabled       = true;
bool screen3Enabled       = true;
bool screen4Enabled       = true;
bool screen5Enabled       = true;
bool screenLuftEnabled    = true;
int  pollenSchwellwert    = 30;
int  dimTimeoutMin        = 3;
int  brightnessPercent    = 80;   // 10–100%, konfigurierbar per Webinterface
bool darkThemeEnabled     = true; // Design: Dunkel/Hell, konfigurierbar per Webinterface

// --- NACHTMODUS ---
bool nachtModusEnabled  = false;
int  nachtVon           = 22 * 60; // Minuten seit Mitternacht (22:00)
int  nachtBis           =  6 * 60; // Minuten seit Mitternacht (06:00)
int  nachtHelligkeit    = 0;       // 0 = Display aus
bool nachtOverride          = false;
unsigned long nachtOverrideUntil = 0; // Zeitstempel bis wann Override aktiv ist

int getBrightPWM() { return (brightnessPercent * 255) / 100; }

// --- DISPLAY HARDWARE PINS ---
#define TFT_MOSI 20
#define TFT_SCLK 21
#define TFT_CS   6
#define TFT_DC   7
#define TFT_RST  10
#define TFT_BL   5

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 0, true, 240, 240, 0, 0);

static const uint16_t screenWidth  = 240;
static const uint16_t screenHeight = 240;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * 10];

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}

// Funktions-Deklarationen
void checkTouchButton();
void updateClock();
void fetchWeather();
String getWindDirection(int deg);
bool geocodeLocation(const String& city);
void setWeatherIcon(lv_obj_t* img, int wmoCode);
void setTempColor(lv_obj_t* label, float temp);
void fetchPollen();
void setPollenLabel(lv_obj_t* label, float value);
void updateWeatherIcon(int wmoCode);
void showBootScreen();
void runCaptivePortal();
void handlePortalRoot();
void handlePortalSave();
void loadConfig();
void startConfigServer();
void handleConfig();
void handleConfigSave();
void handleOTAPage();
void handleOTACheck();
void handleOTAInstall();
void handleWifiScan();
void handleWifiChange();
void handleLocationChange();

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("--- CUBE START ---");

    pinMode(TOUCH_PIN, INPUT);
    ledcAttach(TFT_BL, BL_FREQ, BL_RESOLUTION);
    ledcWrite(TFT_BL, getBrightPWM());
    lastActivityTime = millis();

    gfx->begin();
    gfx->fillScreen(0x0000);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 10);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    lv_theme_t *theme = lv_theme_default_init(lv_disp_get_default(), lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_disp_set_theme(lv_disp_get_default(), theme);
    create_screens();

    // Daten aus dem Flash-Speicher laden
    preferences.begin("wettercube", false);
    location  = preferences.getString("location", "");
    latitude  = preferences.getFloat("lat", 0.0);
    longitude = preferences.getFloat("lon", 0.0);
    loadConfig();
    change_color_theme(darkThemeEnabled ? THEME_DARK : THEME_LIGHT);

    showBootScreen(); // verbindet WLAN via WiFiManager, startet mDNS + Config-Server

    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "europe.pool.ntp.org");
    if (latitude == 0.0 && longitude == 0.0) geocodeLocation(location);
    fetchWeather();
    fetchPollen();
}

void loop() {
    lv_timer_handler();
    server.handleClient(); // Config-Webserver
    delay(5);

    checkTouchButton();

    // --- Nachtmodus (zeitbasiert) ---
    if (nachtModusEnabled) {
        struct tm ti = {};
        if (!getLocalTime(&ti, 0)) ti.tm_hour = ti.tm_min = 0;
        int nowMin = ti.tm_hour * 60 + ti.tm_min;
        bool inNight;
        if (nachtVon < nachtBis) {
            inNight = (nowMin >= nachtVon && nowMin < nachtBis);
        } else {
            inNight = (nowMin >= nachtVon || nowMin < nachtBis); // über Mitternacht
        }
        // Override läuft nach 15 Sekunden ab
        if (nachtOverride && millis() >= nachtOverrideUntil) {
            nachtOverride = false;
        }
        if (inNight && !nachtOverride) {
            if (!isDimmed) {
                ledcWrite(TFT_BL, (nachtHelligkeit * 255) / 100);
                isDimmed = true;
            }
        } else if (!inNight) {
            nachtOverride = false;
            if (isDimmed) {
                ledcWrite(TFT_BL, getBrightPWM());
                isDimmed = false;
            }
        }
    }

    // --- Display dimmen nach Inaktivität ---
    if (!nachtModusEnabled && !isDimmed && dimTimeoutMs > 0 && millis() - lastActivityTime >= dimTimeoutMs) {
        ledcWrite(TFT_BL, BL_DIM);
        isDimmed = true;
    }

    // --- Pollen-Warnung blinken ---
    if (pollenWarnungAktiv && millis() - lastPollenBlink >= 500) {
        lastPollenBlink = millis();
        pollenBlinkState = !pollenBlinkState;
        lv_obj_set_style_bg_color(objects.uiscreenwarnungpollen,
            pollenBlinkState ? lv_color_hex(0xFF8800) : lv_color_hex(0xCC5500),
            LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // --- Regen-Warnung blinken ---
    if (regenWarnungAktiv && millis() - lastBlinkTime >= 500) {
        lastBlinkTime = millis();
        blinkState = !blinkState;
        lv_obj_set_style_bg_color(objects.uiscreenwarnung,
            blinkState ? lv_color_hex(0xCC0000) : lv_color_hex(0x660000),
            LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate >= 1000) {
        lastTimeUpdate = millis();
        updateClock();
    }

    if (millis() - lastWeatherUpdate >= weatherInterval) {
        lastWeatherUpdate = millis();
        fetchWeather();
        fetchPollen();
    }
}

// --- BUTTON ---

void checkTouchButton() {
    if (digitalRead(TOUCH_PIN) == HIGH) {
        lastActivityTime = millis();
        if (nachtOverride) nachtOverrideUntil = millis() + 15000UL; // Timer bei jedem Touch verlängern
        if (isDimmed) {
            ledcWrite(TFT_BL, getBrightPWM());
            isDimmed = false;
            if (nachtModusEnabled) {
                nachtOverride = true;
                nachtOverrideUntil = millis() + 15000UL; // 15 Sekunden
            }
            return;
        }
        // Pollen-Warnung bestätigen
        if (pollenWarnungAktiv) {
            if (millis() - lastTouchTime > 500) {
                lastTouchTime = millis();
                pollenWarnungAktiv = false;
                pollenWarnungBestaetigt = true;
                lv_scr_load_anim(objects.screen1, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
                currentScreen = 1;
            }
            return;
        }
        // Regen-Warnung bestätigen
        if (regenWarnungAktiv) {
            if (millis() - lastTouchTime > 500) {
                lastTouchTime = millis();
                regenWarnungAktiv = false;
                regenWarnungBestaetigt = true;
                lv_scr_load_anim(objects.screen1, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
                currentScreen = 1;
            }
            return;
        }
        if (millis() - lastTouchTime > 500) {
            lastTouchTime = millis();
            // Screenliste: 1 (immer aktiv), 4, 2, 3, 5, 6
            int  order[]   = {1, 4, 2, 3, 5, 6};
            bool enabled[] = {true, screen4Enabled, screen2Enabled, screen3Enabled, screen5Enabled, screenLuftEnabled};
            // Aktuellen Index finden
            int curIdx = 0;
            for (int i = 0; i < 6; i++) { if (order[i] == currentScreen) { curIdx = i; break; } }
            // Nächsten aktiven Screen suchen
            int nextScreen = 1;
            for (int i = 1; i <= 6; i++) {
                int ni = (curIdx + i) % 6;
                if (enabled[ni]) { nextScreen = order[ni]; break; }
            }
            // Screen laden
            lv_obj_t* screens[] = {nullptr, objects.screen1, objects.screen2, objects.screen3, objects.screen4, objects.screenpollen, objects.screenluft};
            lv_scr_load_anim_t animDir = (nextScreen == 1 && curIdx >= 1) ? LV_SCR_LOAD_ANIM_MOVE_RIGHT : LV_SCR_LOAD_ANIM_MOVE_LEFT;
            if (nextScreen >= 1 && nextScreen <= 6) {
                lv_scr_load_anim(screens[nextScreen], animDir, 300, 0, false);
                currentScreen = nextScreen;
            }
        }
    }
}

// Setup-Portal entfällt – WLAN-Einrichtung erfolgt über WiFiManager (tzapu)
// Reset: wm.resetSettings() z.B. bei langem Tastendruck aufrufen

// --- BOOT SCREEN / CAPTIVE PORTAL ---

static DNSServer dnsServer;
static bool portalActive = false;
static WebServer portalServer(80);

void handlePortalRoot() {
    String savedSsid = preferences.getString("ssid", "");
    String savedLoc  = preferences.getString("location", "");

    int n = WiFi.scanNetworks();
    String opts = "";
    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        opts += "<option value='" + s + "'" + (s == savedSsid ? " selected" : "") + ">" + s + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>WetterCube Setup</title>"
        "<style>body{font-family:Arial,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:20px;}"
        "h1{color:#58a6ff;} p{color:#8b949e;font-size:.9em;}"
        "input,select{background:#161b22;color:#e6edf3;border:1px solid #30363d;border-radius:6px;"
        "padding:9px;width:100%;box-sizing:border-box;margin-top:6px;font-size:1em;}"
        "button{background:#238636;color:#fff;border:none;padding:12px;border-radius:6px;"
        "cursor:pointer;width:100%;font-size:1em;margin-top:14px;}"
        "label{display:block;margin-top:12px;font-size:.9em;color:#8b949e;}</style></head><body>"
        "<h1>&#9729; WetterCube Setup</h1>"
        "<p>WLAN-Zugangsdaten und Standort eingeben.</p>"
        "<form method='POST' action='/save'>"
        "<label>WLAN-Netzwerk</label><select name='ssid'>" + opts + "</select>"
        "<label>Passwort</label><input type='password' name='pass'>"
        "<label>Standort (Stadtname, z.B. Berlin)</label>"
        "<input type='text' name='loc' value='" + savedLoc + "' placeholder='Berlin'>"
        "<p style='font-size:.8em;'>Bei Umlauten: Munchen statt M&uuml;nchen</p>"
        "<button type='submit'>Speichern &amp; Verbinden</button>"
        "</form></body></html>";
    portalServer.send(200, "text/html", html);
}

void handlePortalSave() {
    String ssid = portalServer.arg("ssid");
    String pass = portalServer.arg("pass");
    String loc  = portalServer.arg("loc");
    loc.trim();

    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    if (loc.length() >= 2) {
        bool changed = (loc != preferences.getString("location", ""));
        preferences.putString("location", loc);
        location = loc;
        if (changed) {
            latitude = 0.0; longitude = 0.0;
            preferences.putFloat("lat", 0.0);
            preferences.putFloat("lon", 0.0);
        }
    }

    portalServer.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<style>body{font-family:Arial;background:#0d1117;color:#e6edf3;text-align:center;padding:60px;}"
        "h2{color:#3fb950;}</style></head><body>"
        "<h2>&#10003; Gespeichert!</h2><p>WetterCube verbindet sich neu...</p></body></html>");

    delay(1500);
    ESP.restart();
}

void runCaptivePortal() {
    lv_label_set_text(objects.labelstatus, "Portal: WetterCube-Setup");
    lv_bar_set_value(objects.barwifi, 30, LV_ANIM_ON);
    lv_timer_handler();

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("WetterCube-Setup");
    dnsServer.start(53, "*", WiFi.softAPIP());

    portalServer.on("/",     HTTP_GET,  handlePortalRoot);
    portalServer.on("/save", HTTP_POST, handlePortalSave);
    portalServer.onNotFound([]() { portalServer.sendHeader("Location", "http://192.168.4.1/"); portalServer.send(302); });
    portalServer.begin();

    unsigned long start = millis();
    while (millis() - start < 180000UL) {
        dnsServer.processNextRequest();
        portalServer.handleClient();
        lv_timer_handler();
        delay(5);
    }

    lv_label_set_text(objects.labelstatus, "Timeout - Neustart...");
    lv_timer_handler();
    delay(2000);
    ESP.restart();
}

void showBootScreen() {
    lv_disp_load_scr(objects.screenboot);
    lv_bar_set_value(objects.barwifi, 0, LV_ANIM_OFF);
    lv_label_set_text(objects.labelstatus, "Verbinde mit WLAN...");
    lv_timer_handler();

    location = preferences.getString("location", "");
    String ssid = preferences.getString("ssid", "");
    String pass = preferences.getString("pass", "");

    if (ssid.length() == 0) {
        runCaptivePortal();
        return;
    }

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    lv_bar_set_value(objects.barwifi, 20, LV_ANIM_ON);
    lv_timer_handler();

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000UL) {
        delay(300);
        lv_timer_handler();
    }

    if (WiFi.status() != WL_CONNECTED) {
        runCaptivePortal();
        return;
    }

    lv_bar_set_value(objects.barwifi, 100, LV_ANIM_ON);
    String ipStr = "Verbunden!  IP: " + WiFi.localIP().toString();
    lv_label_set_text(objects.labelstatus, ipStr.c_str());
    lv_timer_handler();

    if (MDNS.begin("wettercube")) Serial.println("mDNS: http://wettercube.local");
    startConfigServer();

    delay(5000);
    lv_scr_load_anim(objects.screen1, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, false);
    currentScreen = 1;
    // Animation vollständig abwarten bevor HTTP-Calls starten
    unsigned long animEnd = millis() + 600;
    while (millis() < animEnd) { lv_timer_handler(); delay(5); }
}

// --- GEOCODING (Stadtname → Koordinaten) ---

bool geocodeLocation(const String& city) {
    WiFiClientSecure geoClient;
    geoClient.setInsecure();
    HTTPClient http;
    String encoded = city;
    encoded.replace(" ", "%20");
    String url = "https://geocoding-api.open-meteo.com/v1/search?name=";
    url += encoded;
    url += "&count=1&language=de&format=json";

    Serial.print("Geocoding: "); Serial.println(url);
    http.begin(geoClient, url);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        deserializeJson(doc, payload);

        if (doc["results"].size() > 0) {
            latitude  = doc["results"][0]["latitude"].as<float>();
            longitude = doc["results"][0]["longitude"].as<float>();

            // Koordinaten dauerhaft speichern
            preferences.putFloat("lat", latitude);
            preferences.putFloat("lon", longitude);

            Serial.printf("Koordinaten: %.4f, %.4f\n", latitude, longitude);
            http.end();
            return true;
        }
    }

    Serial.println("Geocoding fehlgeschlagen!");
    http.end();
    return false;
}

// --- WINDRICHTUNG (Grad → Pfeil + Himmelsrichtung) ---

String getWindDirection(int deg) {
    // 8 Sektoren à 45°, versetzt um 22.5° damit N zentriert ist
    const char* richtungen[] = {
        "N",   // 0°
        "NO",  // 45°
        "O",   // 90°
        "SO",  // 135°
        "S",   // 180°
        "SW",  // 225°
        "W",   // 270°
        "NW"   // 315°
    };
    int index = (int)((deg + 22.5) / 45.0) % 8;
    return String(richtungen[index]);
}

// --- WETTER ABRUFEN (Open-Meteo, kein API-Key) ---

void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;

    // Sicherheitsnetz: falls keine Koordinaten vorhanden
    if (latitude == 0.0 && longitude == 0.0) {
        if (!geocodeLocation(location)) return;
    }

    WiFiClientSecure wxClient;
    wxClient.setInsecure();
    HTTPClient http;
    String url = "https://api.open-meteo.com/v1/forecast";
    url += "?latitude="  + String(latitude, 4);
    url += "&longitude=" + String(longitude, 4);
    url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,wind_speed_10m,wind_direction_10m,surface_pressure,weather_code";
    url += "&daily=uv_index_max,sunrise,sunset";
    url += "&hourly=temperature_2m,weather_code";
    url += "&wind_speed_unit=kmh&timezone=auto&forecast_days=2";

    Serial.print("Wetter-URL: "); Serial.println(url);
    http.begin(wxClient, url);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        deserializeJson(doc, payload);

        JsonObject current = doc["current"];

        float temp = current["temperature_2m"].as<float>();
        lv_label_set_text(objects.labeltemp, (String((int)round(temp)) + "°C").c_str());

        setTempColor(objects.labeltemp, temp);

        int humidity = current["relative_humidity_2m"].as<int>();
        lv_label_set_text(objects.labelhum, (String(humidity) + "%").c_str());

        float feelsLike = current["apparent_temperature"].as<float>();
        lv_label_set_text(objects.labelfeelslike, (String((int)round(feelsLike)) + "°C").c_str());

        float wind = current["wind_speed_10m"].as<float>();  // bereits in km/h
        lv_label_set_text(objects.labelwind, (String((int)round(wind)) + " km/h").c_str());

        int windDeg = current["wind_direction_10m"].as<int>();
        lv_label_set_text(objects.labelwinddir, getWindDirection(windDeg).c_str());

        int pressure = current["surface_pressure"].as<int>();
        lv_label_set_text(objects.labelpress, (String(pressure) + " hPa").c_str());

        int wmoCode = current["weather_code"].as<int>();
        updateWeatherIcon(wmoCode);

        // --- Screen 3: UV-Index, Sonnenaufgang, Sonnenuntergang ---
        float uvMax = doc["daily"]["uv_index_max"][0].as<float>();
        lv_label_set_text(objects.labeluv, String((int)round(uvMax)).c_str());

        String sunriseRaw  = doc["daily"]["sunrise"][0].as<String>();
        String sunsetRaw   = doc["daily"]["sunset"][0].as<String>();
        String sunriseTime = (sunriseRaw.length()  >= 16) ? sunriseRaw.substring(11, 16)  : "--:--";
        String sunsetTime  = (sunsetRaw.length()   >= 16) ? sunsetRaw.substring(11, 16)   : "--:--";
        lv_label_set_text(objects.labelaufgang,   sunriseTime.c_str());
        lv_label_set_text(objects.labeluntergang, sunsetTime.c_str());

        // --- Screen 4: Stündliche Vorhersage ---
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            int h = timeinfo.tm_hour;

            lv_obj_t* zeitLabels[3] = { objects.labelh1zeit, objects.labelh2zeit, objects.labelh3zeit };
            lv_obj_t* tempLabels[3] = { objects.labelh1temp, objects.labelh2temp, objects.labelh3temp };
            lv_obj_t* icons[3]      = { objects.imageh1,     objects.imageh2,     objects.imageh3     };

            for (int i = 0; i < 3; i++) {
                int idx = h + 1 + i; // max 23+3=26, liegt sicher in 48h-Array
                float hTemp = doc["hourly"]["temperature_2m"][idx].as<float>();
                int   hCode = doc["hourly"]["weather_code"][idx].as<int>();
                // Zeit direkt aus API-Array lesen (z.B. "2025-06-02T01:00" → "01:00")
                String timeRaw = doc["hourly"]["time"][idx].as<String>();
                String timeStr = (timeRaw.length() >= 16) ? timeRaw.substring(11, 16) : "--:--";
                lv_label_set_text(zeitLabels[i], timeStr.c_str());
                lv_label_set_text(tempLabels[i], (String((int)round(hTemp)) + "°C").c_str());
                setTempColor(tempLabels[i], hTemp);
                setWeatherIcon(icons[i], hCode);
            }

            // --- Regen-Warnung: nächste 2 Stunden prüfen ---
            bool regenKommt = false;
            for (int i = 1; i <= 2; i++) {
                int idx   = h + i;
                int hCode = doc["hourly"]["weather_code"][idx].as<int>();
                if ((hCode >= 51 && hCode <= 67) ||
                    (hCode >= 80 && hCode <= 82) ||
                     hCode >= 95) {
                    regenKommt = true;
                    break;
                }
            }
            if (!regenKommt) regenWarnungBestaetigt = false; // Reset für nächste Warnung
            if (regenKommt && regenWarnungEnabled && !regenWarnungBestaetigt && !regenWarnungAktiv) {
                regenWarnungAktiv = true;
                if (objects.uiscreenwarnung != nullptr) {
                    lv_scr_load(objects.uiscreenwarnung);
                    currentScreen = 99;
                }
            }
        }

        lastWeatherUpdate = millis();
        Serial.printf("Wetter OK: %.1f°C, %d%%, WMO %d, UV %.1f\n", temp, humidity, wmoCode, uvMax);
    } else {
        Serial.printf("Wetter-Fehler: HTTP %d\n", httpCode);
    }

    http.end();
}

// --- POLLEN ---

void setPollenLabel(lv_obj_t* label, float value) {
    const char* text;
    uint32_t    color;

    if (value <= 0) {
        text = "Keine";    color = 0x888888;
    } else if (value <= 10) {
        text = "Gering";   color = 0x00CC44;
    } else if (value <= 30) {
        text = "Maessig";  color = 0xFFCC00;
    } else if (value <= 100) {
        text = "Hoch";     color = 0xFF8800;
    } else {
        text = "Sehr hoch"; color = 0xFF3300;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
}

// Grenzwerte aus dem European Air Quality Index (EEA, 2024er Revision),
// Good+Fair -> gruen, Moderate -> gelb, Poor -> orange, (Very) Poor+ -> rot
void setAirQualityLabel(lv_obj_t* label, float value, float thGreen, float thYellow, float thOrange) {
    uint32_t color;
    if (value <= thGreen)       color = 0x00CC44; // Gruen
    else if (value <= thYellow) color = 0xFFCC00; // Gelb
    else if (value <= thOrange) color = 0xFF8800; // Orange
    else                        color = 0xFF3300; // Rot

    lv_label_set_text(label, String((int)round(value)).c_str());
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
}

void fetchPollen() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (latitude == 0.0 && longitude == 0.0) return;

    WiFiClientSecure pollenClient;
    pollenClient.setInsecure();
    HTTPClient http;
    String url = "https://air-quality-api.open-meteo.com/v1/air-quality";
    url += "?latitude="  + String(latitude, 4);
    url += "&longitude=" + String(longitude, 4);
    url += "&hourly=birch_pollen,grass_pollen,alder_pollen,mugwort_pollen,ragweed_pollen,pm10,pm2_5,ozone,european_aqi";
    url += "&timezone=auto&forecast_days=1";

    Serial.print("Pollen-URL: "); Serial.println(url);
    http.begin(pollenClient, url);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError jsonErr = deserializeJson(doc, payload);
        if (jsonErr) {
            Serial.printf("Pollen/Luft JSON-Fehler: %s\n", jsonErr.c_str());
            http.end();
            return;
        }

        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) { http.end(); return; }
        int h = timeinfo.tm_hour;

        float birke    = doc["hourly"]["birch_pollen"][h].as<float>();
        float graeser  = doc["hourly"]["grass_pollen"][h].as<float>();
        float erle     = doc["hourly"]["alder_pollen"][h].as<float>();
        float beifuss  = doc["hourly"]["mugwort_pollen"][h].as<float>();
        float ambrosia = doc["hourly"]["ragweed_pollen"][h].as<float>();

        setPollenLabel(objects.labelbirkewert,    birke);
        setPollenLabel(objects.labelgraeserwert,  graeser);
        setPollenLabel(objects.labelerlewert,     erle);
        setPollenLabel(objects.labelbeifusswert,  beifuss);
        setPollenLabel(objects.labelambrosiawert, ambrosia);

        Serial.printf("Pollen OK: Birke %.1f, Graeser %.1f, Erle %.1f, Beifuss %.1f, Ambrosia %.1f\n",
                      birke, graeser, erle, beifuss, ambrosia);

        // --- Screen 6: Luftqualität (Ozon, PM10, PM2.5, EU-AQI) ---
        float ozon = doc["hourly"]["ozone"][h].as<float>();
        float pm10 = doc["hourly"]["pm10"][h].as<float>();
        float pm25 = doc["hourly"]["pm2_5"][h].as<float>();
        float aqi  = doc["hourly"]["european_aqi"][h].as<float>();

        setAirQualityLabel(objects.labelozonwert, ozon, 100, 120, 160);
        setAirQualityLabel(objects.labelpm10wert, pm10,  45, 120, 195);
        setAirQualityLabel(objects.labelpm25wert, pm25,  15,  50,  90);
        setAirQualityLabel(objects.labelaqiwert,  aqi,   40,  60,  80);

        Serial.printf("Luftqualitaet OK: Ozon %.1f, PM10 %.1f, PM2.5 %.1f, AQI %.1f\n", ozon, pm10, pm25, aqi);

        // --- Pollen-Warnung prüfen (Schwellwert konfigurierbar) ---
        struct { const char* name; float wert; } pollenListe[] = {
            {"Birke",    birke},
            {"Graeser",  graeser},
            {"Erle",     erle},
            {"Beifuss",  beifuss},
            {"Ambrosia", ambrosia}
        };
        String warnText = "";
        for (auto& p : pollenListe) {
            if (p.wert > 100 && pollenSchwellwert <= 100) {
                warnText = String(p.name) + ": Sehr hoch"; break;
            } else if (p.wert > 30 && pollenSchwellwert <= 30 && warnText == "") {
                warnText = String(p.name) + ": Hoch";
            } else if (p.wert > 10 && pollenSchwellwert <= 10 && warnText == "") {
                warnText = String(p.name) + ": Maessig";
            }
        }
        if (warnText == "") {
            pollenWarnungBestaetigt = false; // Reset für nächste Warnung
        }
        if (warnText != "" && pollenWarnungEnabled && !pollenWarnungBestaetigt && !pollenWarnungAktiv) {
            pollenWarnungAktiv = true;
            lv_scr_load(objects.uiscreenwarnungpollen);
            currentScreen = 98;
            if (objects.labelpollenwarnart != nullptr)
                lv_label_set_text(objects.labelpollenwarnart, warnText.c_str());
        }
    } else {
        Serial.printf("Pollen-Fehler: HTTP %d\n", httpCode);
    }
    http.end();
}

// --- ICON-AUSWAHL nach WMO-Code ---
// WMO Codes: https://open-meteo.com/en/docs#weathervariables

void setWeatherIcon(lv_obj_t* img, int wmoCode) {
    if (wmoCode == 0 || wmoCode == 1) {
        struct tm ti = {};
        bool isNight = false;
        if (getLocalTime(&ti, 0)) isNight = (ti.tm_hour >= 23 || ti.tm_hour < 5);
        lv_img_set_src(img, isNight ? &night_full_moon_clear : &day_clear);
    } else if (wmoCode == 2) {
        lv_img_set_src(img, &day_partial_cloud);
    } else if (wmoCode == 3) {
        lv_img_set_src(img, &overcast);
    } else if (wmoCode == 45) {
        lv_img_set_src(img, &mist);
    } else if (wmoCode == 48) {
        lv_img_set_src(img, &fog);
    } else if (wmoCode >= 51 && wmoCode <= 57) {
        lv_img_set_src(img, &day_rain);
    } else if ((wmoCode >= 61 && wmoCode <= 67) ||
               (wmoCode >= 80 && wmoCode <= 82)) {
        lv_img_set_src(img, &rain);
    } else if ((wmoCode >= 71 && wmoCode <= 77) ||
               wmoCode == 85 || wmoCode == 86) {
        lv_img_set_src(img, &snow);
    } else if (wmoCode >= 95) {
        lv_img_set_src(img, &thunder);
    } else {
        lv_img_set_src(img, &overcast);
    }
}

void updateWeatherIcon(int wmoCode) {
    setWeatherIcon(objects.imagewetter, wmoCode);
}

void setTempColor(lv_obj_t* label, float temp) {
    if (temp >= 24.0) {
        lv_obj_set_style_text_color(label, lv_color_hex(0xFF3300), LV_PART_MAIN | LV_STATE_DEFAULT); // Rot
    } else if (temp >= 15.0) {
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFCC00), LV_PART_MAIN | LV_STATE_DEFAULT); // Gelb
    } else if (temp >= 8.0) {
        lv_obj_set_style_text_color(label, lv_color_hex(0x00CC44), LV_PART_MAIN | LV_STATE_DEFAULT); // Grün
    } else {
        lv_obj_set_style_text_color(label, lv_color_hex(0x00AAFF), LV_PART_MAIN | LV_STATE_DEFAULT); // Blau
    }
}

// --- UHRZEIT & DATUM ---

void updateClock() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    char zeitPuffer[10];
    sprintf(zeitPuffer, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    lv_label_set_text(objects.labeluhr, zeitPuffer);

    const char* wochentage[] = {"So.", "Mo.", "Di.", "Mi.", "Do.", "Fr.", "Sa."};
    char datumsPuffer[32];
    sprintf(datumsPuffer, "%s %02d.%02d.%d",
        wochentage[timeinfo.tm_wday],
        timeinfo.tm_mday,
        timeinfo.tm_mon + 1,
        timeinfo.tm_year + 1900);
    lv_label_set_text(objects.uilabeldatum, datumsPuffer);
}

// =============================================================================
// --- KONFIGURATION LADEN ---
// =============================================================================

void loadConfig() {
    regenWarnungEnabled  = preferences.getBool("regenWarn",   true);
    pollenWarnungEnabled = preferences.getBool("pollenWarn",  true);
    screen2Enabled       = preferences.getBool("scr2",        true);
    screen3Enabled       = preferences.getBool("scr3",        true);
    screen4Enabled       = preferences.getBool("scr4",        true);
    screen5Enabled       = preferences.getBool("scr5",        true);
    screenLuftEnabled    = preferences.getBool("scrLuft",     true);
    pollenSchwellwert    = preferences.getInt("pollenThresh", 30);
    dimTimeoutMin        = preferences.getInt("dimTime",      3);
    brightnessPercent    = preferences.getInt("brightness",   80);
    brightnessPercent    = constrain(brightnessPercent, 10, 100);
    dimTimeoutMs = (dimTimeoutMin <= 0) ? 0 : (unsigned long)dimTimeoutMin * 60UL * 1000UL;
    nachtModusEnabled    = preferences.getBool("nachtModus",  false);
    nachtVon             = preferences.getInt("nachtVon",     22 * 60);
    nachtBis             = preferences.getInt("nachtBis",      6 * 60);
    nachtHelligkeit      = preferences.getInt("nachtHell",    0);
    darkThemeEnabled     = preferences.getBool("darkTheme",   true);
    Serial.printf("Config: regenWarn=%d pollenWarn=%d brightness=%d%% dimTime=%dmin\n",
        regenWarnungEnabled, pollenWarnungEnabled, brightnessPercent, dimTimeoutMin);
}

// =============================================================================
// --- CONFIG WEBSERVER ---
// =============================================================================

void startConfigServer() {
    server.on("/",              handleConfig);
    server.on("/config",        handleConfig);
    server.on("/config/save",   handleConfigSave);
    server.on("/update",        HTTP_GET, handleOTAPage);
    server.on("/update/check",  HTTP_GET, handleOTACheck);
    server.on("/update/install",HTTP_GET, handleOTAInstall);
    server.on("/wifi/scan",       HTTP_GET,  handleWifiScan);
    server.on("/wifi/change",     HTTP_POST, handleWifiChange);
    server.on("/location/change", HTTP_POST, handleLocationChange);
    server.begin();
    Serial.println("Config-Server: http://wettercube.local  |  /update fuer OTA");
}

void handleConfig() {
    String ip  = WiFi.localIP().toString();
    String chk = " checked";

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>WetterCube Einstellungen</title>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:20px;}";
    html += "h1{color:#58a6ff;font-size:1.4em;margin-bottom:4px;}";
    html += "p.sub{color:#8b949e;font-size:.85em;margin-top:0;}";
    html += ".card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:20px;margin:14px 0;max-width:460px;}";
    html += ".card h2{color:#58a6ff;font-size:1em;margin-top:0;}";
    html += ".row{display:flex;align-items:center;justify-content:space-between;padding:8px 0;border-bottom:1px solid #21262d;}";
    html += ".row:last-child{border-bottom:none;}";
    html += ".row label{font-size:.95em;}";
    html += "select{background:#21262d;color:#e6edf3;border:1px solid #30363d;border-radius:5px;padding:5px 10px;font-size:.9em;}";
    html += "input[type=checkbox]{width:18px;height:18px;accent-color:#58a6ff;cursor:pointer;}";
    html += "button{background:#238636;color:#fff;border:none;padding:12px 28px;border-radius:6px;font-size:1em;cursor:pointer;margin-top:6px;width:100%;max-width:460px;}";
    html += "button:hover{background:#2ea043;}";
    html += ".info{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px 16px;max-width:460px;font-size:.85em;color:#8b949e;margin-bottom:14px;}";
    html += ".info span{color:#58a6ff;font-weight:bold;}";
    html += "</style></head><body>";
    html += "<h1>&#9729; WetterCube</h1>";
    html += "<p class='sub'>Einstellungen &amp; Konfiguration</p>";

    // Info-Bereich
    html += "<div class='info'>Ger&auml;t: <span>WetterCube</span> &nbsp;|&nbsp; IP: <span>" + ip + "</span> &nbsp;|&nbsp; ";
    html += "URL: <span>http://wettercube.local</span></div>";

    html += "<form action='/config/save' method='POST'>";

    // --- Warnungen ---
    html += "<div class='card'><h2>&#9888; Warnungen</h2>";
    html += "<div class='row'><label>Regen-Warnung</label>";
    html += "<input type='checkbox' name='regenWarn' value='1'" + String(regenWarnungEnabled ? chk : "") + "></div>";
    html += "<div class='row'><label>Pollen-Warnung</label>";
    html += "<input type='checkbox' name='pollenWarn' value='1'" + String(pollenWarnungEnabled ? chk : "") + "></div>";
    html += "<div class='row'><label>Pollen-Schwellwert</label>";
    html += "<select name='pollenThresh'>";
    html += "<option value='10'"  + String(pollenSchwellwert == 10  ? " selected" : "") + ">M&auml;&szlig;ig (&gt;10)</option>";
    html += "<option value='30'"  + String(pollenSchwellwert == 30  ? " selected" : "") + ">Hoch (&gt;30)</option>";
    html += "<option value='100'" + String(pollenSchwellwert == 100 ? " selected" : "") + ">Sehr hoch (&gt;100)</option>";
    html += "</select></div></div>";

    // --- Screens ---
    html += "<div class='card'><h2>&#128250; Screens aktivieren</h2>";
    html += "<div class='row'><label>Screen 1 – Hauptanzeige</label><input type='checkbox' disabled checked></div>";
    html += "<div class='row'><label>Screen 4 – 3h-Vorhersage</label>";
    html += "<input type='checkbox' name='scr4' value='1'" + String(screen4Enabled ? chk : "") + "></div>";
    html += "<div class='row'><label>Screen 2 – Luftfeuchte / Druck</label>";
    html += "<input type='checkbox' name='scr2' value='1'" + String(screen2Enabled ? chk : "") + "></div>";
    html += "<div class='row'><label>Screen 3 – UV / Sonnenauf- &amp; untergang</label>";
    html += "<input type='checkbox' name='scr3' value='1'" + String(screen3Enabled ? chk : "") + "></div>";
    html += "<div class='row'><label>Screen 5 – Pollen</label>";
    html += "<input type='checkbox' name='scr5' value='1'" + String(screen5Enabled ? chk : "") + "></div>";
    html += "<div class='row'><label>Screen 6 – Luftqualität</label>";
    html += "<input type='checkbox' name='scrLuft' value='1'" + String(screenLuftEnabled ? chk : "") + "></div></div>";

    // --- Display ---
    html += "<div class='card'><h2>&#128261; Display</h2>";
    html += "<div class='row'><label>Helligkeit</label>";
    html += "<div style='display:flex;align-items:center;gap:10px;'>";
    html += "<input type='range' name='brightness' min='10' max='100' value='" + String(brightnessPercent) + "' ";
    html += "style='width:130px;accent-color:#58a6ff;' ";
    html += "oninput='this.nextElementSibling.textContent=this.value+\"%\"'>";
    html += "<span style='min-width:38px;color:#58a6ff;font-weight:bold;'>" + String(brightnessPercent) + "%</span>";
    html += "</div></div>";
    html += "<div class='row'><label>Dimmen nach</label>";
    html += "<select name='dimTime'>";
    html += "<option value='0'"  + String(dimTimeoutMin == 0  ? " selected" : "") + ">Aus</option>";
    html += "<option value='1'"  + String(dimTimeoutMin == 1  ? " selected" : "") + ">1 Minute</option>";
    html += "<option value='3'"  + String(dimTimeoutMin == 3  ? " selected" : "") + ">3 Minuten</option>";
    html += "<option value='5'"  + String(dimTimeoutMin == 5  ? " selected" : "") + ">5 Minuten</option>";
    html += "<option value='10'" + String(dimTimeoutMin == 10 ? " selected" : "") + ">10 Minuten</option>";
    html += "</select></div>";
    html += "<div class='row'><label>Design</label>";
    html += "<select name='darkTheme'>";
    html += "<option value='1'" + String(darkThemeEnabled  ? " selected" : "") + ">Dunkel</option>";
    html += "<option value='0'" + String(!darkThemeEnabled ? " selected" : "") + ">Hell</option>";
    html += "</select></div></div>";

    // --- Nachtmodus ---
    html += "<div class='card'><h2>&#127769; Nachtmodus</h2>";
    html += "<div class='row'><label>Nachtmodus aktiv</label>";
    html += "<input type='checkbox' name='nachtModus' value='1'" + String(nachtModusEnabled ? chk : "") + "></div>";
    // Von/Bis Dropdowns in 15-Min-Schritten
    html += "<div class='row'><label>Display aus von</label><select name='nachtVon'>";
    for (int m = 0; m < 24 * 60; m += 15) {
        char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", m / 60, m % 60);
        html += "<option value='" + String(m) + "'" + String(m == nachtVon ? " selected" : "") + ">" + buf + "</option>";
    }
    html += "</select></div>";
    html += "<div class='row'><label>Display an ab</label><select name='nachtBis'>";
    for (int m = 0; m < 24 * 60; m += 15) {
        char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", m / 60, m % 60);
        html += "<option value='" + String(m) + "'" + String(m == nachtBis ? " selected" : "") + ">" + buf + "</option>";
    }
    html += "</select></div>";
    html += "<div class='row'><label>Helligkeit nachts</label>";
    html += "<select name='nachtHell'>";
    html += "<option value='0'"  + String(nachtHelligkeit == 0  ? " selected" : "") + ">Aus (0%)</option>";
    html += "<option value='5'"  + String(nachtHelligkeit == 5  ? " selected" : "") + ">5%</option>";
    html += "<option value='10'" + String(nachtHelligkeit == 10 ? " selected" : "") + ">10%</option>";
    html += "<option value='20'" + String(nachtHelligkeit == 20 ? " selected" : "") + ">20%</option>";
    html += "</select></div></div>";

    // --- Firmware Update ---
    html += "<div class='card'><h2>&#128257; Firmware</h2>";
    html += "<div class='row'><label>Installierte Version</label><span style='color:#58a6ff;font-weight:bold;'>" FIRMWARE_VERSION "</span></div>";
    html += "<div style='margin-top:12px;'>";
    html += "<a href='/update' style='display:block;background:#1f6feb;color:#fff;text-align:center;padding:10px;border-radius:6px;text-decoration:none;font-size:.95em;'>&#128257; Auf Updates pr&uuml;fen</a>";
    html += "</div></div>";

    html += "<button type='submit'>&#10003; Speichern</button>";
    html += "</form>";

    // --- Standort ändern (eigenes Formular, außerhalb der Config-Form) ---
    html += "<div class='card'><h2>&#127759; Standort</h2>";
    html += "<div class='row'><label>Aktueller Standort</label><span style='color:#58a6ff;font-weight:bold;'>" + location + "</span></div>";
    html += "<form action='/location/change' method='POST' style='margin-top:12px;'>";
    html += "<input type='text' name='loc' placeholder='Neuer Stadtname (z.B. Berlin)' ";
    html += "style='background:#21262d;color:#e6edf3;border:1px solid #30363d;border-radius:5px;padding:8px 10px;";
    html += "width:100%;max-width:420px;box-sizing:border-box;font-size:.95em;margin-bottom:8px;'>";
    html += "<p style='font-size:.8em;color:#8b949e;margin-bottom:8px;'>Bei Umlauten ohne Punkte schreiben: Munchen statt M&uuml;nchen</p>";
    html += "<button type='submit' style='background:#6e40c9;'>&#127759; Standort speichern &amp; Wetter neu laden</button>";
    html += "</form></div>";

    // --- WLAN wechseln (eigenes Formular, außerhalb der Config-Form) ---
    String curSsid = preferences.getString("ssid", "");
    html += "<div class='card'><h2>&#128246; WLAN wechseln</h2>";
    html += "<div class='row'><label>Aktuelles Netzwerk</label><span style='color:#58a6ff;font-weight:bold;'>" + curSsid + "</span></div>";
    html += "<form action='/wifi/change' method='POST' style='margin-top:12px;'>";
    html += "<label style='font-size:.85em;color:#8b949e;display:block;margin-bottom:4px;'>Neues Netzwerk</label>";
    html += "<select name='ssid' id='wifiSel' style='width:100%;max-width:420px;margin-bottom:10px;'>";
    html += "<option value=''>-- Scan starten --</option>";
    html += "</select>";
    html += "<label style='font-size:.85em;color:#8b949e;display:block;margin-bottom:4px;'>Passwort</label>";
    html += "<input type='password' name='pass' placeholder='WLAN-Passwort' style='background:#21262d;color:#e6edf3;border:1px solid #30363d;border-radius:5px;padding:8px 10px;width:100%;max-width:420px;box-sizing:border-box;font-size:.95em;margin-bottom:10px;'>";
    html += "<button type='button' onclick='scanWifi()' style='background:#1f6feb;margin-bottom:8px;'>&#128247; Netzwerke scannen</button>";
    html += "<button type='submit' style='background:#b08800;'>&#128246; Netzwerk wechseln &amp; neu starten</button>";
    html += "</form></div>";

    html += "<script>";
    html += "function scanWifi(){";
    html += "  var sel=document.getElementById('wifiSel');";
    html += "  sel.innerHTML='<option>Scanne...</option>';";
    html += "  fetch('/wifi/scan').then(r=>r.json()).then(nets=>{";
    html += "    sel.innerHTML='';";
    html += "    nets.forEach(function(n){";
    html += "      var o=document.createElement('option');";
    html += "      o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)';";
    html += "      sel.appendChild(o);";
    html += "    });";
    html += "  }).catch(()=>{sel.innerHTML='<option>Fehler beim Scannen</option>';});";
    html += "}";
    html += "</script>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleConfigSave() {
    regenWarnungEnabled  = server.hasArg("regenWarn")  && server.arg("regenWarn")  == "1";
    pollenWarnungEnabled = server.hasArg("pollenWarn") && server.arg("pollenWarn") == "1";
    screen2Enabled       = server.hasArg("scr2")       && server.arg("scr2")       == "1";
    screen3Enabled       = server.hasArg("scr3")       && server.arg("scr3")       == "1";
    screen4Enabled       = server.hasArg("scr4")       && server.arg("scr4")       == "1";
    screen5Enabled       = server.hasArg("scr5")       && server.arg("scr5")       == "1";
    screenLuftEnabled    = server.hasArg("scrLuft")    && server.arg("scrLuft")    == "1";
    pollenSchwellwert    = server.hasArg("pollenThresh") ? server.arg("pollenThresh").toInt() : 30;
    dimTimeoutMin        = server.hasArg("dimTime")      ? server.arg("dimTime").toInt()      : 3;
    brightnessPercent    = server.hasArg("brightness")   ? server.arg("brightness").toInt()   : 80;
    brightnessPercent    = constrain(brightnessPercent, 10, 100);
    bool newDarkTheme    = server.hasArg("darkTheme") ? (server.arg("darkTheme") == "1") : true;

    dimTimeoutMs = (dimTimeoutMin <= 0) ? 0 : (unsigned long)dimTimeoutMin * 60UL * 1000UL;

    // Helligkeit sofort anwenden (nur wenn nicht gedimmt)
    if (!isDimmed) ledcWrite(TFT_BL, getBrightPWM());

    // Design sofort anwenden, falls geändert
    if (newDarkTheme != darkThemeEnabled) {
        darkThemeEnabled = newDarkTheme;
        change_color_theme(darkThemeEnabled ? THEME_DARK : THEME_LIGHT);
    }

    // In Flash speichern
    preferences.putBool("regenWarn",   regenWarnungEnabled);
    preferences.putBool("pollenWarn",  pollenWarnungEnabled);
    preferences.putBool("scr2",        screen2Enabled);
    preferences.putBool("scr3",        screen3Enabled);
    preferences.putBool("scr4",        screen4Enabled);
    preferences.putBool("scr5",        screen5Enabled);
    preferences.putBool("scrLuft",     screenLuftEnabled);
    preferences.putInt("pollenThresh", pollenSchwellwert);
    preferences.putInt("dimTime",      dimTimeoutMin);
    preferences.putInt("brightness",   brightnessPercent);
    preferences.putBool("darkTheme",   darkThemeEnabled);
    nachtModusEnabled = server.hasArg("nachtModus");
    if (server.hasArg("nachtVon"))  nachtVon  = server.arg("nachtVon").toInt();
    if (server.hasArg("nachtBis"))  nachtBis  = server.arg("nachtBis").toInt();
    if (server.hasArg("nachtHell")) nachtHelligkeit = server.arg("nachtHell").toInt();
    preferences.putBool("nachtModus", nachtModusEnabled);
    preferences.putInt("nachtVon",    nachtVon);
    preferences.putInt("nachtBis",    nachtBis);
    preferences.putInt("nachtHell",   nachtHelligkeit);

    Serial.println("Config gespeichert.");

    // Antwort + Redirect zurück zur Config-Seite
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='2;url=/config'>";
    html += "<style>body{font-family:Arial;background:#0d1117;color:#e6edf3;text-align:center;padding:60px;}</style></head><body>";
    html += "<h2>&#10003; Einstellungen gespeichert!</h2><p>Weiterleitung...</p></body></html>";
    server.send(200, "text/html", html);
}

// =============================================================================
// --- OTA UPDATE (GitHub Pages) ---
// =============================================================================

void handleOTAPage() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>WetterCube Update</title>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:20px;}";
    html += "h1{color:#58a6ff;font-size:1.4em;}";
    html += ".card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:20px;max-width:460px;margin:14px 0;}";
    html += ".row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #21262d;}";
    html += ".row:last-child{border-bottom:none;}";
    html += ".btn{display:block;padding:11px 20px;border-radius:6px;border:none;font-size:.95em;cursor:pointer;text-align:center;width:100%;margin-top:10px;text-decoration:none;}";
    html += ".btn-check{background:#1f6feb;color:#fff;}";
    html += ".btn-install{background:#238636;color:#fff;display:none;}";
    html += ".btn-back{background:#21262d;color:#e6edf3;}";
    html += "#status{margin-top:12px;padding:10px;border-radius:6px;font-size:.9em;display:none;}";
    html += ".ok{background:#0d2817;border:1px solid #238636;color:#3fb950;}";
    html += ".info{background:#0d1926;border:1px solid #1f6feb;color:#58a6ff;}";
    html += ".err{background:#2d1117;border:1px solid #f85149;color:#f85149;}";
    html += "</style></head><body>";
    html += "<h1>&#128257; Firmware Update</h1>";
    html += "<div class='card'>";
    html += "<div class='row'><span>Installiert</span><b style='color:#58a6ff;'>" FIRMWARE_VERSION "</b></div>";
    html += "<div class='row'><span>Verf&uuml;gbar</span><span id='avail' style='color:#8b949e;'>–</span></div>";
    html += "<div id='status'></div>";
    html += "<button class='btn btn-check' onclick='checkUpdate()'>&#128257; Auf Updates pr&uuml;fen</button>";
    html += "<button class='btn btn-install' id='btnInstall' onclick='installUpdate()'>&#9889; Jetzt updaten</button>";
    html += "<a href='/config' class='btn btn-back' style='margin-top:8px;'>&#8592; Zur&uuml;ck</a>";
    html += "</div>";
    html += "<script>";
    html += "function setStatus(msg,cls){var s=document.getElementById('status');s.textContent=msg;s.className=cls;s.style.display='block';}";
    html += "function checkUpdate(){";
    html += "  setStatus('Prüfe Version...','info');";
    html += "  fetch('/update/check').then(r=>r.json()).then(d=>{";
    html += "    document.getElementById('avail').textContent=d.available;";
    html += "    if(d.hasUpdate){";
    html += "      setStatus('Neue Version '+d.available+' verfügbar!','ok');";
    html += "      document.getElementById('btnInstall').style.display='block';";
    html += "    } else {";
    html += "      setStatus('Bereits aktuell ('+d.current+')','ok');";
    html += "    }";
    html += "  }).catch(()=>setStatus('Fehler beim Prüfen – WLAN ok?','err'));";
    html += "}";
    html += "function installUpdate(){";
    html += "  document.getElementById('btnInstall').disabled=true;";
    html += "  setStatus('Update wird heruntergeladen und installiert... bitte warten (ca. 30 Sek.)','info');";
    html += "  fetch('/update/install').then(r=>r.text()).then(t=>{";
    html += "    if(t==='ok'){setStatus('Update erfolgreich! Cube startet neu...','ok');}";
    html += "    else{setStatus('Fehler: '+t,'err');document.getElementById('btnInstall').disabled=false;}";
    html += "  }).catch(()=>setStatus('Verbindung verloren – Cube startet neu.','ok'));";
    html += "}";
    html += "</script></body></html>";
    server.send(200, "text/html", html);
}

void handleOTACheck() {
    if (WiFi.status() != WL_CONNECTED) {
        server.send(503, "application/json", "{\"error\":\"no wifi\"}");
        return;
    }
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, OTA_VERSION_URL);
    http.setTimeout(8000);
    int code = http.GET();
    if (code != 200) {
        http.end();
        server.send(502, "application/json", "{\"error\":\"fetch failed\"}");
        return;
    }
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    http.end();
    String available = doc["version"] | FIRMWARE_VERSION;
    bool hasUpdate = (available != String(FIRMWARE_VERSION));
    String resp = "{\"current\":\"" FIRMWARE_VERSION "\",\"available\":\"" + available + "\",\"hasUpdate\":" + (hasUpdate ? "true" : "false") + "}";
    server.send(200, "application/json", resp);
}

void handleOTAInstall() {
    if (WiFi.status() != WL_CONNECTED) {
        server.send(503, "text/plain", "no wifi");
        return;
    }
    Serial.println("OTA: Download startet...");
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, OTA_FIRMWARE_URL);
    http.setTimeout(30000);
    int code = http.GET();
    if (code != 200) {
        http.end();
        Serial.printf("OTA: HTTP Fehler %d\n", code);
        server.send(502, "text/plain", "download error " + String(code));
        return;
    }
    int contentLen = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    Serial.printf("OTA: %d Bytes\n", contentLen);
    if (!Update.begin(contentLen > 0 ? contentLen : UPDATE_SIZE_UNKNOWN)) {
        http.end();
        server.send(500, "text/plain", "begin failed");
        return;
    }
    size_t written = Update.writeStream(*stream);
    bool success = Update.end(true) && !Update.hasError();
    http.end();
    if (success) {
        Serial.printf("OTA: %d Bytes geschrieben – Neustart\n", written);
        server.send(200, "text/plain", "ok");
        delay(1500);
        ESP.restart();
    } else {
        Serial.printf("OTA Fehler: %s\n", Update.errorString());
        server.send(500, "text/plain", Update.errorString());
    }
}

// =============================================================================
// --- WLAN WECHSELN ---
// =============================================================================

void handleWifiScan() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    server.send(200, "application/json", json);
}

void handleWifiChange() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.length() == 0) {
        server.send(400, "text/plain", "Kein Netzwerk gewählt");
        return;
    }
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<style>body{font-family:Arial;background:#0d1117;color:#e6edf3;text-align:center;padding:60px;}"
        "h2{color:#3fb950;}</style></head><body>"
        "<h2>&#10003; Gespeichert!</h2>"
        "<p>WetterCube verbindet sich mit <b>" + ssid + "</b> und startet neu...</p>"
        "</body></html>");
    delay(1500);
    ESP.restart();
}

void handleLocationChange() {
    String loc = server.arg("loc");
    loc.trim();
    if (loc.length() < 2) {
        server.send(400, "text/plain", "Stadtname zu kurz");
        return;
    }
    location = loc;
    preferences.putString("location", loc);
    // Koordinaten zurücksetzen → geocodeLocation() wird beim nächsten Wetter-Abruf ausgelöst
    latitude = 0.0; longitude = 0.0;
    preferences.putFloat("lat", 0.0);
    preferences.putFloat("lon", 0.0);

    server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='3;url=/config'>"
        "<style>body{font-family:Arial;background:#0d1117;color:#e6edf3;text-align:center;padding:60px;}"
        "h2{color:#3fb950;}</style></head><body>"
        "<h2>&#10003; Standort gespeichert!</h2>"
        "<p>Neuer Standort: <b>" + loc + "</b></p>"
        "<p>Wetter wird beim n&auml;chsten Abruf aktualisiert...</p>"
        "</body></html>");

    // Sofort neu geocoden und Wetter laden
    if (WiFi.status() == WL_CONNECTED) {
        if (geocodeLocation(location)) {
            preferences.putFloat("lat", latitude);
            preferences.putFloat("lon", longitude);
        }
        lastWeatherUpdate = 0; // Sofortiger Wetter-Abruf im nächsten loop()
    }
}
