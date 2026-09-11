# Changelog – WetterCube

Alle relevanten Änderungen je Version. Neueste Version zuerst.

---

## [1.8.2] – 2026-09-11

### Behoben
- **Luftqualitäts-Werte immer grün** – `DynamicJsonDocument` für den Pollen-/Luftqualitäts-Request war seit dem Hinzufügen von Ozon/PM10/PM2.5/EU-AQI zu klein, `deserializeJson` schlug lautlos fehl und alle Werte blieben 0 (fällt bei jeder Schwelle in den grünen Bereich). Alle JSON-Parsing-Stellen im Projekt nutzen jetzt ArduinoJson v7s elastisches `JsonDocument` statt einer fest geschätzten Kapazität; zusätzlich Fehler-Log bei fehlgeschlagenem Parsing ergänzt
- **Kaputter Include-Pfad** – `screens.c`/`styles.c` verwiesen durch einen späten PicoPixel-Reexport wieder auf `fonts/fonts.h` statt `fonts.h`, wodurch der Quellcode für niemanden außer dem bereits gebauten Release-Binary kompilierbar war

---

## [1.8.1] – 2026-08-15

### Neu
- **Screen 6: Luftqualität** – Neuer Screen zeigt Ozon, PM10, PM2.5 und den europäischen Luftqualitätsindex (EU-AQI), farblich codiert (grün/gelb/orange/rot) nach den offiziellen EEA-Grenzwerten (2024er Revision)
- Werte werden über denselben Air-Quality-API-Aufruf wie die Pollendaten abgerufen (kein zusätzlicher HTTP-Request)
- Screen ist wie die anderen über die Web-UI einzeln an-/abschaltbar

---

## [1.8.0] – 2026-08-14

