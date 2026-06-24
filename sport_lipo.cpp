/*
 * sport_lipo.cpp  –  ESP32-RC-Sound v0.84  (Hardware V4)
 *
 * S.Port Master: ESP32 pollt FrSky FLVSS/MLVSS Sensoren und parst
 * Einzelzell-Spannungen. Automatische Erkennung der Zellanzahl (1–6S).
 *
 * S.Port Paketformat (Sensor → Master, nach Byte-Stuffing 8 Bytes):
 *   Byte 0: 0x10 (DATA_FRAME)
 *   Byte 1: Data-ID Low
 *   Byte 2: Data-ID High
 *   Byte 3–6: Payload (32 Bit, Little Endian)
 *   Byte 7: CRC
 *
 * CELLS-Payload (Data-ID 0x0300, 32 Bit):
 *   Bits  3:0  → Zell-Startindex (0 = Paar 1+2, 1 = Paar 3+4, 2 = Paar 5+6)
 *   Bits 14:4  → Spannung Zelle A in Einheiten von 5mV
 *   Bits 17:15 → Gesamtzellanzahl des Packs (1–6), nur in Paket mit Index 0 gültig
 *   Bits 28:18 → Spannung Zelle B in Einheiten von 5mV (0 wenn nicht vorhanden)
 *
 * Beispiel 4S-Pack, erstes Paket (Index 0):
 *   Zelle 1 = 3.810V → 762 × 5mV, Zelle 2 = 3.820V → 764 × 5mV, Count = 4
 *   value = (0) | (762 << 4) | (4 << 15) | (764 << 18)
 */

#include "sport_lipo.h"
#include "crsf_esp32.h"
#include "config.h"

// Zugriff auf den CRSF-Instanz aus dem Hauptsketch
extern CRSF crsf;

// ── Globale Sensor-Daten ──────────────────────────────────────────────
LipoSensorData lipoSensor[SPORT_NUM_SENSORS] = {};

// ── UART-Instanz ──────────────────────────────────────────────────────
static HardwareSerial sportSerial(SPORT_UART_PORT);  // Serial1 (GPIO 33 TX / GPIO 32 RX)

// ── State-Machine ─────────────────────────────────────────────────────
enum SportRxState : uint8_t { WAIT_START, WAIT_PHYSID, RECV_DATA };
static SportRxState rxState   = WAIT_START;
static uint8_t      rxBuf[8]  = {};
static uint8_t      rxIdx     = 0;
static bool         rxStuffed = false;

// Welcher Sensor gerade auf Antwort wartet
static uint8_t      pollSensor   = 0;
static unsigned long lastPollMs  = 0;

// ── CRC (S.Port: Summe aller Bytes mod 256, dann 0xFF minus Ergebnis) ─
static uint8_t sportCrc(const uint8_t* buf, uint8_t len) {
    uint16_t sum = 0;
    for (uint8_t i = 0; i < len; i++) sum += buf[i];
    sum = (sum >> 8) + (sum & 0xFF);
    return (uint8_t)(0xFF - sum);
}

