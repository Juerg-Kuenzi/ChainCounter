/*
 * ChainCounter.ino  -  Kettenzaehler fuer Ankerwinde (Projekt P2410-01)
 *
 * Hardware : Seeed XIAO ESP32C3 auf Interface-PCB P2410-01.401 V2.0
 * Spez.    : HW/SW Spezifikation V1.3
 *
 * Funktion:
 *   - zaehlt Umdrehungen (Impulse, entprellt) vorwaerts/rueckwaerts (Direction)
 *   - Kettenlaenge = Umdrehungen * Umrechnungsfaktor
 *   - sendet 1x/s NMEA "$ISLWND,..." per UDP-Broadcast
 *   - empfaengt NMEA "$ISLWNS,..." Kommandos per UDP (und Serial)
 *   - speichert Zaehler, Systemstarts und TotalRuntime ins EEPROM M95P08:
 *     bei Spannungsausfall (VCC_Sense), alle SAVE_INTERVAL_MIN Minuten und bei reboot
 *
 * Arduino IDE: Board "XIAO_ESP32C3", "USB CDC On Boot: Enabled".
 * Keine zusaetzlichen Libraries noetig (nur ESP32 Arduino Core von Espressif).
 */
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_timer.h>
#include <math.h>
#include <stdarg.h>

#include "config.h"
#include "crc32.h"
#include "m95p.h"
#include "recordlog.h"
#include "records.h"
#include "nmea.h"

// ============================================================================
// Globale Objekte und Zustand
// ============================================================================
static M95P      ee;
static RecordLog counterLog(ee, EE_COUNTER_BASE, EE_COUNTER_SIZE, sizeof(CounterRecord), MAGIC_COUNTER);
static RecordLog configLog (ee, EE_CONFIG_BASE,  EE_CONFIG_SIZE,  sizeof(ConfigRecord),  MAGIC_CONFIG);

static ConfigRecord       cfg;
static SemaphoreHandle_t  eeMutex;
static bool               eeOk = false;

// Zaehler-Zustand (wird vom 1-ms-Sampler und vom Stromausfall-Task benutzt)
static portMUX_TYPE       g_mux          = portMUX_INITIALIZER_UNLOCKED;
static volatile int32_t   g_revolutions  = DEFAULT_REVOLUTION_COUNTER;
static volatile uint32_t  g_systemStarts = DEFAULT_SYSTEM_STARTS;
static volatile bool      g_dirDown      = false;
static volatile uint32_t  g_pulseCount   = 0;       // nur fuer LED-Blitz
static volatile uint16_t  g_debounceMs   = DEFAULT_DEBOUNCE_MS;
static uint32_t           g_runtimeBase  = DEFAULT_TOTAL_RUNTIME;   // TotalRuntime beim Start

// Stromausfall-Erkennung
static TaskHandle_t       pfTaskHandle   = nullptr;
static volatile bool      pfArmed        = false;
static volatile bool      pfSaved        = false;   // Zaehler wurde wegen Ausfall gespeichert

// Netzwerk
static WiFiUDP            udp;
static bool               udpRunning       = false;
static bool               wifiWasConnected = false;
static uint32_t           wifiLastAttempt  = 0;
static float              g_vcc            = 0.0f;
static char               g_lastTx[128]    = "";      // zuletzt gesendeter $ISLWND-Satz
static bool               g_txMonitor      = false;   // Konsole: tx on/off

// ============================================================================
// Prototypen
// ============================================================================
static void  logf(const char *fmt, ...);
static void  sampleInputs(void *arg);
static void  onVccSenseFall();
static void  powerFailTask(void *arg);
static bool  saveCounter(bool fast);
static bool  saveConfig();
static void  loadDefaultConfig(ConfigRecord &c);
static bool  configSane(const ConfigRecord &c);
static void  setRevolutions(int32_t v);
static int32_t getRevolutions();
static uint32_t actualRuntime();
static uint32_t totalRuntime();
static bool  saveCounterLocked(const char *reason);
static void  rebootDevice();
static void  handlePowerMonitor();
static void  wifiStart();
static void  wifiRestart();
static void  wifiHandle();
static void  udpReceive();
static void  udpSend();
static float readVcc();
static void  handleLed();
static void  serialReceive();
static void  handleSerialLine(char *line);
static void  handleNmeaLine(char *line, const char *source);
static void  handleCommand(char **f, int n);
static void  factoryReset();
static void  printStatus();
static char *trim(char *s);

