#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h  -  ESP32-RC-Sound v0.84
 *
 * Hardware_Config:
 *   0 = V1  GPIO-Pins {22, 0, 2, 4}  – BUS + PWM + Pin + Einkanal
 *   1 = V2  GPIO-Pins {14,27,32,33}  – BUS + PWM + Pin + Einkanal
 *   2 = V3  Kein GPIO-Eingang        – nur BUS + Einkanal
 */

#include <Arduino.h>

struct ConfigData {
    int Source_Start_Sound[9];   // Quelle Motor(0) + Sound 1-8
    int Volumen_Sound[9];        // Lautstärke 0-200
    int Mode_Sound[9];           // 0=Normal, 1=Loop, 2=Tippbetrieb

    int Source_Speed_Sound_0;    // Quelle Motorgeschwindigkeit
    int throttle_mode;           // 0=Eine Richtung, 1=Zwei Richtungen
    int Min_Speed_Sound_0;       // Mindestgeschwindigkeit Motor (%)
    int Max_Speed_Sound_0;       // Maximalgeschwindigkeit Motor (%)
    int shutdowndelay;           // Motor aus Standgas Verzögerung (s)
    int engine_on_toggle;        // 0=Normal, 1=Toggle
    int throttle_ramp;           // Gas-Rampe (%/s)
    int throttle_dead_band;      // Standgas-Totband (%)

    int Einkanal_Channel;        // BUS-Kanal für Einkanal (999=deaktiviert)
    int Einkanal_mode;           // 0=Normal, 10-13=WM-Adresse 0-3
    int Einkanal_RC_System;      // 0=FrSky,1=FlySky,2=ELRS SBUS,3=Hott,4=ELRS CRSF
    int modul_adress;            // Modul-Adresse (WM/CRSF)

    uint8_t sport_poll_id[2];    // S.Port Physical Poll-ID Sensor 1+2 (z.B. 0xA1, 0x22)

    int Source_Ebenen_Um_Kanal;  // Ebenen Umschalt-Kanal (999=deaktiviert)
    int Source_Ebenen_Kanal;     // Ebenen Werte-Kanal    (999=deaktiviert)

    int Hardware_Config;         // 0=V1, 1=V2, 2=V3
    int PWM_scale_min;           // PWM Skalierung min (µs) – V1/V2
    int PWM_scale_max;           // PWM Skalierung max (µs) – V1/V2

    char WiFi_SSID[32];
    char WiFi_Password[64];
    char WiFi_IP[16];
    char Device_Name[24];
};

extern ConfigData    config;
extern bool          configDirty;
extern unsigned long configDirtyMs;

void loadConfig();
void saveConfig();
void saveConfigForce();
void markDirty();
void Reset_all();
void set_sbus();
void set_pwm();
void set_pin();

#endif