// ── Empfangenes Byte verarbeiten ──────────────────────────────────────
static void sportProcessByte(uint8_t b) {

    // 0x7E = neues Frame beginnt (auch unser eigenes Echo vom Poll)
    if (b == SPORT_START_BYTE) {
        rxState   = WAIT_PHYSID;
        rxIdx     = 0;
        rxStuffed = false;
        return;
    }

    // Byte-Stuffing auflösen
    if (!rxStuffed && b == SPORT_STUFF_BYTE) {
        rxStuffed = true;
        return;
    }
    if (rxStuffed) {
        b ^= SPORT_STUFF_MASK;
        rxStuffed = false;
    }

    switch (rxState) {

        case WAIT_PHYSID:
            // Physical-ID Byte → ist unser eigenes Poll-Echo, ignorieren
            // Danach kommen 8 Sensor-Antwort-Bytes
            rxState = RECV_DATA;
            rxIdx   = 0;
            break;

        case RECV_DATA:
            if (rxIdx < 8) rxBuf[rxIdx++] = b;
            if (rxIdx == 8) {
                rxState = WAIT_START;

                // ── CRC prüfen ────────────────────────────────────────
                if (sportCrc(rxBuf, 7) != rxBuf[7]) return;

                // ── Frame-Typ ─────────────────────────────────────────
                if (rxBuf[0] != SPORT_DATA_FRAME) return;

                // ── Data-ID (16 Bit, LE) ──────────────────────────────
                uint16_t dataId = (uint16_t)rxBuf[1] | ((uint16_t)rxBuf[2] << 8);
                if (dataId < SPORT_CELLS_ID || dataId > (SPORT_CELLS_ID + 0x0F)) return;

                // ── Payload (32 Bit, LE) ──────────────────────────────
                uint32_t val = (uint32_t)rxBuf[3]
                             | ((uint32_t)rxBuf[4] << 8)
                             | ((uint32_t)rxBuf[5] << 16)
                             | ((uint32_t)rxBuf[6] << 24);

                // ── CELLS dekodieren ──────────────────────────────────
                uint8_t cellIdx    = (uint8_t)(val & 0x0F);
                float   voltA      = ((val >>  4) & 0x7FF) * 0.005f;
                uint8_t totalCells = (uint8_t)((val >> 15) & 0x07);
                float   voltB      = ((val >> 18) & 0x7FF) * 0.005f;

                LipoSensorData& s = lipoSensor[pollSensor];

                // Zellanzahl aus erstem Paket (cellIdx==0) lesen
                if (cellIdx == 0 && totalCells >= 1 && totalCells <= SPORT_MAX_CELLS) {
                    s.cellCount = totalCells;
                }

                // Spannungen eintragen
                uint8_t idxA = cellIdx * 2;
                uint8_t idxB = cellIdx * 2 + 1;
                if (idxA < SPORT_MAX_CELLS) s.cellVoltage[idxA] = voltA;
                if (idxB < SPORT_MAX_CELLS && voltB > 0.01f) s.cellVoltage[idxB] = voltB;

                // Wenn letztes Paket des Sensors empfangen → Gesamtwerte berechnen
                if (s.cellCount > 0) {
                    uint8_t lastPktIdx = (s.cellCount - 1) / 2;
                    if (cellIdx == lastPktIdx) {
                        s.totalVoltage = 0.0f;
                        s.minCell      = 9.9f;
                        for (uint8_t i = 0; i < s.cellCount; i++) {
                            s.totalVoltage += s.cellVoltage[i];
                            if (s.cellVoltage[i] > 0.1f && s.cellVoltage[i] < s.minCell)
                                s.minCell = s.cellVoltage[i];
                        }
                        s.lastUpdateMs = millis();
                        s.online       = true;
                    }
                }
            }
            break;

        default:
            break;
    }
}

// ── Initialisierung ───────────────────────────────────────────────────
void sportLipoInit() {
    // Serial2 mit invertiertem Signal (S.Port = invertierter UART)
    sportSerial.begin(SPORT_BAUD, SERIAL_8N1, SPORT_RX_PIN, SPORT_TX_PIN, true);

    // Sensor-Strukturen zurücksetzen
    for (uint8_t i = 0; i < SPORT_NUM_SENSORS; i++) {
        memset(&lipoSensor[i], 0, sizeof(LipoSensorData));
    }

    rxState    = WAIT_START;
    pollSensor = 0;
    lastPollMs = 0;

    Serial.printf("[S.Port] Init: TX=GPIO%d  RX=GPIO%d  %d Baud (invertiert)\n",
                  SPORT_TX_PIN, SPORT_RX_PIN, SPORT_BAUD);
}

// ── Update (non-blocking, in loop() aufrufen) ─────────────────────────
void sportLipoUpdate() {
    unsigned long now = millis();

    // Empfangene Bytes verarbeiten
    while (sportSerial.available()) {
        sportProcessByte((uint8_t)sportSerial.read());
    }

    // Sensor-Timeout prüfen
    for (uint8_t i = 0; i < SPORT_NUM_SENSORS; i++) {
        if (lipoSensor[i].online &&
            (now - lipoSensor[i].lastUpdateMs) > SPORT_TIMEOUT_MS) {
            lipoSensor[i].online     = false;
            lipoSensor[i].cellCount  = 0;
            lipoSensor[i].totalVoltage = 0.0f;
            lipoSensor[i].minCell    = 0.0f;
            Serial.printf("[S.Port] Sensor %d offline\n", i + 1);
        }
    }

    // Poll senden (Round-Robin)
    if ((now - lastPollMs) >= SPORT_POLL_MS) {
        lastPollMs = now;

        // 0x7E + Physical-ID senden → Sensor antwortet mit DATA_FRAME
        sportSerial.write(SPORT_START_BYTE);
        sportSerial.write(config.sport_poll_id[pollSensor]);

        // State-Machine auf Empfang der Antwort vorbereiten
        rxState   = WAIT_PHYSID;
        rxIdx     = 0;
        rxStuffed = false;

        // Nächsten Sensor für nächsten Poll-Zyklus
        pollSensor = (pollSensor + 1) % SPORT_NUM_SENSORS;
    }
}