// ============================================================================
// Logging
// ============================================================================
static void logf(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uint32_t ms = millis();
    Serial.printf("[%6lu.%03lu] %s\n", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000), buf);
}

// ============================================================================
// Eingaenge abtasten und entprellen (alle 1 ms, esp_timer)
// ============================================================================
static void sampleInputs(void *)
{
    static bool     init = false;
    static bool     impStable = false, dirStable = false;
    static uint16_t impCnt = 0, dirCnt = 0;

    bool imp = digitalRead(PIN_IMPULSE)   == IMPULSE_ACTIVE_LEVEL;
    bool dir = digitalRead(PIN_DIRECTION) == DIRECTION_DOWN_LEVEL;

    if (!init) {                       // Startzustand uebernehmen, nicht zaehlen
        impStable = imp;
        dirStable = dir;
        g_dirDown = dir;
        init = true;
        return;
    }

    // Direction: feste Entprellung
    if (dir != dirStable) {
        if (++dirCnt >= DIRECTION_DEBOUNCE_MS) {
            dirStable = dir;
            dirCnt = 0;
            g_dirDown = dir;
        }
    } else {
        dirCnt = 0;
    }

    // Impulse: Pegel muss 'DebounceTimer' ms stabil sein. Gezaehlt wird beim Schliessen.
    if (imp != impStable) {
        if (++impCnt >= g_debounceMs) {
            impStable = imp;
            impCnt = 0;
            if (impStable) {
                portENTER_CRITICAL(&g_mux);
                g_revolutions = g_revolutions + (dirStable ? 1 : -1);   // runter: +1, hoch: -1
                portEXIT_CRITICAL(&g_mux);
                g_pulseCount = g_pulseCount + 1;
            }
        }
    } else {
        impCnt = 0;
    }
}

static void setRevolutions(int32_t v)
{
    portENTER_CRITICAL(&g_mux);
    g_revolutions = v;
    portEXIT_CRITICAL(&g_mux);
}

static int32_t getRevolutions()
{
    portENTER_CRITICAL(&g_mux);
    int32_t v = g_revolutions;
    portEXIT_CRITICAL(&g_mux);
    return v;
}

// ActualRuntime: Sekunden seit Systemstart
static uint32_t actualRuntime()
{
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

// TotalRuntime: gespeicherte Summe beim Start + Laufzeit seit Start
static uint32_t totalRuntime()
{
    return g_runtimeBase + actualRuntime();
}

// ============================================================================
// Stromausfall: ISR -> hochpriorer Task -> Record per Page Program schreiben
// ============================================================================
static void IRAM_ATTR onVccSenseFall()
{
    bool trigger;
    portENTER_CRITICAL_ISR(&g_mux);
    trigger = pfArmed;
    pfArmed = false;
    portEXIT_CRITICAL_ISR(&g_mux);
    if (!trigger) return;

    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(pfTaskHandle, &woken);
    portYIELD_FROM_ISR(woken);
}

static void powerFailTask(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t t0 = micros();
        digitalWrite(PIN_LED, LOW);                    // Strom sparen

        bool ok = false;
        if (eeOk && xSemaphoreTake(eeMutex, pdMS_TO_TICKS(12)) == pdTRUE) {
            ok = saveCounter(true);
            xSemaphoreGive(eeMutex);
        }
        uint32_t dt = micros() - t0;
        pfSaved = true;
        logf("VCC_Sense LOW: Zaehler %ld gespeichert %s (%lu us)",
             (long)getRevolutions(), ok ? "OK" : "FEHLER", (unsigned long)dt);
    }
}

// Aufruf nur mit gehaltenem eeMutex (oder vor Start der Tasks)
static bool saveCounter(bool fast)
{
    CounterRecord r;
    memset(&r, 0, sizeof(r));
    r.revolutions  = getRevolutions();
    r.systemStarts = g_systemStarts;
    r.totalRuntime = totalRuntime();
    return counterLog.append(&r, fast);
}

