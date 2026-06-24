#ifndef SPORT_LIPO_H
#define SPORT_LIPO_H

/*
 * sport_lipo.h  –  ESP32-RC-Sound v0.84  (Hardware V4)
 *
 * S.Port Master-Polling für bis zu 2 FrSky FLVSS/MLVSS LiPo-Sensoren.
 * Der ESP32 übernimmt die Rolle des S.Port-Masters und pollt die Sensoren
 * aktiv über einen dedizierten Hardware-UART (Serial1).
 *
 * Hardware V4 – Pins (in V1/V2/V3 alle frei):
 *   GPIO 33  → S.Port TX  (Output, kein Strapping)
 *   GPIO 32  → S.Port RX  (Input, kein Strapping)
 *
 * Schaltung (Half-Duplex, S.Port ist invertierter UART 57600 Baud):
 *
 *   ESP32 GPIO 33 (TX) ──[1N4148 Anode→Kathode]──┬── S.Port Signal (gelb)
 *   ESP32 GPIO 32 (RX) ────────────────────────────┘
 *   ESP32 GND ──────────────────────────────────────── S.Port GND (schwarz)
 *
 *   Diode 1N4148: Anode an GPIO 33, Kathode Richtung Bus
 *   (schwarzer Ring zeigt WEG von GPIO 33, Richtung S.Port Kabel)
 *
 * Warum GPIO 32 / 33?
 *   In V1: GPIO 32/33 komplett frei (V1 nutzt GPIO 0,2,4,16,17,22)
 *   In V2: GPIO 32 = PWM 5, GPIO 33 = PWM 6 – in V4 nicht verwendet
 *   In V3: beide frei (V3 hat keine PWM-Eingänge)
 *   Kein Strapping-Pin, kein Boot-Konflikt, vollwertig Input+Output
 *   Serial1 frei (Serial2 = SBUS auf GPIO 16/17)
 *
 * Sensor Poll-IDs (konfigurierbar über Weboberfläche Seite 12):
 *   Sensor 1 (Pack 1): Werkseinstellung Physical ID 0x02 → Poll-Byte 0xA1
 *   Sensor 2 (Pack 2): Umprogrammiert auf Physical ID 0x03 → Poll-Byte 0x22
 *
 * ⚠ 12S Hinweis: Bei 2×6S in Reihe darf GND NUR am ersten Sensor
 *   angeschlossen werden. Am zweiten Sensor entsteht sonst ein Kurzschluss!
 */

#include <Arduino.h>

// ── Pin-Konfiguration V4 ──────────────────────────────────────────────
#define SPORT_TX_PIN     33      // Output GPIO, kein Strapping
#define SPORT_RX_PIN     32      // Input GPIO, kein Strapping
#define SPORT_UART_PORT   1      // Serial1 (Serial2 = SBUS/CRSF auf GPIO 16/17)

// S.Port: 57600 Baud, 8N1, invertiert
#define SPORT_BAUD       57600

// ── Sensor-Konfiguration ──────────────────────────────────────────────
#define SPORT_MAX_CELLS    6     // FLVSS/MLVSS: max. 6S pro Sensor
#define SPORT_NUM_SENSORS  2     // Pack 1 + Pack 2

// Physical Poll-Bytes werden zur Laufzeit aus config.sport_poll_id[] gelesen.
// Standardwerte (Werkseinstellung):
//   Sensor 1: Physical ID 0x02 → Poll-Byte 0xA1
//   Sensor 2: Physical ID 0x03 → Poll-Byte 0x22
// Konfigurierbar über Weboberfläche → Seite 12 (FrSky LiPo Sensoren)

// ── Protokoll-Konstanten ──────────────────────────────────────────────
#define SPORT_START_BYTE   0x7E
#define SPORT_DATA_FRAME   0x10
#define SPORT_CELLS_ID     0x0300   // Data-ID FLVSS Einzelzellen (0x0300–0x030F)
#define SPORT_STUFF_BYTE   0x7D     // Byte-Stuffing Escape
#define SPORT_STUFF_MASK   0x20     // XOR-Maske nach Escape

// ── Timing ────────────────────────────────────────────────────────────
#define SPORT_POLL_MS      20       // Poll-Intervall pro Sensor (ms)
#define SPORT_TIMEOUT_MS   2000     // Sensor offline nach dieser Zeit ohne Paket

// ── Datenstruktur pro Sensor ──────────────────────────────────────────
struct LipoSensorData {
    float    cellVoltage[SPORT_MAX_CELLS];  // Einzelzellen in Volt
    uint8_t  cellCount;                     // Erkannte Zellanzahl (0 = offline)
    float    totalVoltage;                  // Summe aller Zellen in Volt
    float    minCell;                       // Niedrigste Einzelzelle in Volt
    unsigned long lastUpdateMs;             // Zeitstempel letztes gültiges Paket
    bool     online;                        // true = Sensor antwortet
};

// ── Öffentliche Daten ─────────────────────────────────────────────────
extern LipoSensorData lipoSensor[SPORT_NUM_SENSORS];

// ── Öffentliche Funktionen ────────────────────────────────────────────
void    sportLipoInit();
void    sportLipoUpdate();              // Non-blocking, in loop() aufrufen

float   sportGetTotalVoltage();         // Gesamtspannung beider Packs (V)
float   sportGetMinCell();              // Niedrigste Einzelzelle aller Packs (V)
uint8_t sportGetTotalCells();           // Gesamtzellanzahl beider Packs
uint8_t sportCalcSoC(float minCell);    // SoC aus min. Zellspannung (0–100%)
void    sportSendCellsTelemetry(uint8_t sensorIdx); // CRSF Voltages senden

#endif // SPORT_LIPO_H
