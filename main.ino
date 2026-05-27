#include <bluefruit.h>
#include <nrf_soc.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

#define BATT_FULL_MV       3000
#define BATT_EMPTY_MV      2400

#define LOG_INTERVAL_MS    (15UL * 60UL * 1000UL)
#define BLE_TIMEOUT_MS     (1UL * 60UL * 1000UL)
#define DBL_RST_MAGIC      0xAA
#define DBL_RST_WINDOW_MS  500

BLEService        svc    ("19B10000-E8F2-537E-4F6C-D104768A1214");
BLECharacteristic cmdChar("19B10001-E8F2-537E-4F6C-D104768A1214");
BLECharacteristic csvChar("19B10002-E8F2-537E-4F6C-D104768A1214");

volatile bool connected      = false;
volatile bool dumpRequested  = false;
volatile bool clearRequested = false;
volatile bool batRequested   = false;
uint32_t      lastLog        = 0;
uint32_t      bleModeStart   = 0;
bool          bleMode        = true;

// ── INDEX ────────────────────────────────────────────────────────────────────
uint32_t readIndex() {
  File f(InternalFS);
  if (!f.open("/idx.txt", FILE_O_READ)) return 0;
  char buf[16] = {};
  int  n = f.read(buf, sizeof(buf) - 1);
  f.close();
  return n > 0 ? (uint32_t)atol(buf) : 0;
}

void writeIndex(uint32_t idx) {
  InternalFS.remove("/idx.txt");
  File f(InternalFS);
  if (!f.open("/idx.txt", FILE_O_WRITE)) return;
  f.print(idx);
  f.close();
}

// ── BATTERY ──────────────────────────────────────────────────────────────────
uint8_t readBatteryPercent(float* mv_out) {
  analogReference(AR_INTERNAL);
  analogReadResolution(12);
  delay(5);
  int   raw = analogRead(A0);
  float mv  = raw * (3600.0f / 4095.0f);
  if (mv_out) *mv_out = mv;
  float pct = (mv - BATT_EMPTY_MV) / (float)(BATT_FULL_MV - BATT_EMPTY_MV) * 100.0f;
  if (pct > 100.0f) pct = 100.0f;
  if (pct < 0.0f)   pct = 0.0f;
  return (uint8_t)pct;
}

void sendBattery() {
  if (!connected) return;
  float   mv;
  uint8_t pct = readBatteryPercent(&mv);
  char    buf[32];
  int     n = snprintf(buf, sizeof(buf), "BAT:%u%% (%.0fmV)\n", pct, mv);
  if (csvChar.notifyEnabled()) csvChar.notify((uint8_t*)buf, n);
}

// ── TEMP ─────────────────────────────────────────────────────────────────────
float readTemp() {
  int32_t  raw = 0;
  uint32_t err = sd_temp_get(&raw);
  if (err != NRF_SUCCESS) {
    NRF_TEMP->TASKS_START = 1;
    uint32_t t0 = millis();
    while (!NRF_TEMP->EVENTS_DATARDY && millis() - t0 < 200);
    NRF_TEMP->EVENTS_DATARDY = 0;
    raw = (int32_t)NRF_TEMP->TEMP;
    NRF_TEMP->TASKS_STOP = 1;
  }
  return raw / 4.0f;
}

// ── LOG ──────────────────────────────────────────────────────────────────────
void logTemp() {
  float    t   = readTemp();
  uint32_t idx = readIndex() + 1;
  writeIndex(idx);

  File file(InternalFS);
  if (!file.open("/log.txt", FILE_O_WRITE)) return;
  file.seek(file.size());
  file.print(idx);
  file.print(",");
  file.println(t);
  file.close();
  lastLog = millis();
}

// ── LOW POWER ────────────────────────────────────────────────────────────────
void switchToLowPower() {
  if (bleMode) Bluefruit.Advertising.stop();
  NRF_POWER->GPREGRET2 |= 0x01;
  NVIC_SystemReset();
}