// Speichern aus normalem Kontext (loop): Mutex, Schreiben, naechsten Slot vorbereiten
static bool saveCounterLocked(const char *reason)
{
    if (!eeOk) { logf("FEHLER: EEPROM nicht verfuegbar (%s)", reason); return false; }
    xSemaphoreTake(eeMutex, portMAX_DELAY);
    bool ok = saveCounter(false);
    xSemaphoreGive(eeMutex);
    xSemaphoreTake(eeMutex, portMAX_DELAY);       // Mutex zwischendurch freigeben
    counterLog.prepareNext();
    xSemaphoreGive(eeMutex);
    logf("%s: Zaehler %ld, Starts %lu, TotalRuntime %lu s gespeichert %s (Slot %ld)", reason,
         (long)getRevolutions(), (unsigned long)g_systemStarts, (unsigned long)totalRuntime(),
         ok ? "OK" : "FEHLER", (long)counterLog.lastSlot());
    return ok;
}

// reboot: variable Daten sichern, dann Neustart
static void rebootDevice()
{
    portENTER_CRITICAL(&g_mux);
    pfArmed = false;                              // keine Ausfall-Speicherung waehrenddessen
    portEXIT_CRITICAL(&g_mux);
    saveCounterLocked("reboot");
    logf("reboot: Neustart ...");
    Serial.flush();
    delay(200);
    ESP.restart();
}

// Schaltet die Ausfallerkennung scharf, sobald VCC_Sense stabil High ist.
static void handlePowerMonitor()
{
    static uint32_t okSince = 0;
    static bool     warned  = false;
    static bool     armedOnce = false;

    if (pfArmed || !eeOk) return;

    bool vccOk = digitalRead(PIN_VCC_SENSE) == VCC_SENSE_OK_LEVEL;
    if (!vccOk) {
        okSince = 0;
        if (!warned && !armedOnce && millis() > 3000) {
            logf("WARNUNG: VCC_Sense LOW - Ausfallerkennung inaktiv (nur USB-Speisung?)");
            warned = true;
        }
        return;
    }
    if (okSince == 0) okSince = millis() | 1;
    if (millis() - okSince < VCC_SENSE_ARM_DELAY_MS) return;

    // Naechsten Slot vorbereiten (Speisung ist gut)
    if (!counterLog.nextReady()) {
        xSemaphoreTake(eeMutex, portMAX_DELAY);
        if (!counterLog.prepareNext()) logf("FEHLER: EEPROM Slot-Vorbereitung fehlgeschlagen");
        xSemaphoreGive(eeMutex);
    }
    if (pfSaved) {
        logf("Speisung wieder OK");
        pfSaved = false;
    }

    portENTER_CRITICAL(&g_mux);
    pfArmed = true;
    portEXIT_CRITICAL(&g_mux);
    if (!armedOnce) {
        logf("Ausfallerkennung aktiv");
        armedOnce = true;
    }

    // Falls die Speisung genau jetzt ausgefallen ist (Flanke verpasst):
    if (digitalRead(PIN_VCC_SENSE) != VCC_SENSE_OK_LEVEL) {
        bool trigger;
        portENTER_CRITICAL(&g_mux);
        trigger = pfArmed;
        pfArmed = false;
        portEXIT_CRITICAL(&g_mux);
        if (trigger) xTaskNotifyGive(pfTaskHandle);
    }
}

// ============================================================================
// Konfiguration
// ============================================================================
static void loadDefaultConfig(ConfigRecord &c)
{
    memset(&c, 0, sizeof(c));
    c.kFactor    = DEFAULT_K_FACTOR_M;
    c.debounceMs = DEFAULT_DEBOUNCE_MS;
    c.udpPort    = DEFAULT_UDP_PORT;
    strlcpy(c.ssid,     DEFAULT_WLAN_SSID,     sizeof(c.ssid));
    strlcpy(c.password, DEFAULT_WLAN_PASSWORD, sizeof(c.password));
}

static bool configSane(const ConfigRecord &c)
{
    return isfinite(c.kFactor) && c.kFactor >= K_FACTOR_MIN && c.kFactor <= K_FACTOR_MAX
        && c.debounceMs >= DEBOUNCE_MS_MIN && c.debounceMs <= DEBOUNCE_MS_MAX
        && c.udpPort != 0
        && memchr(c.ssid, 0, sizeof(c.ssid)) != nullptr && c.ssid[0] != 0
        && memchr(c.password, 0, sizeof(c.password)) != nullptr;
}

