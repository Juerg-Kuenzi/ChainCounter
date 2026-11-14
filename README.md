# ChainCounter

Kettenzähler für eine Ankerwinde. Er zählt die Umdrehungen des Kettenrads, rechnet sie in die Kettenlänge um und sendet die Werte einmal pro Sekunde als NMEA0183-Satz per UDP-Broadcast an das Navigationssystem.

- **Hardware:** Seeed XIAO ESP32C3 auf Interface-PCB P2410-01.401 V2.0
- **Spezifikation:** [HW/SW Spezifikation V1.8](docs/ChainCounterSpecification_V1.8.odt) (Firmware 1.3.0)
- **Firmware:** `firmware/ChainCounter/` (Arduino-Sketch)

## Inhalt

```
firmware/ChainCounter/
  ChainCounter.ino     Hauptprogramm: Zählung, Stromausfall, WLAN, UDP, Kommandos, Konsole
  config.h             Defaultwerte, Pins, Polaritäten, EEPROM-Layout, Grenzwerte
  secrets.h.example    Vorlage für die WLAN-Zugangsdaten  ->  nach secrets.h kopieren
  m95p.*               SPI-Treiber für das EEPROM M95P08
  recordlog.*          Wear-Leveling-Ringpuffer mit CRC32
  records.h            Aufbau der EEPROM-Records
  crc32.*              CRC32 gemäss Spezifikation (Tabelle, Startwert 0x4711e2c7)
  nmea.*               NMEA0183: Checksumme, Parser, strikte Zahlenprüfung
tests/host/            PC-Tests (g++) mit simuliertem EEPROM
```

## Entwicklungsumgebung (Arduino IDE)

1. **Arduino IDE 2.x** installieren (Windows oder Linux).
2. Unter *Datei → Einstellungen → Zusätzliche Boardverwalter-URLs* eintragen:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. In der *Boardverwaltung* **„esp32“ by Espressif Systems** installieren, Version **3.0.x oder neuer** (getestet mit 3.0.7).
4. Board **„XIAO_ESP32C3“** wählen. *Tools → USB CDC On Boot* muss auf **Enabled** stehen (das ist der Default), sonst erscheint die Konsole nicht am USB-C-Anschluss.
5. `firmware/ChainCounter/secrets.h.example` nach `secrets.h` kopieren und SSID/Passwort eintragen. Die Datei steht in `.gitignore` und landet nicht im Repo.
6. `firmware/ChainCounter/ChainCounter.ino` öffnen, hochladen und den seriellen Monitor mit **115200 Baud** öffnen.

**Zusätzliche Libraries: keine.** Gebraucht werden nur `WiFi`, `WiFiUdp` und `SPI`, die im ESP32-Core enthalten sind.

> Der XIAO ESP32C3 braucht die mitgelieferte **externe Antenne** (U.FL), sonst ist die WLAN-Verbindung sehr schwach.

## Pinbelegung

| GPIO | XIAO | Signal | Richtung | Bedeutung |
|---|---|---|---|---|
| 2  | A0/D0 | VCC_Analogue | Analog In | Vin über Teiler 82k/8k2 (1/11) |
| 3  | D1  | LED | Out | High = an |
| 4  | D2  | – | – | nicht angeschlossen |
| 5  | D3  | VCC_Sense | In | Low = Speisung fällt aus (Interrupt) |
| 6  | D4  | Direction | In | High = Kette runter (+1), Low = hoch (−1) |
| 7  | D5  | Impulse | In | High = Reedkontakt geschlossen |
| 21 | D6  | !WP1 | Out | EEPROM Write Protect, Low aktiv |
| 20 | D7  | !CS1 | Out | EEPROM Chip Select, Low aktiv |
| 8  | D8  | SCK | Out | SPI Clock |
| 9  | D9  | MISO | **In** | SPI, Q des EEPROM |
| 10 | D10 | MOSI | Out | SPI |

Die Polaritäten lassen sich in `config.h` umstellen (`DIRECTION_DOWN_LEVEL`, `IMPULSE_ACTIVE_LEVEL`, `VCC_SENSE_OK_LEVEL`).

## Funktion