// ── DUMP ─────────────────────────────────────────────────────────────────────
void sendDump() {
  if (!connected) return;
  File file(InternalFS);
  if (!file.open("/log.txt", FILE_O_READ)) {
    csvChar.notify((uint8_t*)"NO_DATA\n", 8);
    return;
  }
  char buf[32];
  while (file.available() && connected) {
    int n = file.readBytesUntil('\n', buf, sizeof(buf) - 2);
    if (n <= 0) break;
    buf[n++] = '\n';
    buf[n]   = 0;
    if (csvChar.notifyEnabled()) csvChar.notify((uint8_t*)buf, n);
    delay(10);
  }
  file.close();
  csvChar.notify((uint8_t*)"END", 3);
}

// ── CLEAR ────────────────────────────────────────────────────────────────────
void clearLog() {
  InternalFS.remove("/log.txt");
  InternalFS.remove("/idx.txt");
  logTemp();
  if (connected) csvChar.notify((uint8_t*)"CLEARED\n", 8);
}

// ── BLE CALLBACKS ────────────────────────────────────────────────────────────
void onCmdWrite(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) {
  char cmd[20];
  if (len >= sizeof(cmd)) return;
  memcpy(cmd, data, len);
  cmd[len] = 0;
  if (strcmp(cmd, "dump")  == 0) dumpRequested  = true;
  if (strcmp(cmd, "clear") == 0) clearRequested = true;
  if (strcmp(cmd, "bat")   == 0) batRequested   = true;
}

void connectCB(uint16_t)             { connected = true;  }
void disconnectCB(uint16_t, uint8_t) { connected = false; }

// ── SETUP ────────────────────────────────────────────────────────────────────
void setup() {
  // Double-reset detection: two resets within DBL_RST_WINDOW_MS → enter BLE mode
  if (NRF_POWER->GPREGRET == DBL_RST_MAGIC) {
    NRF_POWER->GPREGRET  = 0;
    NRF_POWER->GPREGRET2 ^= 0x01;   // toggle low-power flag → force BLE mode
  } else {
    NRF_POWER->GPREGRET = DBL_RST_MAGIC;
    delay(DBL_RST_WINDOW_MS);
    NRF_POWER->GPREGRET = 0;
  }

  bleMode = !(NRF_POWER->GPREGRET2 & 0x01);

  InternalFS.begin();

  // FIX 1: Only start SoftDevice when BLE is actually needed.
  //        In low-power mode this was the biggest drain.
  if (bleMode) {
    Bluefruit.begin();
    Bluefruit.setTxPower(0);          // FIX 2: was +4 dBm; 0 dBm is enough at close range
    Bluefruit.setName("CCtecTragZeit01");

    bleModeStart = millis();

    Bluefruit.Periph.setConnectCallback(connectCB);
    Bluefruit.Periph.setDisconnectCallback(disconnectCB);

    svc.begin();

    cmdChar.setProperties(CHR_PROPS_WRITE);
    cmdChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    cmdChar.setWriteCallback(onCmdWrite);
    cmdChar.begin();

    csvChar.setProperties(CHR_PROPS_NOTIFY);
    csvChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    csvChar.begin();

    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(svc);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    // FIX 3: was (32, 244) = 20 ms min — very aggressive.
    //        (160, 480) = 100–300 ms cuts TX events by ~5×.
    Bluefruit.Advertising.setInterval(160, 480);
    Bluefruit.Advertising.start(0);
  }

  logTemp();
}

// ── LOOP ─────────────────────────────────────────────────────────────────────
void loop() {
  if (bleMode) {
    if (millis() - bleModeStart >= BLE_TIMEOUT_MS) {
      switchToLowPower();
    }

    if (dumpRequested)  { dumpRequested  = false; sendDump();  switchToLowPower(); }
    if (clearRequested) { clearRequested = false; clearLog(); }
    if (batRequested)   { batRequested   = false; sendBattery(); }

    // FIX 4: was delay(500) — busy polling.
    //        sd_app_evt_wait() halts the CPU until the next BLE or app event.
    sd_app_evt_wait();

  } else {
    // Low-power logging mode: no SoftDevice running.
    // FreeRTOS idle task issues WFI, so delay() is efficient here.
    if (millis() - lastLog >= LOG_INTERVAL_MS) {
      logTemp();
    }

    uint32_t elapsed   = millis() - lastLog;
    uint32_t remaining = (elapsed < LOG_INTERVAL_MS) ? (LOG_INTERVAL_MS - elapsed) : 0;
    if (remaining > 0) delay(remaining);
  }
}