static bool saveConfig()
{
    if (!eeOk) { logf("FEHLER: EEPROM nicht verfuegbar, Konfiguration nicht gespeichert"); return false; }
    xSemaphoreTake(eeMutex, portMAX_DELAY);
    bool ok = configLog.append(&cfg);
    xSemaphoreGive(eeMutex);

    xSemaphoreTake(eeMutex, portMAX_DELAY);       // Mutex zwischendurch freigeben
    configLog.prepareNext();
    xSemaphoreGive(eeMutex);

    logf("Konfiguration gespeichert %s (Slot %ld, Seq %lu)", ok ? "OK" : "FEHLER",
         (long)configLog.lastSlot(), (unsigned long)configLog.lastSeq());
    return ok;
}

// ============================================================================
// Factory Reset
// ============================================================================
static void factoryReset()
{
    logf("Factory Reset: EEPROM wird geloescht ...");
    portENTER_CRITICAL(&g_mux);
    pfArmed = false;
    portEXIT_CRITICAL(&g_mux);

    bool ok = false;
    if (eeOk) {
        xSemaphoreTake(eeMutex, portMAX_DELAY);
        ok  = counterLog.eraseAll();
        ok &= configLog.eraseAll();
        xSemaphoreGive(eeMutex);
    }
    logf("Factory Reset: Loeschen %s - Neustart, Initialwerte werden geschrieben", ok ? "OK" : "FEHLER");
    Serial.flush();
    delay(200);
    ESP.restart();
}

// ============================================================================
// WLAN
// ============================================================================
static void wifiStart()
{
    WiFi.persistent(false);
    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);              // schnelle UDP-Reaktion
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.ssid, cfg.password);
    wifiLastAttempt = millis();
    logf("WLAN: verbinde mit '%s' ...", cfg.ssid);
}

static void wifiRestart()
{
    if (udpRunning) { udp.stop(); udpRunning = false; }
    WiFi.disconnect();
    wifiWasConnected = false;
    WiFi.begin(cfg.ssid, cfg.password);
    wifiLastAttempt = millis();
    logf("WLAN: neu verbinden mit '%s' ...", cfg.ssid);
}

static void wifiHandle()
{
    bool conn = (WiFi.status() == WL_CONNECTED);

    if (conn && !wifiWasConnected) {
        logf("WLAN verbunden: IP %s, Broadcast %s, RSSI %d dBm",
             WiFi.localIP().toString().c_str(), WiFi.broadcastIP().toString().c_str(), WiFi.RSSI());
        udpRunning = udp.begin(cfg.udpPort);
        logf("UDP Port %u %s", cfg.udpPort, udpRunning ? "offen" : "FEHLER beim Oeffnen");
    }
    if (!conn && wifiWasConnected) {
        logf("WLAN Verbindung verloren");
        if (udpRunning) { udp.stop(); udpRunning = false; }
        wifiLastAttempt = millis();
    }
    wifiWasConnected = conn;

    if (!conn && millis() - wifiLastAttempt > WIFI_RECONNECT_INTERVAL_MS) {
        logf("WLAN: neuer Verbindungsversuch (Status %d)", (int)WiFi.status());
        WiFi.disconnect();
        WiFi.begin(cfg.ssid, cfg.password);
        wifiLastAttempt = millis();
    }
}

// ============================================================================
// UDP Rx / Tx
// ============================================================================
static void udpReceive()
{
    if (!udpRunning) return;
    for (int i = 0; i < 4; i++) {                 // max. 4 Pakete pro Durchlauf
        int size = udp.parsePacket();
        if (size <= 0) return;

        char buf[256];
        int n = udp.read((uint8_t *)buf, sizeof(buf) - 1);
        if (n < 0) n = 0;
        buf[n] = '\0';

        IPAddress rip = udp.remoteIP();
        if (rip == WiFi.localIP()) continue;       // eigener Broadcast
        char src[32];
        snprintf(src, sizeof(src), "UDP %s", rip.toString().c_str());

        if (size > n) {
            logf("FEHLER %s: Paket zu lang (%d Byte)", src, size);
            continue;
        }
        // Ein Paket kann mehrere Saetze enthalten
        char *save = nullptr;
        for (char *line = strtok_r(buf, "\r\n", &save); line; line = strtok_r(nullptr, "\r\n", &save)) {
            handleNmeaLine(line, src);
        }
    }
}