// ── Hilfsfunktionen ───────────────────────────────────────────────────

float sportGetTotalVoltage() {
    float v = 0.0f;
    for (uint8_t i = 0; i < SPORT_NUM_SENSORS; i++)
        if (lipoSensor[i].online) v += lipoSensor[i].totalVoltage;
    return v;
}

float sportGetMinCell() {
    float minV  = 9.9f;
    bool  found = false;
    for (uint8_t i = 0; i < SPORT_NUM_SENSORS; i++) {
        if (lipoSensor[i].online && lipoSensor[i].minCell > 0.1f) {
            if (lipoSensor[i].minCell < minV) { minV = lipoSensor[i].minCell; found = true; }
        }
    }
    return found ? minV : 0.0f;
}

uint8_t sportGetTotalCells() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < SPORT_NUM_SENSORS; i++)
        if (lipoSensor[i].online) n += lipoSensor[i].cellCount;
    return n;
}

/*
 * Ladezustand (SoC) aus niedrigster Einzelzellspannung (LiPo-Ruhespannung):
 *   4.20V = 100%   4.00V = 75%   3.80V = 50%
 *   3.60V = 25%    3.40V = 10%   < 3.40V = 0%
 */
uint8_t sportCalcSoC(float minCell) {
    if (minCell <= 0.1f)  return 0;
    if (minCell >= 4.20f) return 100;
    if (minCell >= 4.00f) return (uint8_t)map((long)(minCell * 100), 400, 420,  75, 100);
    if (minCell >= 3.80f) return (uint8_t)map((long)(minCell * 100), 380, 400,  50,  75);
    if (minCell >= 3.60f) return (uint8_t)map((long)(minCell * 100), 360, 380,  25,  50);
    if (minCell >= 3.40f) return (uint8_t)map((long)(minCell * 100), 340, 360,  10,  25);
    return 0;
}

/*
 * Einzelzellen eines Sensors als CRSF Voltages-Telemetrie senden.
 * sensorIdx: 0 = Pack 1 (source_id=1), 1 = Pack 2 (source_id=2)
 * Werte in mV (int32_t). EdgeTX erkennt diese nach "Sensor suchen"
 * als native Sensoren und zeigt sie als Widgets an.
 */
void sportSendCellsTelemetry(uint8_t sensorIdx) {
    if (sensorIdx >= SPORT_NUM_SENSORS) return;
    const LipoSensorData& s = lipoSensor[sensorIdx];
    if (!s.online || s.cellCount == 0) return;

    uint8_t srcId = sensorIdx + 1;

    int32_t mv[SPORT_MAX_CELLS];
    for (uint8_t i = 0; i < s.cellCount; i++)
        mv[i] = (int32_t)(s.cellVoltage[i] * 1000.0f + 0.5f);

    switch (s.cellCount) {
        case 1: crsf.send_tele_Voltages(srcId, {mv[0]}); break;
        case 2: crsf.send_tele_Voltages(srcId, {mv[0], mv[1]}); break;
        case 3: crsf.send_tele_Voltages(srcId, {mv[0], mv[1], mv[2]}); break;
        case 4: crsf.send_tele_Voltages(srcId, {mv[0], mv[1], mv[2], mv[3]}); break;
        case 5: crsf.send_tele_Voltages(srcId, {mv[0], mv[1], mv[2], mv[3], mv[4]}); break;
        case 6: crsf.send_tele_Voltages(srcId, {mv[0], mv[1], mv[2], mv[3], mv[4], mv[5]}); break;
        default: break;
    }
}
