# WetterCube – Entwicklungs-Log

Dieses Dokument protokolliert alle Funktionen, Entscheidungen und Code-Änderungen
des WetterCube-Projekts. Es dient als Referenz und Backup des Entwicklungsverlaufs.

---

## Projekt-Grundlage

| Eigenschaft | Wert |
|---|---|
| Hardware | ESP32-C3 |
| Display | ST7789, 1,54", 240×240 px, SPI |
| GUI-Framework | LVGL 8.3 |
| Display-Treiber | Arduino_GFX_Library |
| Wetter-API | Open-Meteo (kostenlos, kein API-Key) |
| UI-Design | [PicoPixel](https://picopixel.io) (bis v1.7.4: SquareLine Studio 1.6.1) |
| Repo | https://github.com/JPPeterson-lab/wettercube |
| Web-Installer | https://jppeterson-lab.github.io/wettercube/ |

---

## Hardware-Pinbelegung

| Funktion | GPIO |
|---|---|
| TFT MOSI (SPI) | 20 |
| TFT SCLK (SPI) | 21 |
| TFT CS | 6 |
| TFT DC | 7 |
| TFT RST | 10 |
| TFT Backlight (PWM) | 5 |
| Touch-Taste TTP223 | 3 |

---

## Wichtige Konfiguration

### lv_conf.h (Bibliotheksordner)
```c
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0   // 0 = korrekt für Arduino_GFX + ST7789
```

### ui.c – Kompiliercheck dauerhaft deaktiviert
SquareLine Studio generiert bei jedem Export einen `#error`-Check für
`LV_COLOR_16_SWAP`. Dieser muss nach jedem Export manuell auskommentiert werden:
```c
// #if LV_COLOR_16_SWAP !=0
//     #error "LV_COLOR_16_SWAP should be 0 ..."
// #endif
```
**Merke:** Nach jedem SLS-Export → `ui.c` öffnen → Check auskommentieren.

---

## Screen-Übersicht

| Screen | Inhalt | currentScreen |
|---|---|---|
| ScreenBoot | Boot-Screen mit Ladebalken + IP-Anzeige | – |
| Screen1 | Hauptscreen: Temp, Uhrzeit, Icon, Wind, Windrichtung | 1 |
| Screen4 | 3-Stunden-Forecast mit Icons + Temps | 4 |
| Screen2 | Luftfeuchtigkeit, Luftdruck, gefühlte Temp | 2 |
| Screen3 | UV-Index, Sonnenaufgang, Sonnenuntergang | 3 |
| ScreenPollen | Pollenbelastung alle 5 Arten | 5 |
| uiScreenWarnung | Regen-Warnung, blinkend rot | 99 |
| uiScreenWarnungPollen | Pollen-Warnung, blinkend orange | 98 |

**Touch-Reihenfolge:** 1 → 4 → 2 → 3 → 5 → zurück zu 1 (deaktivierte Screens werden übersprungen)

---

## Implementierte Funktionen

### Boot-Screen (`showBootScreen()`)
- Erscheint beim Start vor Screen 1
- Zeigt "WetterCube" in zwei Farben (Wetter weiß, Cube himmelblau)
- "by Jan Althaus"
- Ladebalken (`uic_BarWifi`) füllt sich während WLAN-Verbindung
- Nach erfolgreicher Verbindung: 4 Sekunden stehen bleiben, dann Fade zu Screen 1
- Bei Fehler: Meldung + Setup-Portal öffnen

### WLAN & Captive Portal (eigene Implementierung, ab v1.6.0)
- Kein externer Library-Overhead – eigene schlanke Implementierung mit `DNSServer` + `WebServer`
- Beim ersten Start oder wenn kein WLAN gespeichert: AP "WetterCube-Setup" öffnet sich automatisch
- Captive Portal öffnet sich auf iOS/Android automatisch beim Verbinden mit dem AP
- Portal zeigt gescannte WLAN-Netzwerke als Dropdown, Passwortfeld, Standortfeld
- WLAN-Credentials (SSID + Passwort) in eigenen Preferences-Keys `ssid` / `pass` gespeichert
- Standort in Preferences-Key `location` gespeichert
- Nach Eingabe: Neustart → Cube verbindet sich direkt, kein zweites Portal nötig
- Timeout Portal: 180 Sek → ESP.restart()
- Bei Verbindungsfehler nach Neustart: Portal öffnet sich erneut automatisch
- Geocoding via Open-Meteo API (Stadtname → Koordinaten), Koordinaten dauerhaft in Preferences
- **Hinweis:** WiFiManager (tzapu) war in v1.5.1 eingebaut, wurde in v1.6.0 entfernt wegen zu großem Sketch (103% → 99%)

### Wetter-Abruf (`fetchWeather()`)
- Intervall: alle 15 Minuten
- API: Open-Meteo forecast
- **Current:** temp, humidity, feelsLike, wind speed+direction, pressure, weather_code
- **Daily:** uv_index_max, sunrise, sunset
- **Hourly:** temperature_2m, weather_code (für Forecast + Regenwarnung)
- JSON-Buffer: 6144 Bytes
- Timezone: auto

### Temperatur-Farbwechsel (`setTempColor()`)
Gilt für Screen 1 und alle 3 Forecast-Labels:
| Temperatur | Farbe |
|---|---|
| unter 8°C | Blau `#00AAFF` |
| 8–14°C | Grün `#00CC44` |
| 15–23°C | Gelb `#FFCC00` |
| ab 24°C | Rot `#FF3300` |

### Wetter-Icons (`setWeatherIcon()`)
Wiederverwendbare Funktion für Main-Screen und Forecast-Screen:
| WMO-Code | Icon |
|---|---|
| 0, 1 | day_clear |
| 2, 3 | overcast |
| 45, 48 | fog |
| 51–67, 80–82 | rain |
| 71–77, 85, 86 | snow |
| ≥95 | thunder |

### Windrichtung (`getWindDirection()`)
8 Himmelsrichtungen als Text: N, NO, O, SO, S, SW, W, NW
Berechnung: `(deg + 22.5) / 45.0) % 8`

### 3-Stunden-Forecast (Screen 4)
- Nächste 3 Stunden ab aktueller Uhrzeit
- Je Stunde: Uhrzeit (HH:00), Wettericon, Temperatur mit Farbwechsel
- Widgets: `uic_LabelH1/2/3Zeit`, `uic_ImageH1/2/3`, `uic_LabelH1/2/3Temp`

### Pollen-Screen (`ScreenPollen`)
- Separater API-Aufruf: Open-Meteo Air Quality API (kostenlos, kein Key)
- 5 Pollenarten: Birke, Gräser, Erle, Beifuß, Ambrosia
- Farbindikator: Keine (grau) / Gering (grün) / Mäßig (gelb) / Hoch (orange) / Sehr hoch (rot)
- Abruf gleichzeitig mit Wetter alle 15 Minuten
- API-Mapping: birch_pollen, grass_pollen, alder_pollen, mugwort_pollen, ragweed_pollen
- Widgets: `uic_LabelBirkeWert`, `uic_LabelGraeserWert`, `uic_LabelErleWert`, `uic_LabelBeifussWert`, `uic_LabelAmbrosiaWert`

### Pollen-Warnung (`uiScreenWarnungPollen`)
- Trigger: irgendein Pollenwert > 30 (Hoch) oder > 100 (Sehr hoch)
- Sehr hoch hat Vorrang – wird zuerst angezeigt
- Blinkt zwischen `#FF8800` und `#CC5500` (alle 500ms)
- `uic_LabelPollenWarnArt` zeigt z.B. "Birke: Sehr hoch"
- Bestätigung: Touch → zurück zu Screen 1
- Reset wenn alle Werte wieder ≤ 30

### Regen-Warnung (`uiScreenWarnung`)
- Prüft stündliche Wettercodes für die nächsten 2 Stunden
- Auslöser: WMO 51–67 (Regen), 80–82 (Schauer), ≥95 (Gewitter)
- Anzeige: roter Screen blinkt zwischen `#CC0000` und `#660000` (alle 500ms)
- Bestätigung: Touch-Button → zurück zu Screen 1
- Reset: wenn kein Regen mehr vorhergesagt → kann erneut warnen
- Widgets: `uic_ImageWarnRegen`, `uic_LabelWarnTitel`, `uic_LabelWarnDetail`, `uic_LabelWarnHint`

### Display-Dimmen
- Normal: 80% Helligkeit (PWM 204/255)
- Timeout: konfigurierbar (0=Aus / 1 / 3 / 5 / 10 Min), Standard 3 Min → 20%
- Aufwecken: Touch-Taste → erste Betätigung nur aufhellen, kein Screen-Wechsel
- Pin: GPIO 5, PWM via `ledcAttach()` (ESP32 Arduino Core 3.x API)

### OTA-Update (ab v1.6.0)
- Erreichbar unter `http://wettercube.local/update`
- Button "Auf Updates prüfen" → Cube holt `docs/version.json` von GitHub Pages
- Bei neuer Version: "Jetzt updaten" → lädt `firmware.bin` herunter, flasht sich selbst, startet neu
- Kein USB nötig – funktioniert komplett über WLAN
- `FIRMWARE_VERSION` im Code muss mit `docs/version.json` synchron sein
- HTTPS-Download via `WiFiClientSecure` mit `setInsecure()`

### Helligkeit (ab v1.6.0)
- Slider 10–100% im Webinterface, Preferences-Key: `brightness`
- Wird sofort angewendet ohne Neustart

### Web-Konfiguration
- Erreichbar unter `http://wettercube.local` (mDNS) oder direkt per IP
- IP wird nach WLAN-Connect 5 Sekunden auf dem Boot-Screen angezeigt
- Konfigurierbare Optionen:
  - Regen-Warnung ein/aus
  - Pollen-Warnung ein/aus
  - Pollen-Schwellwert: Mäßig (>10) / Hoch (>30) / Sehr hoch (>100)
  - Screens 2–5 einzeln aktivieren/deaktivieren
  - Dimm-Timeout: Aus / 1 / 3 / 5 / 10 Minuten
- Einstellungen werden in Preferences (Flash) gespeichert → bleiben nach Neustart
- Config-Server läuft auf Port 80 im Normalbetrieb (separater Pfad vom Setup-Portal)

### Uhrzeit & Datum (`updateClock()`)
- NTP: `europe.pool.ntp.org`
- Timezone: `CET-1CEST,M3.5.0,M10.5.0/3`
- Update: jede Sekunde
- Format Uhrzeit: `HH:MM`
- Format Datum: `Mo. 01.06.2025`

---

### OTA-Voraussetzung: Partition Scheme
- OTA benötigt zwei App-Partitionen im Flash → Partition Scheme muss OTA-fähig sein
- Korrekte Einstellung: `Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)`
- Standard-Partition hat keinen OTA-Slot → `Update.begin()` scheitert mit "begin failed"
- Nach Wechsel des Partition Scheme: alle 4 Bins neu kompilieren und exportieren

---

## Bekannte Eigenheiten

- **LV_COLOR_16_SWAP** muss nach jedem SLS-Export in `ui.c` auskommentiert werden
- Unicode-Pfeile (↑↗→) werden von Montserrat nicht unterstützt → Klartext-Richtungen verwenden
- ESP32 Arduino Core 3.x: `ledcSetup()`/`ledcAttachPin()` → ersetzt durch `ledcAttach(pin, freq, res)`
- JSON-Buffer Wetter: 8192 Bytes (forecast_days=2 → 48 Stunden-Einträge)
- JSON-Buffer Pollen: 4096 Bytes (forecast_days=1 → 24 Einträge)
- Forecast-Index darf nie auf 23 geclampt werden → forecast_days=2 + Zeit aus API-Array lesen
- Web-Installer braucht Chrome oder Edge (WebSerial API)
- Nach GitHub-Repo privat/öffentlich Wechsel: GitHub Pages manuell reaktivieren unter Settings → Pages

---

## Versions-Historie

| Version | Inhalt |
|---|---|
| 1.0.0 | Erste Firmware: WLAN, Wetter, LVGL, Captive Portal |
| 1.0.1 | UI-Anpassungen |
| 1.1.0 | Screen 3 (UV, Sunrise, Sunset), Long-Press Reset (später entfernt) |
| 1.1.1 | Windrichtung, Display-Dimmen, Temperatur-Farbwechsel, UI-Überarbeitung |
| 1.2.0 | Boot-Screen, 3-Stunden-Forecast (Screen 4), Regen-Warnung |
| 1.3.0 | Display-Helligkeit 80%, Temperaturfarben auf Forecast, Screenreihenfolge 1→4→2→3 |
| 1.3.1 | Bugfix: Forecast-Überlauf nach 23:00 (forecast_days=2, Zeit aus API-Array) |
| 1.4.0 | Pollen-Screen mit Open-Meteo Air Quality API |
| 1.4.1 | Pollen-Warnscreen (blinkend orange, Touch-Quittierung) |
| 1.5.0 | Web-Konfiguration (mDNS, IP-Anzeige Boot-Screen, Config-UI) |
| 1.5.1 | WiFiManager (tzapu) ersetzt eigenes Captive Portal |
| 1.6.0 | OTA-Update via GitHub Pages, Helligkeitsregler, WiFiManager durch eigenes schlankes Portal ersetzt (Sketch 103% → 99%) |
| 1.6.1 | OTA-Testrelease – bestätigt funktionierende OTA-Update-Kette |
| 1.6.2 | Crash-Fix: LVGL-Animation vor HTTP-Calls abwarten, Pollen-Warnung NULL-gesichert |
| 1.7.0 | WLAN-Wechsel im Web-UI, Custom Partition Table (SPIFFS entfernt, App 2MB), lv_conf.h projektlokal via build_opt.h |
| 1.7.1 | Bugfix: Nachtmodus Touch-Wake (15s Timer), Config-Hang durch WLAN-Auto-Scan behoben, Standort-Formular-Fix |
| 1.7.2 | OTA-URLs auf korrekten GitHub Pages Pfad (WetterCube) korrigiert |
| 1.7.3 | Bugfix: Ortsnamen mit Leerzeichen (URL-Enkodierung %20 für Geocoding) |

---

## Bekannte Bugs & Fixes

### v1.6.2: Crash nach WLAN-Verbindung (LVGL-Animation)
**Problem:** Nach WLAN-Connect startete `lv_scr_load_anim()` eine 500ms-Animation, danach begannen sofort die HTTP-Calls für Wetter und Pollen. Der LVGL-Animations-Callback feuerte genau während des ersten HTTP-Requests → Null-Pointer-Zugriff auf LVGL-Objekt (MTVAL=0x0020) → Guru Meditation Error / Crash-Loop.

**Fix:** Nach `lv_scr_load_anim()` wird die Animation vollständig abgewartet (600ms `lv_timer_handler()`-Loop) bevor HTTP-Calls starten.

**Zusatz:** Pollen-Warnscreen-Labels mit NULL-Guards abgesichert für den Fall dass die Widgets noch nicht initialisiert sind.

---

---

## v1.7.0 – WLAN-Wechsel, Custom Partition, lv_conf.h projektlokal

### WLAN-Wechsel im Web-UI
- Neuer Bereich "WLAN wechseln" auf der Config-Seite (`http://wettercube.local`)
- Zeigt aktuell verbundenes Netzwerk, scannt beim Laden automatisch verfügbare Netzwerke
- Netzwerk + Passwort speichern → Cube startet neu und verbindet sich mit dem neuen WLAN
- Kein Captive Portal nötig – funktioniert vollständig im normalen Betrieb
- Neue Endpunkte: `GET /wifi/scan` (JSON), `POST /wifi/change`

### Custom Partition Table
- `partitions.csv` im Sketch-Ordner: SPIFFS-Partition entfernt (wird nicht genutzt, NVS/Preferences bleibt)
- App-Partitionen von 1.966.080 auf **2.031.616 Bytes** vergrößert (+65KB)
- Löst das Größenproblem mit Board Package 3.3.10 dauerhaft
- `Tools → Partition Scheme → Custom` in Arduino IDE wählen
- **Hinweis:** Erster Flash nach Partitionsänderung muss vollständig sein (alle 4 Bins), danach OTA normal

### lv_conf.h projektlokal via build_opt.h
- `lv_conf.h` in Arduino-Bibliotheken hat jetzt `#ifndef`-Guards um `LV_COLOR_16_SWAP` und `LV_MEM_SIZE`
- WetterCube setzt `-DLV_COLOR_16_SWAP=0 -DLV_MEM_SIZE=65536` in `build_opt.h`
- Andere Projekte können abweichende Werte in ihrer `build_opt.h` setzen ohne WetterCube zu beeinflussen

---

## v1.7.1 – Bugfixes Nachtmodus & Web-UI

### Nachtmodus Touch-Wake (15s Timer)
- Nach Touch im Nachtmodus kehrt das Display nach **15 Sekunden** Inaktivität automatisch zur Nachtmodus-Helligkeit zurück
- Jeder weitere Touch verlängert den Timer um 15 Sekunden
- Normaler Inaktivitäts-Dimmer greift im Nachtmodus nicht mehr
- `nachtOverride`-Flag mit `nachtOverrideUntil`-Timestamp steuert das Verhalten

### Config-Hang durch WLAN-Auto-Scan behoben
- `WiFi.scanNetworks()` ist ein blockierender Call (~2–4 Sekunden)
- Wurde bisher automatisch beim Laden der Config-Seite ausgelöst (`scanWifi()` im JS)
- Jetzt nur noch auf expliziten Klick auf "Netzwerke scannen"

### Standort-Formular verschachtelt (Fix)
- Standort-`<form>` war innerhalb des Haupt-`<form>` (ungültiges HTML) → Browser ignoriert innere Forms
- Jetzt eigenes Formular außerhalb des Config-Formulars

*Zuletzt aktualisiert: Juni 2026*

---

## v1.7.2 – OTA-URL-Fix & v1.7.3 – Geocoding URL-Encoding

- OTA-URLs auf korrekten GitHub Pages Pfad (`WetterCube` mit Großbuchstabe) korrigiert
- Geocoding: Städtenamen mit Leerzeichen (z.B. "Bad Schwartau") per `%20` URL-kodiert

---

## v1.7.4 – Neue Wetter-Icons, HTTPS-Fix, Heap-Optimierung

### Neue Icons
- **WMO 2 → `day_partial_cloud`**: Eigenes Icon für leicht bewölkt (statt overcast)
- **WMO 0/1 nachts (23:00–04:59) → `night_full_moon_clear`**: Mondicon statt Sonnensymbol
- WMO 3 bleibt `overcast`

### HTTPS mit WiFiClientSecure (wichtiger Bugfix)
- `HTTPClient::begin(url)` für HTTPS-URLs schlug mit HTTP -1 fehl in aktuellen Board-Package-Versionen
- Geocoding, Wetter und Pollen nutzen jetzt `WiFiClientSecure client; client.setInsecure(); http.begin(client, url)`
- Betrifft alle Geräte nach NVS-Reset (zuvor: Koordinaten aus Cache → Geocoding nie aufgerufen → Fehler unsichtbar)

### LVGL-Heap vs. SSL-Heap
- `LV_MEM_SIZE` von 128 KB auf 96 KB reduziert
- 128 KB ließ nur ~50 KB freien Heap → WiFiClientSecure SSL-Handshake fehlgeschlagen
- 96 KB → ~82 KB freier Heap → SSL funktioniert, Icons und Farben bleiben vollständig erhalten

### getLocalTime() Watchdog-Fix
- `getLocalTime()` ohne Timeout blockiert bis zu 5 Sekunden auf NTP-Sync
- Alle Aufrufe außerhalb NTP-Init nutzen jetzt `getLocalTime(&ti, 0)` (sofortiger Rückgabe)

*Zuletzt aktualisiert: Juli 2026*

---

## v1.8.0 – UI-Redesign mit PicoPixel

### Wechsel von SquareLine Studio zu PicoPixel
- Das komplette LVGL-UI wurde neu in [PicoPixel](https://picopixel.io) aufgebaut, einem browserbasierten LVGL-Editor (Penpot-basiert)
- Alle 8 Screens (Hauptanzeige, Luftdruck/-feuchte, UV/Sonnenauf-/untergang, 3h-Vorhersage, Boot-Screen, Pollen, Regen-Warnung, Pollen-Warnung) wurden dort neu erstellt und exportiert
- PicoPixels Export-Struktur unterscheidet sich grundlegend von SquareLine Studio:
  - Statt einzelner globaler Zeiger pro Widget (`ui_LabelTemp`, `uic_LabelWindDir`) gibt es eine zentrale `objects_t`-Struktur (`objects.labeltemp`, `objects.labelwinddir`)
  - Screens werden über `create_screen_screen1()` statt `ui_Screen1_screen_init()` erzeugt, geladen über `loadScreen(SCREEN_ID_...)` bzw. direkt per `lv_scr_load()` auf `objects.screenX`
  - Icon-Variablen heißen jetzt `day_clear`, `rain` usw. statt `ui_img_day_clear_png`
  - Alle betroffenen Referenzen in `wettercube.ino` wurden entsprechend umbenannt
- Fonts werden von PicoPixel als eigene `.c`-Dateien pro Größe exportiert (dieses Mal 16/18/22/28/48 – Größe 20 entfiel, da nicht mehr verwendet)
- Icon-Assets liegen in einem `images/`-Unterordner, der über `#include "images/xyz.c"` textuell in `images.c` eingebunden wird (Arduino kompiliert dieses Verzeichnis daher nicht separat)

### Design-Umschaltung Hell/Dunkel
- PicoPixel generiert ein `colors.c/h` mit `change_color_theme(THEME_DARK / THEME_LIGHT)`, das Hintergrund- und Textfarben der wichtigsten Widgets umschaltet
- In der Web-UI unter „Display“ als Dropdown ergänzt, wird sofort angewendet und in den Preferences gespeichert (`darkTheme`-Key)

### Icon-Feinabstimmung
- Nebel: WMO 45 (normaler Nebel) → neues Icon `mist`, WMO 48 (Reifnebel) → weiterhin `fog`
- Regen: WMO 51–57 (Niesel-/Sprühregen) → neues Icon `day_rain`, WMO 61–67 + 80–82 (Regen/Schauer) → weiterhin `rain`

### Speicherproblem: 256×256px-Warnicon
- Das Pollen-Warnicon (`alert`) wurde ursprünglich als 256×256px-Asset hochgeladen → 196 KB allein für dieses eine Bild im Flash
- Nach Verkleinern auf 80×80px (passend zum bestehenden `rain_80x80`-Icon) sank der Speicherbedarf auf 19 KB
- Sketch-Größe nach der kompletten Migration: ~1,79 MB von 2,03 MB verfügbarer Partition (vorher bei v1.7.4: ~2,0 MB, entsprechend knapperer Puffer)

---

## v1.8.1 – Screen 6: Luftqualität

### Neuer Screen
- In PicoPixel im gleichen Layout-Raster wie ScreenPollen aufgebaut (Titel oben, Zeilen mit 35px Abstand, linksbündige Namen, zentrierte Werte)
- Zeigt vier Werte: Ozon, PM10, PM2.5 und den europäischen Luftqualitätsindex (`european_aqi`)
- Alle vier über denselben Air-Quality-API-Call wie die Pollendaten abgerufen (`&hourly=...,pm10,pm2_5,ozone,european_aqi`), kein zusätzlicher HTTP-Request nötig

### Farbcodierung nach EEA-Grenzwerten
Die offiziellen EU-Luftqualitätsindex-Grenzwerte (EEA, 2024er Revision) haben 6 Kategorien
(Good/Fair/Moderate/Poor/Very Poor/Extremely Poor), die für die 4-Farben-Darstellung
(grün/gelb/orange/rot) zusammengefasst wurden:

| Schadstoff | Grün ≤ | Gelb ≤ | Orange ≤ | Rot > |
|---|---|---|---|---|
| PM2.5 (µg/m³) | 15 | 50 | 90 | 90 |
| PM10 (µg/m³) | 45 | 120 | 195 | 195 |
| Ozon (µg/m³) | 100 | 120 | 160 | 160 |
| EU-AQI | 40 | 60 | 80 | 80 |

### Web-UI
- Neue Checkbox „Screen 6 – Luftqualität" unter „Screens aktivieren", gleiches Muster wie die bestehenden Screen-Toggles (Preference-Key `scrLuft`)
- Navigationsreihenfolge erweitert: 1 → 4 → 2 → 3 → 5 → 6

---

## v1.8.2 – Bugfix: Luftqualitätswerte immer grün

### Ursache
- `DynamicJsonDocument doc(8192)` im Pollen-/Luftqualitäts-Request war zu klein, nachdem
  vier weitere Stunden-Arrays (pm10, pm2_5, ozone, european_aqi) zu den fünf Pollen-Arrays
  hinzukamen
- `deserializeJson()` schlug lautlos fehl (Rückgabewert wurde nicht geprüft), alle
  gelesenen Werte blieben 0 – und 0 liegt bei jeder Farbschwelle im grünen Bereich
- Bekanntes Fallstrick-Muster in diesem Projekt (siehe Lessons Learned), diesmal durch die
  neue Luftqualitäts-Erweiterung erneut ausgelöst

### Fix
- Alle vier `DynamicJsonDocument`-Stellen (Geocoding, Wetter, Pollen/Luft, OTA-Check) auf
  ArduinoJson v7s elastisches `JsonDocument` umgestellt – wächst automatisch mit der
  tatsächlichen Antwortgröße, kein Kapazitäts-Raten mehr nötig
- Für den Pollen-/Luft-Request zusätzlich den `DeserializationError`-Rückgabewert geprüft
  und bei Fehler ins Serial-Log geschrieben, damit ein künftiges Problem sichtbar wird
  statt sich als "alles zeigt 0/grün an" zu tarnen

### Zweiter Fund: kaputter Include-Pfad
- Ein später PicoPixel-Reexport hatte `screens.c`/`styles.c` erneut auf
  `#include "fonts/fonts.h"` zurückgesetzt (die alte, nicht-flache Verzeichnisstruktur),
  statt der im Projekt verwendeten flachen `fonts.h`
- Das bereits veröffentlichte v1.8.1-Release-Binary war davon nicht betroffen (vor dem
  fehlerhaften Reexport gebaut), aber der Quellcode im Repository war für niemand sonst
  kompilierbar
- Vor jedem Release künftig: nach dem letzten Datei-Edit nochmal frisch kompilieren,
  nicht auf einen früheren erfolgreichen Compile-Lauf im selben Turn verlassen

*Zuletzt aktualisiert: September 2026*