static void udpSend()
{
    char s[128];
    float chainLength = (float)getRevolutions() * cfg.kFactor;
    int   chainDir    = g_dirDown ? 0 : 1;                 // 0 = down, 1 = up

    snprintf(s, sizeof(s), "$ISLWND,%.1f,%d,%.1f,%lu,%lu,%lu",
             chainLength, chainDir, g_vcc, (unsigned long)g_systemStarts,
             (unsigned long)totalRuntime(), (unsigned long)actualRuntime());
    nmeaAppendChecksum(s, sizeof(s));

    if (udpRunning) {
        udp.beginPacket(WiFi.broadcastIP(), cfg.udpPort);
        udp.write((const uint8_t *)s, strlen(s));
        udp.endPacket();
    }
    strlcpy(g_lastTx, s, sizeof(g_lastTx));
    if (g_txMonitor) Serial.printf("TX%s %s", udpRunning ? "" : " (nicht gesendet, kein WLAN)", s);
}

// ============================================================================
// Spannungsmessung VCC_Analogue
// ============================================================================
static float readVcc()
{
    uint32_t sum = 0;
    for (int i = 0; i < VCC_ADC_SAMPLES; i++) sum += analogReadMilliVolts(PIN_VCC_ANALOGUE);
    return (float)sum / VCC_ADC_SAMPLES / 1000.0f * VCC_DIVIDER_RATIO * VCC_CALIBRATION;
}

// ============================================================================
// LED: an = WLAN verbunden, blinkt = verbindet, kurzer Blitz = Impuls
// ============================================================================
static void handleLed()
{
    static uint32_t lastPulses = 0, flashUntil = 0;
    uint32_t now = millis();
    uint32_t p   = g_pulseCount;
    if (p != lastPulses) { lastPulses = p; flashUntil = now + 60; }

    bool on = wifiWasConnected ? true : ((now / 250) & 1);
    if ((int32_t)(flashUntil - now) > 0) on = !on;
    if (pfSaved) on = false;
    digitalWrite(PIN_LED, on ? HIGH : LOW);
}

// ============================================================================
// NMEA Kommandos  $ISLWNS,p1[,p2]*xx
// ============================================================================
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return s;
}

static void handleNmeaLine(char *line, const char *source)
{
    line = trim(line);
    if (!*line) return;
    logf("RX %s: %s", source, line);

    char *f[NMEA_MAX_FIELDS];
    int n;
    const char *err;
    if (!nmeaParse(line, f, NMEA_MAX_FIELDS, &n, &err)) {
        logf("FEHLER NMEA: %s", err);
        return;
    }
    if (strcmp(f[0], "ISLWNS") != 0) {
        logf("  ignoriert (Satz '%s')", f[0]);
        return;
    }
    handleCommand(f, n);
}

