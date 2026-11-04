# ChainCounter

Kettenzähler für eine Ankerwinde. Er zählt die Umdrehungen des Kettenrads, rechnet sie in die Kettenlänge um und sendet die Werte einmal pro Sekunde als NMEA0183-Satz per UDP-Broadcast an das Navigationssystem.

- **Hardware:** Seeed XIAO ESP32C3 auf Interface-PCB P2410-01.401 V2.0
- **Spezifikation:** HW/SW Spezifikation V1.0
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
$ISLWND,<Kettenlänge m %.1f>,<Richtung 0=down 1=up>,<VCC V %.1f>,<Systemstarts>,<Laufzeit s>*XX
```
Beispiel: `$ISLWND,12.4,0,24.1,17,3605*11`

### UDP Rx (Kommandos `$ISLWNS,p1[,p2]*xx`)

| Kommando | Wirkung | Speicherung |
|---|---|---|
| `$ISLWNS,C*xx` | Kettenzähler = 0 | beim nächsten Stromausfall |
| `$ISLWNS,C,<m>*xx` | Kettenlänge setzen (%d, Meter → Umdrehungen) | beim nächsten Stromausfall |
| `$ISLWNS,S,<ms>*xx` | DebounceTimer (1…1000 ms) | sofort |
| `$ISLWNS,K,<m/U>*xx` | Umrechnungsfaktor (0.001…100) | sofort |
| `$ISLWNS,F*xx` | Factory Reset: EEPROM löschen, Neustart, Initialwerte schreiben | – |

Jeder empfangene Satz wird geloggt. Fehler werden auf der seriellen Schnittstelle protokolliert: falsche Checksumme, fehlender Parameter, falsches Format, zu viele Parameter, Wertebereich. Sätze mit anderem Adressfeld werden geloggt und ignoriert, eigene Broadcasts werden verworfen.

### Serielle Konsole (115200 Baud)
Zusätzlich zur Spezifikation gibt es eine serielle Konsole:

| Befehl | Funktion |
|---|---|
| `$ISLWNS,...*xx` | gleiches Kommando wie per UDP |
| `cs $ISLWNS,K,0.42` | hängt die Checksumme an (zum Testen): `$ISLWNS,K,0.42*4F` |
| `status` | zeigt den aktuellen Zustand |
| `save` | schreibt den Zähler sofort ins EEPROM |
| `ssid=<name>` / `pass=<pw>` / `port=<n>` | WLAN und Port ändern (wird gespeichert) |

SSID und Passwort sind laut Spezifikation „konstant, aber konfigurierbar“. Weil es dafür kein NMEA-Kommando gibt, laufen sie über die Konsole und damit nicht übers Netz.

### LED
Dauernd an: WLAN verbunden. Blinkt mit 2 Hz: Verbindungsaufbau. Kurzer Blitz: Impuls gezählt.

### WLAN
DHCP, `setAutoReconnect(true)`. Fehlt die Verbindung länger als 15 s, wird `WiFi.begin()` neu aufgerufen. Danach wird der UDP-Socket neu geöffnet.

## EEPROM M95P08 und Stromausfall

### Layout
| Bereich | Adresse | Grösse | Recordgrösse | Slots |
|---|---|---|---|---|
| Zähler (Umdrehungen, Systemstarts) | 0x000000 | 64 KB | 32 Byte | 2048 |
| Konfiguration (K, Debounce, Port, SSID, Passwort) | 0x010000 | 4 KB | 128 Byte | 32 |

Jeder Record ist so aufgebaut: `magic | seq | Daten | crc32`. Die CRC wird über alle Bytes ausser der CRC selbst gebildet, mit Startwert `0x4711e2c7`, der Tabelle aus der Spezifikation und ohne abschliessendes XOR. Beim Lesen wird sie immer geprüft.

### Wear-Leveling
Neue Records werden immer in den **nächsten** Slot geschrieben. Ist der Bereich voll, beginnt das Schreiben wieder vorne. Beim Start wird der ganze Bereich gelesen (64 KB, ca. 60 ms) und der gültige Record mit der höchsten Sequenznummer verwendet. Bei 2048 Slots und 500 000 Zyklen pro Zelle reicht das für weit über eine Million Power-Cycles.

### Klärung aus der Spezifikation: nächsten Record vorbereiten?
**Ja, er wird vorbereitet.** Beim Start, solange die Speisung gut ist, prüft die Firmware den nächsten Slot. Liegt er am Anfang einer neuen Page, wird diese Page bei Bedarf gelöscht (Page Erase, max. 4,5 ms). Bei einem Stromausfall braucht es dann weder Suche noch Löschen, sondern nur ein **Page Program** von 32 Byte (typisch etwa 0,1–0,2 ms). Damit bleibt viel Reserve zu den garantierten 20 ms.

Ablauf beim Ausfall: Flanke an VCC_Sense → ISR → FreeRTOS-Task mit höchster Priorität → Record schreiben und zurücklesen → loggen. Die Ausfallerkennung wird erst scharf geschaltet, wenn VCC_Sense 200 ms lang High war. Kommt die Speisung ohne Reset zurück, wird der nächste Slot vorbereitet und die Erkennung wieder aktiviert.

Bei jedem Start wird `Systemstarts` erhöht und sofort ein Record geschrieben. Ein fehlgeschlagenes Speichern beim Ausfall kostet so höchstens die Zählung seit diesem Start.

## Tests

```
make -C tests/host
```
Getestet werden CRC32, NMEA-Checksumme und Parser, die strikte Zahlenprüfung sowie der Ringpuffer mit simuliertem M95P08: 3000 Start/Ausfall-Zyklen mit Überlauf und ein beim Stromausfall abgebrochener Schreibvorgang. Die Simulation bricht ab, falls auf nicht gelöschte Zellen programmiert würde.

UDP-Kommandos lassen sich z. B. unter Linux so senden:
```
echo -n '$ISLWNS,K,0.42*4F' | nc -u -b -w1 255.255.255.255 1467
```

## Annahmen und offene Punkte

Unklare oder widersprüchliche Stellen in Spezifikation V1.0 und wie sie umgesetzt sind:

1. **Tx-Format:** Im sprintf stehen 6 Platzhalter (`%.1f,%d,%.1f,%d,%d,%d`), es sind aber nur 5 Werte beschrieben. Umgesetzt sind die 5 Werte. Falls ein sechstes Feld gemeint ist, bitte angeben.
2. **Factory Reset:** In der Tabelle steht bei p1 `"K"`. Umgesetzt ist es als `"F"`.
3. **Impulse:** Die Angabe „Low: Reedkontakt“ ist unvollständig. Angenommen ist Low = offen, gezählt wird beim Schliessen. Achtung: U2A/U2B (74LVC2G14) **invertieren**. Die Pegel in `config.h` beziehen sich auf den GPIO-Pin.
4. **VCC-Messung, Hardware:** Mit dem Teiler 1/11 ergeben 36 V am ADC 3,27 V. Der ESP32-C3-ADC misst bei 11 dB aber nur bis etwa 2,5 V genau, **die Anzeige sättigt also ab etwa 27 V**. Weil VCC_Sense am selben Teiler hängt, lässt sich der Teiler nicht einfach ändern. Vorschlag: ein eigener Teiler für den ADC, z. B. 82k/6k2. Für den Feinabgleich gibt es `VCC_CALIBRATION` in `config.h`.
5. **Systemstarts:** Werden beim Start erhöht, der erste Start nach Erststart bzw. Factory Reset meldet also 1.
6. **MISO** ist auf dem ESP ein Eingang (in der Spezifikation steht „Ausgang“).
7. **USB-Betrieb ohne Vin:** VCC_Sense ist dann Low und die Ausfallerkennung bleibt inaktiv. Es erscheint eine Warnung im Log.
8. **GPIO20/21** sind die UART0-Pins. Beim Reset gibt das ROM Boot-Meldungen auf GPIO21 (!WP1) aus, was ohne aktives !CS1 unkritisch ist. Empfehlung: Pull-up 10k an !CS1, damit das EEPROM beim Booten sicher deselektiert ist.
9. **Factory Reset per UDP** ist ohne Authentisierung möglich. Jedes Gerät im WLAN kann ihn auslösen.
10. **M95P08:** Die Befehlscodes (02h Page Write, 0Ah Page Program, DBh/20h/D8h Erase) und Zeiten stammen aus ST TN1559/AN5771. Am Gerät prüfen: Wirkung von !WP und Block-Protect-Bits im Statusregister (wird beim Start geloggt).
