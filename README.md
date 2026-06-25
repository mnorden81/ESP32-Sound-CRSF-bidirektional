# ESP32-RC-Sound v0.70

**Autor:** Ziege-One (Der RC-Modellbauer)

ESP32-basiertes RC-Soundmodul mit I2S-Audioausgabe, SD-Karten-Wiedergabe und Konfiguration über WLAN-Weboberfläche oder TBS Agent (CRSF-Parameterystem).

---

## Funktionsübersicht

- Motorklangsimulation (Start, Loop, Abschalten) mit drehzahlabhängiger Abspielgeschwindigkeit
- 8 frei konfigurierbare Zusatzsounds (WAV-Dateien von SD-Karte)
- Unterstützung für SBUS (FrSky, FlySky, ELRS SBUS, Hott) und CRSF/ELRS
- Einkanal-Multiplexing (MultiSwitch-Protokoll, WM0–WM3)
- Ebenenumschaltung (bis zu 7 Ebenen × 3 Gruppen)
- **Alle drei Hardware-Versionen (V1, V2, V3) in einem Firmware-Image**
- Hardware-Version per Weboberfläche oder TBS Agent umschaltbar
- **CRSF-Parametersystem** (75 Parameter, vollständige Konfiguration über TBS Agent)
- WLAN-Weboberfläche zur Konfiguration (Access-Point-Modus)

---

## Neu in v0.70 (gegenüber v0.45)

- **Alle Hardware-Versionen vereint** – V1, V2 und V3 in einem Programm
- **Hardware V3** – reiner BUS-Betrieb (SBUS/CRSF) ohne GPIO-Eingänge
- **CRSF-Parametersystem** – 75 Parameter, vollständige Konfiguration über TBS Agent (EdgeTX/OpenTX Lua-Script)
- **CRSF-Telemetrie** – Battery-Sensor Keep-Alive
- **Sound-Test** – Sounds direkt über TBS Agent testbar
- **Gerätebezeichnung** – frei konfigurierbar über Weboberfläche
- Drehzahlmax-Wert intern halbiert gespeichert (größerer Wertebereich im CRSF-UI)

---

## Hardware-Versionen

| Version | GPIO-Pins (Eingänge) | Besonderheit |
|---|---|---|
| **V1** | 22, 0, 2, 4 | BUS + PWM-Eingang + GPIO-Pin + Einkanal |
| **V2** | 14, 27, 32, 33 | BUS + PWM-Eingang + GPIO-Pin + Einkanal |
| **V3** | – | Nur BUS-Kanal + Einkanal (SBUS/CRSF) |

---

## Pin-Belegung

### Alle Versionen

| GPIO | Funktion |
|---|---|
| 13 | WiFi-Aktivierung (LOW = AP-Modus) |
| 16 | CRSF RX / SBUS RX |
| 17 | CRSF TX |
| 05 | SD_CS |
| 18 | SD_CLK |
| 19 | SD_MISO |
| 23 | SD_MOSI |
| 21 | I2S_DOUT |
| 25 | I2S_LRC |
| 26 | I2S_BCLK |

### V1 – zusätzliche Eingänge

| GPIO | Funktion |
|---|---|
| 22, 0, 2, 4 | Input 3–6 (PWM/GPIO) |

### V2 – zusätzliche Eingänge

| GPIO | Funktion |
|---|---|
| 14, 27, 32, 33 | Input 3–6 (PWM/GPIO) |

---

## Hardware-Voraussetzungen

| Komponente | Beschreibung |
|---|---|
| ESP32 | Board-Version 3.3.8 |
| SD-Karte | SPI-Anschluss |
| I2S-DAC/Verstärker | z. B. MAX98357A |
| RC-Empfänger | SBUS oder CRSF |

---

## SD-Karten-Dateien

| Dateiname | Funktion |
|---|---|
| `loop.wav` | Motor-Laufgeräusch (Schleife) |
| `start.wav` | Motorstart-Geräusch |
| `shut.wav` | Motorabschalt-Geräusch |
| `sound1.wav` – `sound8.wav` | Zusatzsounds 1–8 |

---

## Bibliotheken

| Bibliothek | Version |
|---|---|
| Bolder Flight Systems SBUS | 8.1.4 |
| CRSF_ESP32 | https://github.com/Ziege-One/CRSF_ESP32 |

---

## Konfiguration

### Weboberfläche (alle Versionen)
1. GPIO 13 auf LOW ziehen
2. ESP32 startet als WLAN-Access-Point
3. Mit dem konfigurierten Netzwerk verbinden und Weboberfläche öffnen

### TBS Agent / EdgeTX Lua (CRSF-Modus)
- Vollständige Konfiguration über 75 CRSF-Parameter
- Ordnerstruktur: Motor, Sound 1–8, Einstellungen
- Sound-Test direkt aus dem Agent heraus möglich

---

## Quellentypen

| Code | Quelle | V1/V2 | V3 |
|---|---|---|---|
| 0–15 | BUS-Kanal Low (1–16) | ✓ | ✓ |
| 20–35 | BUS-Kanal High (1–16) | ✓ | ✓ |
| 40–45 | PWM-Eingang Low (1–6) | ✓ | – |
| 50–55 | PWM-Eingang High (1–6) | ✓ | – |
| 60–65 | GPIO-Pin direkt (1–6) | ✓ | – |
| 70–77 | Einkanal-Bit (1–8) | ✓ | ✓ |
| 80–103 | Ebenen-Umschaltung | ✓ | ✓ |
| 200 | Dauerbetrieb | ✓ | ✓ |
| 999 | Deaktiviert | ✓ | ✓ |