### Neu
- **UI-Redesign** – Das komplette Interface wurde von SquareLine Studio auf [PicoPixel](https://picopixel.io) umgestellt, einen browserbasierten LVGL-Editor. Alle 8 Screens wurden dort neu aufgebaut und exportiert
- **Design-Umschaltung (Hell/Dunkel)** – Neue Einstellung in der Web-UI unter „Display“, wird sofort angewendet (kein Neustart nötig) und dauerhaft gespeichert
- **Icon-Feinabstimmung** – Nebel unterscheidet jetzt zwischen leichtem Nebel (`mist`, WMO 45) und Reifnebel (`fog`, WMO 48); Niesel-/Sprühregen (WMO 51–57) zeigt ein eigenes Icon (`day_rain`) statt des Regen-Icons für stärkeren Regen

### Behoben
- **Pollen-Warnicon Speicherverbrauch** – Das Warnsymbol lag als 256×256px-Bild vor (196 KB im Flash) und wurde auf 80×80px verkleinert (19 KB), passend zum bereits vorhandenen Regen-Warnicon

### Technisch
- Codebasis von SquareLine-generierten Dateien (`ui_*.c/h`) auf PicoPixels Export-Struktur (`screens.c/h`, `images.c/h`, `colors.c/h` u.a.) umgestellt
- Alle Widget-Referenzen im Code von einzelnen globalen Zeigern (`ui_LabelTemp`) auf eine zentrale `objects`-Struktur (`objects.labeltemp`) umgestellt

---

## [1.7.4] – 2026-07-22

### Neu
- **Wetter-Icon: teilweise bewölkt** – WMO-Code 2 zeigt jetzt ein eigenes Icon (`day_partial_cloud`)
- **Wetter-Icon: Nacht klarer Himmel** – WMO-Code 0/1 zwischen 23:00 und 04:59 Uhr zeigt Mondicon (`night_full_moon_clear`) statt Sonnensymbol

### Behoben
- **HTTPS-Verbindungen** – Geocoding, Wetterdaten und Pollendaten nutzen jetzt durchgehend `WiFiClientSecure` mit `setInsecure()`. Betrifft alle Geräte die nach einem NVS-Reset neu geocodiert werden müssen (zuvor schlugen HTTPS-Calls mit HTTP -1 fehl)
- **LVGL-Heap vs. SSL-Heap** – `LV_MEM_SIZE` von 128 KB auf 96 KB reduziert; gibt ~32 KB mehr Heap für SSL-Handshake frei, Icons und Farben bleiben erhalten
- **Watchdog durch blockierendes `getLocalTime()`** – alle `getLocalTime()`-Aufrufe außerhalb von NTP-Init nutzen jetzt Timeout 0 (kein Warten auf NTP-Sync)

---

## [1.7.3] – 2026-06-29

### Behoben
- **Ortsnamen mit Leerzeichen** (z.B. "Bad Schwartau") wurden nicht korrekt geocodiert – Leerzeichen werden jetzt als `%20` URL-enkodiert

---

## [1.7.2] – 2026-06-19

### Behoben
- **OTA-URLs korrigiert** – Repository wurde zu `WetterCube` (Großbuchstabe) umbenannt, GitHub Pages URL hatte sich dadurch geändert. OTA-Check und Firmware-Download zeigen jetzt auf die korrekte URL.

---

## [1.7.1] – 2026-06-19

### Behoben
- **Nachtmodus Touch-Wake** – nach Touch kehrt Display nach 15 Sekunden Inaktivität automatisch zur Nachtmodus-Helligkeit zurück; weitere Touches verlängern den Timer; normaler Dimm-Timeout greift im Nachtmodus nicht mehr
- **Config-Seite Hang** – WLAN-Scan wird nicht mehr automatisch beim Laden ausgelöst, sondern nur noch auf Knopfdruck
- **Standort-Formular** – war innerhalb des Haupt-Formulars verschachtelt (ungültiges HTML), dadurch konnte Speichern nicht funktionieren; jetzt eigenes Formular außerhalb

---

## [1.7.0] – 2026-06-14

### Neu
- **WLAN-Wechsel im Web-UI** – Netzwerk direkt über `http://wettercube.local` wechseln, kein Captive Portal nötig. Scan-Button lädt verfügbare Netzwerke, nach Speichern startet der Cube neu und verbindet sich automatisch.
- **Custom Partition Table** – SPIFFS-Partition entfernt (wird nicht genutzt), App-Partitionen von 1,87 MB auf **1,94 MB** vergrößert. Löst das Größenproblem mit ESP32 Arduino Core 3.3.10 dauerhaft.
- **lv_conf.h projektlokal** – `LV_COLOR_16_SWAP` und `LV_MEM_SIZE` werden jetzt projektspezifisch über `build_opt.h` gesetzt (`-DLV_COLOR_16_SWAP=0`). Andere Projekte können abweichende Werte nutzen ohne den WetterCube zu beeinflussen.

### Hinweis
- Erster Flash nach v1.7.0 muss **vollständig** sein (alle 4 Bins via Web-Installer oder USB), danach läuft OTA wieder normal.

---

## [1.6.2] – 2026-06

### Behoben
- **Crash nach WLAN-Verbindung** – LVGL-Animations-Callback feuerte während des ersten HTTP-Requests → Null-Pointer-Zugriff (MTVAL=0x0020). Fix: 600 ms `lv_timer_handler()`-Loop nach `lv_scr_load_anim()` vor HTTP-Calls.
- **Pollen-Warnung NULL-Guard** – `uic_LabelPollenWarnArt` wird vor Zugriff auf `nullptr` geprüft.

---

## [1.6.1] – 2026-06

### Geändert
- OTA-Testrelease – bestätigt funktionierende OTA-Update-Kette (Version → Check → Download → Flash → Neustart).

---

## [1.6.0] – 2026-06

### Neu
- **OTA-Update via GitHub Pages** – Firmware-Update komplett ohne USB unter `http://wettercube.local/update`.
- **Helligkeitsregler** – Slider 10–100 % im Webinterface, wird sofort angewendet, bleibt nach Neustart erhalten.
- **Eigenes Captive Portal** (ersetzt WiFiManager/tzapu) – DNSServer + WebServer, schlanker, kein externer Library-Overhead. Sketch-Größe von 103 % → 99 %.

### Entfernt
- WiFiManager (tzapu) – zu groß für das Partition-Schema mit OTA.

---

## [1.5.1] – 2026

### Geändert
- WiFiManager (tzapu) ersetzt das eigene Captive Portal.

---

## [1.5.0] – 2026

### Neu
- **Web-Konfiguration** – mDNS (`wettercube.local`), IP-Anzeige auf Boot-Screen, Config-UI im Browser.
- Einstellungen: Regen-Warnung, Pollen-Warnung, Pollen-Schwellwert, Screens 2–5, Dimm-Timeout.

---

## [1.4.1] – 2026

### Neu
- **Pollen-Warnscreen** – blinkt orange bei Belastung Hoch/Sehr hoch, Quittierung per Touch.

---

## [1.4.0] – 2026

### Neu
- **Pollen-Screen** – Open-Meteo Air Quality API, 5 Pollenarten (Birke, Gräser, Erle, Beifuß, Ambrosia), Farbindikator je Stufe.

---

## [1.3.1] – 2026

### Behoben
- Forecast-Überlauf nach 23:00 Uhr – `forecast_days=2` und Zeit direkt aus API-Array lesen statt Modulo.

---

## [1.3.0] – 2026

### Geändert
- Display-Helligkeit auf 80 % gesetzt.
- Temperaturfarben auf Forecast-Labels ausgeweitet.
- Screen-Reihenfolge: 1 → 4 → 2 → 3.

---

## [1.2.0] – 2026

### Neu
- **Boot-Screen** mit Ladebalken und IP-Anzeige.
- **3-Stunden-Forecast** (Screen 4).
- **Regen-Warnscreen** – blinkt rot bei Regen in den nächsten 2 Stunden, Quittierung per Touch.

---

## [1.1.1] – 2026

### Neu
- Windrichtung (8 Himmelsrichtungen als Text).
- Display-Dimmen nach Inaktivität.
- Temperatur-Farbwechsel (blau / grün / gelb / rot).
- UI-Überarbeitung.

---

## [1.1.0] – 2026

### Neu
- Screen 3: UV-Index, Sonnenaufgang, Sonnenuntergang.

---

## [1.0.1] – 2026

### Geändert
- UI-Anpassungen.

---

## [1.0.0] – 2026

### Erstveröffentlichung
- WLAN-Verbindung, Wetterdaten (Open-Meteo), LVGL-UI, Captive Portal.