static void handleCommand(char **f, int n)
{
    // f[0] = "ISLWNS", f[1] = p1, f[2] = p2 ...
    if (n < 2 || f[1][0] == '\0') { logf("FEHLER ISLWNS: Parameter p1 fehlt"); return; }
    if (strlen(f[1]) != 1)         { logf("FEHLER ISLWNS: ungueltiger Parameter p1 '%s'", f[1]); return; }

    const char cmd = f[1][0];
    switch (cmd) {

    case 'C': {                                   // Chain counter setzen
        if (n == 2) {
            setRevolutions(0);
            logf("C: Kettenzaehler = 0");
        } else if (n == 3) {
            long meter;
            if (!parseIntStrict(f[2], &meter)) { logf("FEHLER C: Parameter '%s' ist kein %%d", f[2]); return; }
            double revs = round((double)meter / (double)cfg.kFactor);
            if (revs > INT32_MAX || revs < INT32_MIN) { logf("FEHLER C: Wert %ld ausserhalb Bereich", meter); return; }
            setRevolutions((int32_t)revs);
            logf("C: Kettenlaenge %ld m -> %ld Umdrehungen", meter, (long)revs);
        } else {
            logf("FEHLER C: zu viele Parameter");
        }
        break;
    }

    case 'S': {                                   // Debounce Timer
        if (n < 3)  { logf("FEHLER S: Parameter fehlt"); return; }
        if (n > 3)  { logf("FEHLER S: zu viele Parameter"); return; }
        long ms;
        if (!parseIntStrict(f[2], &ms)) { logf("FEHLER S: Parameter '%s' ist kein %%d", f[2]); return; }
        if (ms < DEBOUNCE_MS_MIN || ms > DEBOUNCE_MS_MAX) {
            logf("FEHLER S: %ld ms ausserhalb %d..%d", ms, DEBOUNCE_MS_MIN, DEBOUNCE_MS_MAX);
            return;
        }
        cfg.debounceMs = (uint16_t)ms;
        g_debounceMs   = (uint16_t)ms;
        logf("S: DebounceTimer = %ld ms", ms);
        saveConfig();
        break;
    }

    case 'K': {                                   // Umrechnungsfaktor
        if (n < 3)  { logf("FEHLER K: Parameter fehlt"); return; }
        if (n > 3)  { logf("FEHLER K: zu viele Parameter"); return; }
        float k;
        if (!parseFloatStrict(f[2], &k)) { logf("FEHLER K: Parameter '%s' ist kein %%f", f[2]); return; }
        if (k < K_FACTOR_MIN || k > K_FACTOR_MAX) {
            logf("FEHLER K: %s ausserhalb %.3f..%.1f", f[2], K_FACTOR_MIN, K_FACTOR_MAX);
            return;
        }
        cfg.kFactor = k;
        logf("K: Umrechnungsfaktor = %.4f m/Umdrehung", k);
        saveConfig();
        break;
    }

    case 'F': {                                   // Factory Reset
        if (n > 2) { logf("FEHLER F: zu viele Parameter"); return; }
        factoryReset();
        break;
    }

    default:
        logf("FEHLER ISLWNS: unbekanntes Kommando '%c'", cmd);
        break;
    }
}

// ============================================================================
// Serielle Konsole (115200 Baud)
//   $ISLWNS,...*xx    gleiches Kommando wie per UDP
//   Weitere Befehle: help, status, save, cs <satz>, ssid=..., pass=..., port=...
// ============================================================================
static void serialReceive()
{
    static char   line[160];
    static size_t len = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (len) { line[len] = '\0'; handleSerialLine(line); len = 0; }
        } else if (len < sizeof(line) - 1) {
            line[len++] = c;
        } else {
            logf("FEHLER Serial: Zeile zu lang");
            len = 0;
        }
    }
}

static void handleSerialLine(char *line)
{
    line = trim(line);
    if (line[0] == '$') { handleNmeaLine(line, "Serial"); return; }

    if (strcmp(line, "help") == 0) {
        Serial.println(F("Befehle:\n"
                         "  $ISLWNS,...*xx  NMEA Kommando (C, S, K, F)\n"
                         "  cs <satz>       Checksumme berechnen, z.B. cs $ISLWNS,K,0.42\n"
                         "  status          Zustand anzeigen\n"
                         "  save            variable Daten jetzt ins EEPROM schreiben\n"
                         "  reboot          variable Daten speichern, dann Neustart\n"
                         "  tx              zuletzt gesendeten $ISLWND-Satz anzeigen\n"
                         "  tx on | tx off  jeden gesendeten Satz laufend anzeigen\n"
                         "  ssid=<name>     WLAN SSID setzen (gespeichert)\n"
                         "  pass=<passwort> WLAN Passwort setzen (gespeichert)\n"
                         "  port=<n>        UDP Port setzen (gespeichert)"));
    } else if (strcmp(line, "status") == 0) {
        printStatus();
    } else if (strcmp(line, "save") == 0) {
        saveCounterLocked("save");
    } else if (strcmp(line, "reboot") == 0) {
        rebootDevice();
    } else if (strcmp(line, "tx") == 0) {
        if (g_lastTx[0]) Serial.print(g_lastTx);
        else logf("tx: noch kein Satz erzeugt");
    } else if (strcmp(line, "tx on") == 0) {
        g_txMonitor = true;
        logf("tx: laufende Anzeige ein");
    } else if (strcmp(line, "tx off") == 0) {
        g_txMonitor = false;
        logf("tx: laufende Anzeige aus");
    } else if (strncmp(line, "cs ", 3) == 0) {
        char s[140];
        strlcpy(s, trim(line + 3), sizeof(s));
        char *star = strchr(s, '*');
        if (star) *star = '\0';
        if (nmeaAppendChecksum(s, sizeof(s))) Serial.print(s);
        else logf("FEHLER: Satz muss mit '$' beginnen");
    } else if (strncmp(line, "ssid=", 5) == 0) {
        size_t l = strlen(line + 5);
        if (l < 1 || l > 32) { logf("FEHLER: SSID muss 1..32 Zeichen haben"); return; }
        strlcpy(cfg.ssid, line + 5, sizeof(cfg.ssid));
        saveConfig();
        wifiRestart();
    } else if (strncmp(line, "pass=", 5) == 0) {
        size_t l = strlen(line + 5);
        if (l > 64 || (l > 0 && l < 8)) { logf("FEHLER: Passwort muss leer oder 8..64 Zeichen sein"); return; }
        strlcpy(cfg.password, line + 5, sizeof(cfg.password));
        saveConfig();
        wifiRestart();
    } else if (strncmp(line, "port=", 5) == 0) {
        long p;
        if (!parseIntStrict(line + 5, &p) || p < 1 || p > 65535) { logf("FEHLER: Port 1..65535"); return; }
        cfg.udpPort = (uint16_t)p;
        saveConfig();
        if (udpRunning) { udp.stop(); udpRunning = udp.begin(cfg.udpPort); }
        logf("UDP Port = %u", cfg.udpPort);
    } else if (line[0]) {
        logf("FEHLER Serial: unbekannter Befehl '%s' (help)", line);
    }
}

