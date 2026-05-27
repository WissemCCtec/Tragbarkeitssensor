
# 📡 CCtec BLE Temperatur-Logger (nRF52 / Bluefruit)

Ein energieeffizienter **Temperatur-Logger mit BLE (Bluetooth Low Energy)** für nRF52-basierte Boards (z. B. Adafruit Bluefruit).

- 📈 Temperaturaufnahme im festen Intervall  
- 💾 Speicherung im internen Flash (LittleFS)  
- 📲 Datenübertragung per BLE (CSV)  
- 🔋 Batteriestatus abrufbar  
- ⚡ Ultra-Low-Power-Modus  

---

# 🚀 Features

- ✅ Temperaturmessung über internen Sensor  
- ✅ Speicherung als CSV (`log.txt`)  
- ✅ Indexverwaltung (`idx.txt`)  
- ✅ BLE-Kommunikation:
  - `dump` → Daten senden  
  - `clear` → Log löschen  
  - `bat` → Batteriestatus  
- ✅ Auto-Switch in Low Power Mode  
- ✅ Double Reset → BLE-Modus aktivieren  
- ✅ Stark optimiert für Stromverbrauch  

---

# 🧰 Hardware Voraussetzungen

- nRF52 Board (z. B. Adafruit Feather nRF52840)
- Batterie (z. B. LiPo)
- Optional:
  - Spannungsteiler für Batterie-Messung an A0

---

# 💻 Einrichtung (Arduino IDE)

## 1. Arduino IDE installieren

👉 https://www.arduino.cc/en/software

---

## 2. Board Support Package installieren

1. Öffne:

   `Datei → Voreinstellungen`

2. Füge unter **Zusätzliche Boardverwalter-URLs** hinzu:

   `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`

3. Dann gehe zu:

   `Werkzeuge → Board → Boardverwalter`

   Suche nach:

   `Adafruit nRF52`

   → Installieren
``