### Zählung
- Die Eingänge werden jede Millisekunde abgetastet (`esp_timer`).
- **Impulse:** Ein Pegelwechsel gilt erst, wenn der Pegel *DebounceTimer* ms lang stabil ist (Default 10 ms). Gezählt wird beim Schliessen des Reedkontakts.
- **Direction:** Feste Entprellung von 20 ms. Kette runter zählt +1, Kette hoch zählt −1.
- Der Zähler zählt Umdrehungen (`int32`). Kettenlänge = Umdrehungen × Umrechnungsfaktor.

### UDP Tx (1× pro Sekunde, Broadcast auf UDP_Port)
```
$PXISWND,<Chainlength>,<ChainDirection>,<VCCVoltage>,<SystemStarts>,<TotalRuntime>,<ActualRuntime>*XX
```
| Feld | Format | Bedeutung |
|---|---|---|
| Chainlength | %.1f | Kettenlänge in m (Umdrehungen × Umrechnungsfaktor) |
| ChainDirection | %d | 0 = down, 1 = up |
| VCCVoltage | %.1f | VCC_Analogue in V |
| SystemStarts | %d | Anzahl Systemstarts |
| TotalRuntime | %d (u32) | Laufzeit in s seit Inbetriebnahme/Factory Reset: gespeicherte Summe + Laufzeit seit Start |
| ActualRuntime | %d (u32) | Laufzeit in s seit dem aktuellen Start |

Beispiel: `$PXISWND,12.4,0,24.1,17,86400,3605*43`

Der Satz ist ein herstellereigener NMEA-0183-Satz: `P` + Kürzel `XIS` (X = Eigenbau, kein von der NMEA vergebenes Herstellerkürzel) + Satztyp `WND`. Bis Firmware 1.2.1 hieß er `$ISLWND`.

### UDP Rx (Kommandos `$PXISWNS,p1[,p2]*xx`)

| Kommando | Wirkung | Speicherung |
|---|---|---|
| `$PXISWNS,C*xx` | Kettenzähler = 0 | beim nächsten Stromausfall bzw. bei der nächsten periodischen Speicherung |
| `$PXISWNS,C,<m>*xx` | Kettenlänge setzen (%d, Meter → Umdrehungen) | beim nächsten Stromausfall bzw. bei der nächsten periodischen Speicherung |
| `$PXISWNS,S,<ms>*xx` | DebounceTimer (1…1000 ms) | sofort |
| `$PXISWNS,K,<m/U>*xx` | Umrechnungsfaktor (0.001…100) | sofort |
| `$PXISWNS,R*xx` | Neustart: variable Daten speichern, dann Neustart, wie `reboot` an der Konsole | sofort |
| `$PXISWNS,F*xx` | Factory Reset: EEPROM löschen, Neustart, Initialwerte schreiben | – |

**Übergangsphase:** Kommandos in der alten Form `$ISLWNS,…` werden weiterhin angenommen, damit App und Display auch vor ihrem Update funktionieren. Wenn alle Geräte aktualisiert sind, wird das entfernt (`NMEA_CMD_ADDR_LEGACY` in `config.h`).

Jeder empfangene Satz wird geloggt. Fehler werden auf der seriellen Schnittstelle protokolliert: falsche Checksumme, fehlender Parameter, falsches Format, zu viele Parameter, Wertebereich. Sätze mit anderem Adressfeld werden geloggt und ignoriert, eigene Broadcasts werden verworfen.

### Serielle Konsole (USB-C, 115200 Baud, 8N1)
Eingaben sind ASCII-Zeilen mit Abschluss CR, LF oder CR+LF, höchstens 159 Zeichen. Leerzeichen am Zeilenanfang und -ende werden entfernt. Die Befehle sind **case sensitive** (klein geschrieben). Beginnt eine Zeile mit `$`, wird sie als NMEA-Kommando behandelt (mit Checksumme), alle anderen Zeilen sind Konsolenbefehle ohne Checksumme. Log-Ausgaben beginnen mit einem Zeitstempel in Sekunden seit dem Start, z. B. `[    12.345]`.

Diese Liste ist als Vorlage für die Befehlsliste in Spezifikation V1.4 gedacht:

| Befehl | Parameter | Funktion | Speicherung |
|---|---|---|---|
| `$PXISWNS,...*xx` | wie UDP | NMEA-Kommando C, S, K, R, F, gleiche Verarbeitung wie per UDP | wie UDP |
| `help` | – | Befehlsliste anzeigen | – |
| `status` | – | Zustand anzeigen: Zähler, Länge, Richtung, VCC, Starts, Laufzeiten, Konfiguration, WLAN, EEPROM | – |
| `save` | – | variable Daten (Zähler, Systemstarts, TotalRuntime) sofort ins EEPROM schreiben | sofort |
| `reboot` | – | variable Daten speichern, dann Neustart | sofort |
| `tx` | – | zuletzt erzeugten `$PXISWND`-Satz einmal anzeigen | – |
| `tx on` / `tx off` | – | jeden erzeugten `$PXISWND`-Satz laufend anzeigen (1× pro Sekunde) bzw. ausschalten; nach einem Neustart aus | – |
| `cs <satz>` | NMEA-Satz ab `$` | Checksumme anhängen und anzeigen (nicht ausführen); vorhandenes `*xx` wird ersetzt | – |
| `ssid=<name>` | 1…32 Zeichen | WLAN-SSID setzen, neu verbinden | sofort |
| `pass=<pw>` | leer oder 8…64 Zeichen | WLAN-Passwort setzen, neu verbinden | sofort |
| `port=<n>` | 1…65535 | UDP-Port (Rx und Tx) setzen, Port neu öffnen | sofort |

Ein unbekannter Befehl erzeugt `FEHLER Serial: unbekannter Befehl '<eingabe>' (help)`.

SSID und Passwort lassen sich bewusst nur über die Konsole ändern, nicht per UDP. So kann niemand im Netz die Zugangsdaten ändern.

Beispiele:
```
tx
$PXISWND,12.4,0,24.1,17,86400,3605*43
save
[    85.210] save: Zaehler 31, Starts 17, TotalRuntime 86400 s gespeichert OK (Slot 40)
reboot
[    90.002] reboot: Zaehler 31, Starts 17, TotalRuntime 86405 s gespeichert OK (Slot 41)
[    90.003] reboot: Neustart ...
```

### LED
Dauernd an: WLAN verbunden. Blinkt mit 2 Hz: Verbindungsaufbau. Kurzer Blitz: Impuls gezählt.

### WLAN
DHCP, `setAutoReconnect(true)`. Fehlt die Verbindung länger als 15 s, wird `WiFi.begin()` neu aufgerufen. Danach wird der UDP-Socket neu geöffnet.

## EEPROM M95P08 und Stromausfall

### Layout
| Bereich | Adresse | Grösse | Recordgrösse | Slots |
|---|---|---|---|---|
| Zähler (Umdrehungen, Systemstarts, TotalRuntime) | 0x000000 | 64 KB | 32 Byte | 2048 |
| Konfiguration (K, Debounce, Port, SSID, Passwort) | 0x010000 | 4 KB | 128 Byte | 32 |

Jeder Record ist so aufgebaut: `magic | seq | Daten | crc32`. Die CRC wird über alle Bytes ausser der CRC selbst gebildet, mit Startwert `0x4711e2c7`, der Tabelle aus der Spezifikation und ohne abschliessendes XOR. Beim Lesen wird sie immer geprüft.

### Wear-Leveling
Neue Records werden immer in den **nächsten** Slot geschrieben. Ist der Bereich voll, beginnt das Schreiben wieder vorne. Beim Start wird der ganze Bereich gelesen (64 KB, ca. 60 ms) und der gültige Record mit der höchsten Sequenznummer verwendet. Bei 2048 Slots und 500 000 Zyklen pro Zelle reicht das für weit über eine Million Power-Cycles.

### Vorbereiteter Record
Der nächste Record wird beim Start und nach jedem Schreiben vorbereitet. Solange die Speisung gut ist, solange die Speisung gut ist, prüft die Firmware dazu den nächsten Slot. Liegt er am Anfang einer neuen Page, wird diese Page bei Bedarf gelöscht (Page Erase, max. 4,5 ms). Bei einem Stromausfall braucht es dann weder Suche noch Löschen, sondern nur ein **Page Program** von 32 Byte (typisch etwa 0,1–0,2 ms). Damit bleibt viel Reserve zu den garantierten 20 ms.