static void printStatus()
{
    int32_t rev = getRevolutions();
    Serial.printf("--- ChainCounter %s (Spez. V%s) ---\n", FW_VERSION, SPEC_VERSION);
    Serial.printf("Umdrehungen     : %ld\n", (long)rev);
    Serial.printf("Kettenlaenge    : %.2f m\n", rev * cfg.kFactor);
    Serial.printf("Richtung        : %s\n", g_dirDown ? "down (0)" : "up (1)");
    Serial.printf("VCC             : %.2f V\n", g_vcc);
    Serial.printf("VCC_Sense       : %s, Ausfallerkennung %s\n",
                  digitalRead(PIN_VCC_SENSE) == VCC_SENSE_OK_LEVEL ? "OK" : "LOW",
                  pfArmed ? "aktiv" : "inaktiv");
    Serial.printf("Systemstarts    : %lu\n", (unsigned long)g_systemStarts);
    Serial.printf("ActualRuntime   : %lu s\n", (unsigned long)actualRuntime());
    Serial.printf("TotalRuntime    : %lu s\n", (unsigned long)totalRuntime());
    Serial.printf("K-Faktor        : %.4f m/U\n", cfg.kFactor);
    Serial.printf("DebounceTimer   : %u ms\n", cfg.debounceMs);
    Serial.printf("UDP Port        : %u\n", cfg.udpPort);
    Serial.printf("WLAN SSID       : %s\n", cfg.ssid);
    Serial.printf("WLAN            : %s %s\n", wifiWasConnected ? "verbunden" : "getrennt",
                  wifiWasConnected ? WiFi.localIP().toString().c_str() : "");
    Serial.printf("EEPROM          : %s\n", eeOk ? "OK" : "FEHLER");
    Serial.printf("Counter-Record  : Slot %ld / %lu, Seq %lu\n", (long)counterLog.lastSlot(),
                  (unsigned long)counterLog.slotCount(), (unsigned long)counterLog.lastSeq());
    Serial.printf("Config-Record   : Slot %ld / %lu, Seq %lu\n", (long)configLog.lastSlot(),
                  (unsigned long)configLog.slotCount(), (unsigned long)configLog.lastSeq());
}

// ============================================================================
// setup / loop
// ============================================================================
void setup()
{
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    pinMode(PIN_VCC_SENSE, INPUT);
    pinMode(PIN_DIRECTION, INPUT);
    pinMode(PIN_IMPULSE,   INPUT);

    Serial.begin(SERIAL_BAUD);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 1500) delay(10);   // kurz auf USB-CDC warten

    logf("ChainCounter %s (Spez. V%s) gestartet", FW_VERSION, SPEC_VERSION);
    eeMutex = xSemaphoreCreateMutex();

    // ---- EEPROM ----
    eeOk = ee.begin();
    logf("EEPROM M95P08: JEDEC ID %06lX, Status %02X -> %s",
         (unsigned long)ee.readJedecId(), ee.readStatus(), eeOk ? "OK" : "FEHLER (nicht gefunden)");

    loadDefaultConfig(cfg);
    CounterRecord cr;

    if (eeOk) {
        uint32_t ts = millis();
        // Konfiguration
        ConfigRecord tmp;
        if (configLog.scan(&tmp) && configSane(tmp)) {
            cfg = tmp;
            logf("Konfiguration geladen (Slot %ld, Seq %lu)", (long)configLog.lastSlot(),
                 (unsigned long)configLog.lastSeq());
        } else {
            logf("Keine gueltige Konfiguration im EEPROM -> Defaultwerte werden geschrieben");
            loadDefaultConfig(cfg);
            configLog.prepareNext();
            if (!configLog.append(&cfg)) logf("FEHLER: Konfiguration schreiben");
        }
        configLog.prepareNext();

        // Zaehler
        if (counterLog.scan(&cr)) {
            g_revolutions  = cr.revolutions;
            g_systemStarts = cr.systemStarts;
            g_runtimeBase  = cr.totalRuntime;
            logf("Zaehler geladen (Slot %ld, Seq %lu): Umdrehungen %ld, Systemstarts %lu, TotalRuntime %lu s",
                 (long)counterLog.lastSlot(), (unsigned long)counterLog.lastSeq(),
                 (long)cr.revolutions, (unsigned long)cr.systemStarts, (unsigned long)cr.totalRuntime);
        } else {
            g_revolutions  = DEFAULT_REVOLUTION_COUNTER;
            g_systemStarts = DEFAULT_SYSTEM_STARTS;
            g_runtimeBase  = DEFAULT_TOTAL_RUNTIME;
            logf("Kein gueltiger Zaehler im EEPROM -> Defaultwerte");
        }
        logf("EEPROM Scan: %lu ms", (unsigned long)(millis() - ts));
    } else {
        logf("WARNUNG: ohne EEPROM wird nichts gespeichert, Defaultwerte aktiv");
    }

    // Systemstart zaehlen und sofort sichern, naechsten Slot vorbereiten
    g_systemStarts = g_systemStarts + 1;
    if (eeOk) {
        counterLog.prepareNext();
        if (!saveCounter(false)) logf("FEHLER: Start-Record schreiben");
        counterLog.prepareNext();
    }
    logf("Systemstart Nr. %lu, K=%.4f m/U, Debounce=%u ms, Port=%u",
         (unsigned long)g_systemStarts, cfg.kFactor, cfg.debounceMs, cfg.udpPort);

    // ---- Eingaenge abtasten (1 ms) ----
    g_debounceMs = cfg.debounceMs;
    esp_timer_create_args_t targs = {};
    targs.callback        = &sampleInputs;
    targs.arg             = nullptr;
    targs.dispatch_method = ESP_TIMER_TASK;
    targs.name            = "sample";
    esp_timer_handle_t sampleTimer;
    esp_timer_create(&targs, &sampleTimer);
    esp_timer_start_periodic(sampleTimer, 1000);

    // ---- Stromausfall-Erkennung ----
    xTaskCreate(powerFailTask, "pfail", 4096, nullptr, configMAX_PRIORITIES - 1, &pfTaskHandle);
    attachInterrupt(digitalPinToInterrupt(PIN_VCC_SENSE), onVccSenseFall,
                    VCC_SENSE_OK_LEVEL == HIGH ? FALLING : RISING);

    // ---- WLAN ----
    g_vcc = readVcc();
    wifiStart();
}

void loop()
{
    static uint32_t lastTx   = 0;
    static uint32_t lastSave = 0;

    handlePowerMonitor();
    wifiHandle();
    udpReceive();
    serialReceive();
    handleLed();

    uint32_t now = millis();
    if (now - lastTx >= UDP_TX_INTERVAL_MS) {
        lastTx = now;
        g_vcc = readVcc();
        udpSend();
    }
    if (now - lastSave >= (uint32_t)SAVE_INTERVAL_MIN * 60000UL) {
        lastSave = now;
        if (eeOk) saveCounterLocked("Periodische Speicherung");
    }
    delay(1);
}