Ablauf beim Ausfall: Flanke an VCC_Sense → ISR → FreeRTOS-Task mit höchster Priorität → Record schreiben und zurücklesen → loggen. Die Ausfallerkennung wird erst scharf geschaltet, wenn VCC_Sense 200 ms lang High war. Kommt die Speisung ohne Reset zurück, wird der nächste Slot vorbereitet und die Erkennung wieder aktiviert.

Die variablen Daten (Umdrehungen, Systemstarts, TotalRuntime) werden geschrieben:
- bei jedem Start (`Systemstarts` wird dabei erhöht),
- alle `SAVE_INTERVAL_MIN` Minuten (Default 15, in `config.h`),
- bei `reboot` und `save`,
- bei Spannungsausfall.

Bei einem Reset ohne Vorwarnung (Watchdog, Firmware-Upload) gehen höchstens die Daten seit der letzten Speicherung verloren, also maximal 15 Minuten. Pro Tag sind das etwa 100 Records. Die Lebensdauer des EEPROM reicht damit weit über die Lebensdauer des Geräts hinaus.

Der Zähler-Record von Firmware 1.0.0 hatte an der Stelle von TotalRuntime ein reserviertes Feld mit dem Wert 0. Alte Records bleiben deshalb lesbar und starten mit TotalRuntime = 0.

## Tests

```
make -C tests/host
```
Getestet werden CRC32, NMEA-Checksumme und Parser, die strikte Zahlenprüfung sowie der Ringpuffer mit simuliertem M95P08: 3000 Start/Ausfall-Zyklen mit Überlauf, das Speichern von TotalRuntime und ein beim Stromausfall abgebrochener Schreibvorgang. Die Simulation bricht ab, falls auf nicht gelöschte Zellen programmiert würde.

UDP-Kommandos lassen sich z. B. unter Linux so senden:
```
echo -n '$PXISWNS,K,0.42*0B' | nc -u -b -w1 255.255.255.255 1458
```

## Annahmen und Hinweise

Stand Spezifikation V1.8. Die Punkte aus V1.0 bis V1.2 (Tx-Felder, „F“, Polarität Impulse/Direction, TotalRuntime) sind dort geklärt.

1. **VCC-Messung, Hardware (CR_008):** Mit dem Teiler 1/11 ergeben 36 V am ADC 3,27 V. Der ESP32-C3-ADC misst bei 11 dB nur bis etwa 2,5 V genau, **die Anzeige sättigt also ab etwa 27 V**. Nach der Hardware-Änderung `VCC_DIVIDER_RATIO` in `config.h` anpassen. Für den Feinabgleich gibt es `VCC_CALIBRATION`.
2. **Polaritäten** in `config.h` beziehen sich auf den GPIO-Pin. U2A/U2B (74LVC2G14) invertieren gegenüber der Klemme J2.
3. **Systemstarts:** Werden beim Start erhöht, der erste Start nach Erststart bzw. Factory Reset meldet also 1.
4. **USB-Betrieb ohne Vin:** VCC_Sense ist dann Low und die Ausfallerkennung bleibt inaktiv. Es erscheint eine Warnung im Log.
5. **GPIO20/21** sind die UART0-Pins. Beim Reset gibt das ROM Boot-Meldungen auf GPIO21 (!WP1) aus, was ohne aktives !CS1 unkritisch ist. Empfehlung: Pull-up 10k an !CS1, damit das EEPROM beim Booten sicher deselektiert ist.
6. **Neustart und Factory Reset per UDP** sind ohne Authentisierung möglich. Jedes Gerät im WLAN kann sie auslösen.
7. **M95P08:** Die Befehlscodes (02h Page Write, 0Ah Page Program, DBh/20h/D8h Erase) und Zeiten stammen aus ST TN1559/AN5771. Am Gerät prüfen: Wirkung von !WP und Block-Protect-Bits im Statusregister (wird beim Start geloggt).
8. **R8 (CR_009):** Nach der Änderung auf 10k die Einstellung von RV1 und die Schaltschwelle von Impulse nachprüfen.
