/*
   ESP32-RC-Sound  v0.70
   Ziege-One (Der RC-Modellbauer)

   Vereint V1, V2 und V3 in einem Programm.
   Hardware-Version per Web oder TBS Agent wählbar.

   HARDWARE_CONFIG:
     V1  GPIO-Pins {22, 0, 2, 4}   – BUS + PWM-Eingang + GPIO-Pin + Einkanal
     V2  GPIO-Pins {14,27,32,33}   – BUS + PWM-Eingang + GPIO-Pin + Einkanal
     V3  kein GPIO-Eingang          – nur BUS-Kanal + Einkanal (SBUS/CRSF)

   QUELLEN (V1/V2):
     BUS Kanal Low/High (1-16), PWM-Eingang Low/High (1-6),
     GPIO-Pin direkt (1-6), Einkanal (1-8), Ebene, Dauerbetrieb, Deaktiviert

   QUELLEN (V3):
     BUS Kanal Low/High (1-16), Einkanal (1-8), Ebene, Dauerbetrieb, Deaktiviert

   MOTOR SPEED:
     V1/V2: BUS Kanal + PWM Pin
     V3:    BUS Kanal

   PIN-BELEGUNG (alle Versionen):
   GPIO 13: WiFi-Aktivierung (LOW = AP-Modus)
   GPIO 16: CRSF RX / SBUS RX  |  GPIO 17: CRSF TX
   GPIO 05: SD_CS  |  GPIO 18: SD_CLK  |  GPIO 19: SD_MISO  |  GPIO 23: SD_MOSI
   GPIO 21: I2S_DOUT  |  GPIO 25: I2S_LRC  |  GPIO 26: I2S_BCLK
*/

#include <Arduino.h>
#include "XT_I2S_Audio.h"
#include <WiFi.h>
#include "sbus.h"
#include "crsf_esp32.h"
#include "config.h"
#include "WebServerManager.h"

uint16_t Version = 70;
char versionString[6];

// ── Zustand ───────────────────────────────────────────────────────────
bool Sound_on[9]     = {};
bool Sound_play[9]   = {};
bool Sound_on_web[9] = {};

bool Sound_on_Motor       = false;
bool Sound_on_Motor_state = false;
bool engine_break         = false;
unsigned long shutdown_timer = 0;

enum Engine_State : uint8_t { OFF, STARTING, RUNNING, STOPPING };
volatile uint8_t engine_State = OFF;

int      Einkanal_RC_System_boot = 0;
uint16_t einkanal_Data           = 0;
uint16_t einkanal_SpeicherWM     = 0;

int      Source_Ebenen_Um_Kanal_wert = 0;
int      Source_Ebenen_Kanal_wert    = 0;
uint16_t ebenenkanal_Data            = 99;

// ── Hardware ──────────────────────────────────────────────────────────
#define SD_CS    5
#define I2S_DOUT 21
#define I2S_BCLK 26
#define I2S_LRC  25
const uint8_t WifiPin = 13;

// GPIO-Pins: Index 0-1 = CRSF RX/TX (immer), 2-5 = Eingangs-Pins
// V1: {16,17, 22, 0, 2, 4}
// V2: {16,17, 14,27,32,33}
// V3: nicht verwendet
uint8_t Input_Pin[6] = {16, 17, 22, 0, 2, 4};  // Default V1

// ── Throttle / Rampe ─────────────────────────────────────────────────
unsigned long previousTime_ramp = 0;
float last_throttle = 0.0f;
float throttle      = 0.0f;

char ssid[32];
char password[64];
unsigned long currentTime = 0;

// ── SBUS ──────────────────────────────────────────────────────────────
bfs::SbusRx   sbus_rx(&Serial1, 16, 27, true);
bfs::SbusData sbus_data;
bool BUS_OK = false;

// ── CRSF ──────────────────────────────────────────────────────────────
CRSF crsf;
#define MWset       0x01
#define MWprop      0x02
#define MWset4      0x07
#define MWset4m     0x09
#define Multiswitch 0xA1

static constexpr unsigned long CRSF_TIMEOUT_MS = 2000;
static unsigned long lastCrsfPacket = 0;

static void checkCrsfTimeout() {
    if (Einkanal_RC_System_boot != 4) return;
    if (millis() - lastCrsfPacket > CRSF_TIMEOUT_MS) BUS_OK = false;
}

uint16_t channel_output[16] = {};

// ── I2S Audio ─────────────────────────────────────────────────────────
XT_I2S_Class I2SAudio(I2S_LRC, I2S_BCLK, I2S_DOUT, I2S_NUM_0);

XT_Wav_Class Sound_loop ("/loop.wav");
XT_Wav_Class Sound_shut ("/shut.wav");
XT_Wav_Class Sound_start("/start.wav");
XT_Wav_Class Sound1("/sound1.wav"); XT_Wav_Class Sound2("/sound2.wav");
XT_Wav_Class Sound3("/sound3.wav"); XT_Wav_Class Sound4("/sound4.wav");
XT_Wav_Class Sound5("/sound5.wav"); XT_Wav_Class Sound6("/sound6.wav");
XT_Wav_Class Sound7("/sound7.wav"); XT_Wav_Class Sound8("/sound8.wav");

XT_Wav_Class* Sounds[9] = {
    nullptr, &Sound1,&Sound2,&Sound3,&Sound4,&Sound5,&Sound6,&Sound7,&Sound8
};

// ── PWM-Eingang (V1/V2) ───────────────────────────────────────────────
volatile unsigned int PWM_pulse_width[6] = {3000,3000,3000,3000,3000,3000};
volatile unsigned int PWM_prev_time[6]   = {0,0,0,0,0,0};

static inline void IRAM_ATTR handlePWM(uint8_t idx) {
    if (digitalRead(Input_Pin[idx])) PWM_prev_time[idx] = micros();
    else PWM_pulse_width[idx] = micros() - PWM_prev_time[idx];
}
void IRAM_ATTR ISR_PWM_0() { handlePWM(0); }
void IRAM_ATTR ISR_PWM_1() { handlePWM(1); }
void IRAM_ATTR ISR_PWM_2() { handlePWM(2); }
void IRAM_ATTR ISR_PWM_3() { handlePWM(3); }
void IRAM_ATTR ISR_PWM_4() { handlePWM(4); }
void IRAM_ATTR ISR_PWM_5() { handlePWM(5); }
void (*ISR_PWM[6])() = {ISR_PWM_0,ISR_PWM_1,ISR_PWM_2,ISR_PWM_3,ISR_PWM_4,ISR_PWM_5};

static bool pwm_isr_attached[6] = {};
void attachPWM_ISR(uint8_t pinIdx) {
    // Nur in V1/V2 aktiv
    if (config.Hardware_Config >= 2) return;
    if (pinIdx < 6 && !pwm_isr_attached[pinIdx]) {
        attachInterrupt(digitalPinToInterrupt(Input_Pin[pinIdx]), ISR_PWM[pinIdx], CHANGE);
        pwm_isr_attached[pinIdx] = true;
    }
}

// ── Vorwärtsdeklarationen ─────────────────────────────────────────────
void Config(); void SDCardInit(); void LoadFiles();
void ebenenFunction(); void einkanalFunction(uint16_t ch);
void einkanalFunctionCRSF(); uint8_t compressSwitches(uint16_t state);
float ramp_throttle(float t); float floatMap(float x,float a,float b,float c,float d);
void handleSound(uint8_t idx, XT_Wav_Class* snd);
static void crsfSendParam(uint8_t idx);
static void crsfWriteParam(uint8_t idx, uint8_t val);

// ======== CRSF Parameter-System v0.70 (76 Parameter) =================
//
//  0: Root  1: Version Info
//  2: Folder Motor {3..15}
//   3: Sel MotorMode  4: Sel EINModus  5: Sel QuelleEIN  6: U8 KanalEIN
//   7: Sel QuelleSpd  8: U8 KanalSpd   9: U8 Vol
//  10: U8 Drzmin  11: U8 Drzmax/2  12: U8 Standgas  13: U8 Rampe  14: U8 Totband
//  15: Info HW-Version (aktuell aktive Hardware)
//
//  Sound s(1-8): fi=16+(s-1)*6  → endet bei 63
//   fi+0:Folder fi+1:Sel Quelle fi+2:U8 Kanal fi+3:U8 Vol fi+4:Sel Mode fi+5:Sel Test
//
//  64: Folder Einstellungen {65..72}
//   65:Sel RC-Sys  66:U8 ModAdr  67:U8 EKKanal  68:Sel EKMode
//   69:U8 EbUm  70:U8 EbK
//   71:Sel HardwareConfig  V1;V2;V3
//   72:U8 PWM min(/16)  73:U8 PWM max(/16)
//  74: Info Gerätename

static constexpr uint8_t CRSF_PARAM_COUNT = 75;

// ── Typ-Mapping Quelle Motor/Sound EIN ───────────────────────────────
// Typ: 0=BUS-L(0-15) 1=BUS-H(20-35) 2=PWM-L(40-45) 3=PWM-H(50-55)
//      4=Pin(60-65)  5=Einkanal(70-77) 6=Ebene(80-103)
//      7=Dauerbetrieb(200) 8=Deaktiviert(999)
// V3 ignoriert Typen 2/3/4 in Config()

static uint8_t einTyp(int c) {
    if(c==999)return 8; if(c==200)return 7;
    if(c>=80&&c<=103)return 6; if(c>=70&&c<=77)return 5;
    if(c>=60&&c<=65)return 4; if(c>=50&&c<=55)return 3;
    if(c>=40&&c<=45)return 2; if(c>=20&&c<=35)return 1;
    if(c>=0&&c<=15)return 0; return 8;
}
static uint8_t einNr(int c) {
    if(c>=80&&c<=103)return c-80+1; if(c>=70&&c<=77)return c-70+1;
    if(c>=60&&c<=65)return c-60+1; if(c>=50&&c<=55)return c-50+1;
    if(c>=40&&c<=45)return c-40+1; if(c>=20&&c<=35)return c-20+1;
    if(c>=0&&c<=15)return c+1; return 1;
}
static int setEin(uint8_t t, uint8_t nr) {
    uint8_t i=(nr>0)?nr-1:0;
    if(t==0)return min(i,(uint8_t)15);
    if(t==1)return 20+min(i,(uint8_t)15);
    if(t==2)return 40+min(i,(uint8_t)5);
    if(t==3)return 50+min(i,(uint8_t)5);
    if(t==4)return 60+min(i,(uint8_t)5);
    if(t==5)return 70+min(i,(uint8_t)7);
    if(t==6)return 80+min(i,(uint8_t)23);
    if(t==7)return 200; return 999;
}
static uint8_t einMaxNr(uint8_t t) {
    if(t==0||t==1)return 16; if(t==2||t==3||t==4)return 6;
    if(t==5)return 8; if(t==6)return 24; return 1;
}

// ── Typ-Mapping Quelle Speed ──────────────────────────────────────────
// Typ: 0=BUS Kanal(0-15) 1=PWM Pin(20-25) 2=Deaktiviert(999)
// V3 ignoriert Typ 1 in Config()
static uint8_t spdTyp(int c) {
    if(c>=0&&c<=15)return 0; if(c>=20&&c<=25)return 1; return 2;
}
static uint8_t spdNr(int c) {
    if(c>=0&&c<=15)return c+1; if(c>=20&&c<=25)return c-20+1; return 1;
}
static int setSpd(uint8_t t, uint8_t nr) {
    uint8_t i=(nr>0)?nr-1:0;
    if(t==0)return min(i,(uint8_t)15);
    if(t==1)return 20+min(i,(uint8_t)5);
    return 999;
}

static bool testSoundActive[9]={};

// ── Hilfsfunktion: Aktuelle HW-Version als String ─────────────────────
static const char* hwVersionStr() {
    switch(config.Hardware_Config) {
        case 0: return "V1 (GPIO 22,0,2,4)";
        case 1: return "V2 (GPIO 14,27,32,33)";
        default: return "V3 (nur BUS+EK)";
    }
}

// ── CRSF Quelle-Speed Optionen je nach HW ─────────────────────────────
static const char* spdSelStr() {
    return (config.Hardware_Config < 2)
        ? "BUS Kanal;PWM Pin;Deaktiviert"
        : "BUS Kanal;Deaktiviert";
}
static uint8_t spdSelMax() {
    return (config.Hardware_Config < 2) ? 2 : 1;
}
static uint8_t spdSelVal() {
    uint8_t t = spdTyp(config.Source_Speed_Sound_0);
    // V3: PWM (t==1) → zeige als Deaktiviert
    if (config.Hardware_Config >= 2 && t == 1) return 1;
    return t;
}

// ── CRSF Quelle EIN Optionen: V3 zeigt nur L/H/EK/Dauer/Aus ──────────
static const char* einSelStr() {
    return (config.Hardware_Config < 2)
        ? "L;H;PWM-L;PWM-H;Pin;EK;Eben;Dauer;Aus"
        : "L;H;EK;Dauer;Aus";
}
static uint8_t einSelMax() { return (config.Hardware_Config < 2) ? 8 : 4; }
static uint8_t einSelVal(int cfg) {
    uint8_t t = einTyp(cfg);
    if (config.Hardware_Config >= 2) {
        // V3: PWM/Pin → Deaktiviert (4)
        if (t == 2 || t == 3 || t == 4) return 4;
        // V3 Mapping: 0=L,1=H,5→2=EK,7→3=Dauer,8→4=Aus
        if (t==5) return 2; if (t==7) return 3; if (t>=8) return 4;
        return t; // 0,1
    }
    return t; // V1/V2: vollständig 0-8
}
static int einSelWrite(uint8_t val, int oldCfg) {
    uint8_t nr = einNr(oldCfg);
    if (config.Hardware_Config >= 2) {
        // V3 Mapping Auswahl→Typ: 0=L,1=H,2=EK,3=Dauer,4=Aus
        static const uint8_t v3map[] = {0,1,5,7,8};
        uint8_t t = (val < 5) ? v3map[val] : 8;
        return setEin(t, nr);
    }
    return setEin(val, nr); // V1/V2: direkt
}

// ======== CRSF: Parameter senden =====================================
static void crsfSendParam(uint8_t idx) {
    char buf[48];

    if(idx==0) {
        crsf.send_param_response_CRSF_FOLDER(0,0,"",
            {1,2,16,22,28,34,40,46,52,58,64,74});
    }
    else if(idx==1) {
        snprintf(buf,sizeof(buf),"v0.70 %s",hwVersionStr());
        crsf.send_param_response_CRSF_INFO(1,0,"Version",buf);
    }

    // ── Motor (2..15) ─────────────────────────────────────────────────
    else if(idx==2) crsf.send_param_response_CRSF_FOLDER(2,0,"Motor",
        {3,4,5,6,7,8,9,10,11,12,13,14,15});
    else if(idx==3) crsf.send_param_response_CRSF_TEXT_SELECTION(3,2,
        "Motor Mode","Eine Richtung;Zwei Richtungen",
        (uint8_t)constrain(config.throttle_mode,0,1),0,1);
    else if(idx==4) crsf.send_param_response_CRSF_TEXT_SELECTION(4,2,
        "Motor EIN Modus","Normal;Toggle",
        (uint8_t)constrain(config.engine_on_toggle,0,1),0,1);
    else if(idx==5) crsf.send_param_response_CRSF_TEXT_SELECTION(5,2,
        "Quelle Motor", einSelStr(),
        einSelVal(config.Source_Start_Sound[0]),0,einSelMax());
    else if(idx==6) {
        uint8_t t=einTyp(config.Source_Start_Sound[0]);
        bool noPin=(config.Hardware_Config>=2&&(t==2||t==3||t==4));
        if(t==7||t==8||noPin)
            crsf.send_param_response_CRSF_INFO(6,2,"Kanal EIN",
                (t==7||noPin)?"Dauerbetrieb/Deakt.":"Deaktiviert");
        else crsf.send_param_response_CRSF_UINT8(6,2,"Kanal Nr EIN",
            einNr(config.Source_Start_Sound[0]),1,einMaxNr(t),"");
    }
    else if(idx==7) crsf.send_param_response_CRSF_TEXT_SELECTION(7,2,
        "Quelle Speed", spdSelStr(), spdSelVal(),0,spdSelMax());
    else if(idx==8) {
        uint8_t t=spdTyp(config.Source_Speed_Sound_0);
        bool deakt=(t==2)||(config.Hardware_Config>=2&&t==1);
        if(deakt) crsf.send_param_response_CRSF_INFO(8,2,"Kanal Speed","Deaktiviert");
        else crsf.send_param_response_CRSF_UINT8(8,2,"Kanal Nr Speed",
            spdNr(config.Source_Speed_Sound_0),1,(t==1)?6:16,"");
    }
    else if(idx==9)  crsf.send_param_response_CRSF_UINT8(9, 2,"Volumen Motor",
        (uint8_t)constrain(config.Volumen_Sound[0],0,200),0,200,"");
    else if(idx==10) crsf.send_param_response_CRSF_UINT8(10,2,"Drehzahl min %",
        (uint8_t)constrain(config.Min_Speed_Sound_0,0,200),0,200,"%");
    else if(idx==11) crsf.send_param_response_CRSF_UINT8(11,2,"Drehzahl max /2",
        (uint8_t)constrain(config.Max_Speed_Sound_0/2,50,255),50,255,"%");
    else if(idx==12) crsf.send_param_response_CRSF_UINT8(12,2,"Motor aus Standgas",
        (uint8_t)constrain(config.shutdowndelay,0,60),0,60,"s");
    else if(idx==13) crsf.send_param_response_CRSF_UINT8(13,2,"Motor Rampe %/s",
        (uint8_t)constrain(config.throttle_ramp,0,50),0,50,"");
    else if(idx==14) crsf.send_param_response_CRSF_UINT8(14,2,"Standgas Totband",
        (uint8_t)constrain(config.throttle_dead_band,0,50),0,50,"%");
    else if(idx==15) crsf.send_param_response_CRSF_INFO(15,2,"Hardware",hwVersionStr());

    // ── Sound 1-8: fi=16+(s-1)*6, endet bei 63 ────────────────────────
    else if(idx>=16&&idx<=63) {
        uint8_t s=(idx-16)/6+1, sub=(idx-16)%6, fi=16+(s-1)*6;
        switch(sub) {
        case 0:
            snprintf(buf,sizeof(buf),"Sound %d",s);
            crsf.send_param_response_CRSF_FOLDER(fi,0,buf,
                {(uint8_t)(fi+1),(uint8_t)(fi+2),
                 (uint8_t)(fi+3),(uint8_t)(fi+4),(uint8_t)(fi+5)});
            break;
        case 1:
            crsf.send_param_response_CRSF_TEXT_SELECTION(fi+1,fi,
                "Quelle",einSelStr(),
                einSelVal(config.Source_Start_Sound[s]),0,einSelMax());
            break;
        case 2: {
            uint8_t t=einTyp(config.Source_Start_Sound[s]);
            bool noPin=(config.Hardware_Config>=2&&(t==2||t==3||t==4));
            if(t==7||t==8||noPin)
                crsf.send_param_response_CRSF_INFO(fi+2,fi,"Kanal Nr",
                    t==7?"Dauerbetrieb":"Deaktiviert");
            else crsf.send_param_response_CRSF_UINT8(fi+2,fi,"Kanal Nr",
                einNr(config.Source_Start_Sound[s]),1,einMaxNr(t),"");
            break;
        }
        case 3: crsf.send_param_response_CRSF_UINT8(fi+3,fi,"Volumen",
            (uint8_t)constrain(config.Volumen_Sound[s],0,200),0,200,""); break;
        case 4: crsf.send_param_response_CRSF_TEXT_SELECTION(fi+4,fi,
            "Wiedergabe Mode","Normal;Loop;Tippbetrieb",
            (uint8_t)constrain(config.Mode_Sound[s],0,2),0,2); break;
        case 5: crsf.send_param_response_CRSF_TEXT_SELECTION(fi+5,fi,
            "Test Sound","Aus;Ein",testSoundActive[s]?1:0,0,1); break;
        }
    }

    // ── Einstellungen (64..73) ────────────────────────────────────────
    else if(idx==64) crsf.send_param_response_CRSF_FOLDER(64,0,"Einstellungen",
        {65,66,67,68,69,70,71,72,73});
    else if(idx==65) crsf.send_param_response_CRSF_TEXT_SELECTION(65,64,
        "RC-System","FrSky;FlySky;ELRS SBUS;Hott;ELRS CRSF",
        (uint8_t)constrain(config.Einkanal_RC_System,0,4),0,4);
    else if(idx==66) crsf.send_param_response_CRSF_UINT8(66,64,"Modul Adresse",
        (uint8_t)constrain(config.modul_adress,0,20),0,20,"");
    else if(idx==67) {
        uint8_t v=(config.Einkanal_Channel==999)?255:(uint8_t)constrain(config.Einkanal_Channel,0,15);
        crsf.send_param_response_CRSF_UINT8(67,64,"Einkanal Kanal (255=aus)",v,0,255,"");
    }
    else if(idx==68) {
        uint8_t cur=0;
        if(config.Einkanal_mode==0)cur=0;
        else if(config.Einkanal_mode>=10&&config.Einkanal_mode<=13)
            cur=(uint8_t)(config.Einkanal_mode-9);
        crsf.send_param_response_CRSF_TEXT_SELECTION(68,64,
            "Einkanal Mode","Normal;WM0;WM1;WM2;WM3",cur,0,4);
    }
    else if(idx==69) {
        uint8_t v=(config.Source_Ebenen_Um_Kanal==999)?255:(uint8_t)constrain(config.Source_Ebenen_Um_Kanal,0,250);
        crsf.send_param_response_CRSF_UINT8(69,64,"Ebenen Um-Kanal (255=aus)",v,0,255,"");
    }
    else if(idx==70) {
        uint8_t v=(config.Source_Ebenen_Kanal==999)?255:(uint8_t)constrain(config.Source_Ebenen_Kanal,0,250);
        crsf.send_param_response_CRSF_UINT8(70,64,"Ebenen Kanal (255=aus)",v,0,255,"");
    }
    else if(idx==71) crsf.send_param_response_CRSF_TEXT_SELECTION(71,64,
        "Hardware Config","V1;V2;V3",
        (uint8_t)constrain(config.Hardware_Config,0,2),0,2);
    else if(idx==72) crsf.send_param_response_CRSF_UINT8(72,64,"PWM min (/16 us)",
        (uint8_t)(constrain(config.PWM_scale_min,0,4080)/16),0,255,"");
    else if(idx==73) crsf.send_param_response_CRSF_UINT8(73,64,"PWM max (/16 us)",
        (uint8_t)(constrain(config.PWM_scale_max,0,4080)/16),0,255,"");

    // ── Gerätename (74) ───────────────────────────────────────────────
    else if(idx==74) {
        snprintf(buf,sizeof(buf),"%s (Web aendern)",config.Device_Name);
        crsf.send_param_response_CRSF_INFO(74,0,"Geraetename",buf);
    }
}

// ======== CRSF: Parameter schreiben ==================================
static void crsfWriteParam(uint8_t idx, uint8_t val) {
    if     (idx==3) {config.throttle_mode=constrain(val,0,1);markDirty();}
    else if(idx==4) {config.engine_on_toggle=constrain(val,0,1);markDirty();}
    else if(idx==5) {config.Source_Start_Sound[0]=einSelWrite(val,config.Source_Start_Sound[0]);markDirty();}
    else if(idx==6) {uint8_t t=einTyp(config.Source_Start_Sound[0]);config.Source_Start_Sound[0]=setEin(t,val);markDirty();}
    else if(idx==7) {
        // V3: val 0=BUS,1=Deakt  V1/V2: val 0=BUS,1=PWM,2=Deakt
        uint8_t nr=spdNr(config.Source_Speed_Sound_0);
        uint8_t realTyp=(config.Hardware_Config>=2&&val==1)?2:val;
        config.Source_Speed_Sound_0=setSpd(realTyp,nr); markDirty();
    }
    else if(idx==8) {config.Source_Speed_Sound_0=setSpd(spdTyp(config.Source_Speed_Sound_0),val);markDirty();}
    else if(idx==9) {config.Volumen_Sound[0]=constrain(val,0,200);markDirty();}
    else if(idx==10){config.Min_Speed_Sound_0=constrain(val,0,200);markDirty();}
    else if(idx==11){config.Max_Speed_Sound_0=constrain((int)val*2,100,510);markDirty();}
    else if(idx==12){config.shutdowndelay=constrain(val,0,60);markDirty();}
    else if(idx==13){config.throttle_ramp=constrain(val,0,50);markDirty();}
    else if(idx==14){config.throttle_dead_band=constrain(val,0,50);markDirty();}
    else if(idx>=16&&idx<=63) {
        uint8_t s=(idx-16)/6+1, sub=(idx-16)%6;
        if(s<1||s>8)return;
        if     (sub==1){config.Source_Start_Sound[s]=einSelWrite(val,config.Source_Start_Sound[s]);markDirty();}
        else if(sub==2){config.Source_Start_Sound[s]=setEin(einTyp(config.Source_Start_Sound[s]),val);markDirty();}
        else if(sub==3){config.Volumen_Sound[s]=constrain(val,0,200);markDirty();}
        else if(sub==4){config.Mode_Sound[s]=constrain(val,0,2);markDirty();}
        else if(sub==5){if(val==1)Sound_on_web[s]=true;testSoundActive[s]=(val==1);}
    }
    else if(idx==65){config.Einkanal_RC_System=constrain(val,0,4);markDirty();}
    else if(idx==66){config.modul_adress=constrain(val,0,20);markDirty();}
    else if(idx==67){config.Einkanal_Channel=(val==255)?999:constrain(val,0,15);markDirty();}
    else if(idx==68){config.Einkanal_mode=(val==0)?0:constrain((int)val+9,10,13);markDirty();}
    else if(idx==69){config.Source_Ebenen_Um_Kanal=(val==255)?999:(int)val;markDirty();}
    else if(idx==70){config.Source_Ebenen_Kanal=(val==255)?999:(int)val;markDirty();}
    else if(idx==71){config.Hardware_Config=constrain(val,0,2);markDirty();
                     Serial.printf("Hardware Config -> %d (Neustart noetig)\n",config.Hardware_Config);}
    else if(idx==72){config.PWM_scale_min=constrain((int)val*16,0,4080);markDirty();}
    else if(idx==73){config.PWM_scale_max=constrain((int)val*16,0,4080);markDirty();}
}

// ======== Setup ======================================================
void setup() {
    Serial.begin(115200);
    loadConfig();

    strncpy(ssid,    config.WiFi_SSID,    sizeof(ssid)-1);    ssid[sizeof(ssid)-1]='\0';
    strncpy(password,config.WiFi_Password,sizeof(password)-1); password[sizeof(password)-1]='\0';

    // CRSF oder SBUS starten
    if (config.Einkanal_RC_System == 4) {
        crsf.init_crsf(&Serial2, 16, 17);
        Serial.println("CRSF (RX=16, TX=17)");
    } else {
        sbus_rx.Begin();
        Serial.println("SBUS gestartet");
    }

    // GPIO-Pins je nach Hardware-Version
    switch (config.Hardware_Config) {
        case 0: { uint8_t p[]={16,17,22, 0, 2, 4}; memcpy(Input_Pin,p,6); break; }
        case 1: { uint8_t p[]={16,17,14,27,32,33}; memcpy(Input_Pin,p,6); break; }
        default: break;  // V3: Input_Pin nicht verwendet
    }

    pinMode(WifiPin, INPUT_PULLUP);

    // INPUT_PULLUP für Eingangs-Pins (V1/V2 only, GPIO16/17 bei CRSF überspringen)
    if (config.Hardware_Config < 2) {
        for (uint8_t i = 2; i < 6; i++) {
            if (config.Einkanal_RC_System == 4 &&
                (Input_Pin[i] == 16 || Input_Pin[i] == 17)) continue;
            pinMode(Input_Pin[i], INPUT_PULLUP);
        }
    }
    // CRSF RX/TX-Pins nie als INPUT_PULLUP (gilt für alle Versionen)
    // GPIO16/17 werden durch crsf.init_crsf() / sbus_rx.Begin() konfiguriert

    SDCardInit();
    LoadFiles();

    if (!digitalRead(WifiPin)) {
        Serial.println("AP Modus...");
        WebServerManager::begin(ssid, password);
    }

    Einkanal_RC_System_boot = config.Einkanal_RC_System;
    sprintf(versionString, "%d.%02d", Version/100, Version%100);
    saveConfig();

    Serial.printf("ESP32-RC-Sound v0.70 – Hardware: %s\n", hwVersionStr());
}

// ======== Loop =======================================================
void loop() {
    currentTime = millis();
    if (!digitalRead(WifiPin)) WebServerManager::Webpage();

    if (Einkanal_RC_System_boot == 4) {
        crsf.read_packets(0);
        for (int i=0;i<16;i++) channel_output[i]=crsf.get_crfs_channels(i);
        if(channel_output[0]>0){BUS_OK=true;lastCrsfPacket=millis();}
        checkCrsfTimeout();
        static unsigned long lastTelem=0;
        if(millis()-lastTelem>=100){lastTelem=millis();crsf.send_tele_Battery_Sensor(0,0,0,100);}
        if(crsf.getDeviceInfoReplyPending()){
            crsf.setDeviceInfoReplyPending(false);
            char dn[24]; snprintf(dn,sizeof(dn),"%s@%d",config.Device_Name,config.modul_adress);
            crsf.send_device_info(dn,CRSF_PARAM_COUNT);
        }
        if(crsf.getDeviceReadReplyPending()){
            crsf.setDeviceReadReplyPending(false);
            crsfSendParam(crsf.getParamReadIndex());
        }
        if(crsf.getDeviceWriteReplyPending()){
            uint8_t wi=crsf.getParamWriteIndex(),wv=crsf.getParamWriteValue();
            crsf.setDeviceWriteReplyPending(false);
            crsfWriteParam(wi,wv); crsfSendParam(wi);
        }
        if(crsf.getDeviceCommandReplyPending()){
            crsf.setDeviceCommandReplyPending(false);
            einkanalFunctionCRSF();
        }
    } else {
        if(sbus_rx.Read()){
            sbus_data=sbus_rx.data(); BUS_OK=true;
            if(!sbus_data.failsafe){
                if(config.Einkanal_Channel<16) einkanalFunction(sbus_data.ch[config.Einkanal_Channel]);
            } else BUS_OK=false;
            for(int i=0;i<16;i++) channel_output[i]=sbus_data.ch[i];
        }
    }

    if(configDirty&&(millis()-configDirtyMs)>=2000UL) saveConfigForce();

    Config();
    if(currentTime<5000) return;
    ebenenFunction();
    for(uint8_t i=1;i<=8;i++) handleSound(i,Sounds[i]);

    // Motor Toggle
    if(Sound_on[0]&&!Sound_on_Motor_state&&config.engine_on_toggle){Sound_on_Motor=!Sound_on_Motor;Sound_on_Motor_state=true;}
    else if(!Sound_on[0]&&Sound_on_Motor_state&&config.engine_on_toggle) Sound_on_Motor_state=false;
    else if(!config.engine_on_toggle) Sound_on_Motor=Sound_on[0];

    bool mw=Sound_on_Motor||Sound_on_web[0];
    if((mw&&!engine_break&&!Sound_play[0])||(throttle>0&&engine_break&&!Sound_play[0])||(throttle>0&&Sound_play[0])){Sound_play[0]=true;shutdown_timer=millis();}
    if(!mw){Sound_play[0]=false;engine_break=false;}
    if(config.shutdowndelay>0&&Sound_play[0]&&(millis()-shutdown_timer)>((unsigned long)config.shutdowndelay*1000UL)){Sound_play[0]=false;engine_break=true;}

    switch(engine_State){
        case OFF:    if(Sound_play[0]){Sound_start.Volume=config.Volumen_Sound[0];I2SAudio.Play(&Sound_start);engine_State=STARTING;} break;
        case STARTING: if(!Sound_start.Playing){throttle=0;last_throttle=0;Sound_loop.RepeatForever=true;Sound_loop.Volume=config.Volumen_Sound[0];I2SAudio.Play(&Sound_loop);engine_State=RUNNING;} break;
        case RUNNING:  Sound_loop.Volume=config.Volumen_Sound[0]; if(!Sound_play[0]){throttle=0;if(last_throttle==0){Sound_loop.RepeatForever=false;I2SAudio.Stop(&Sound_loop);engine_State=STOPPING;}} break;
        case STOPPING: if(!Sound_loop.Playing){Sound_shut.Volume=config.Volumen_Sound[0];I2SAudio.Play(&Sound_shut);engine_State=OFF;} break;
    }
    throttle=ramp_throttle(throttle);
    if(config.Min_Speed_Sound_0!=config.Max_Speed_Sound_0)
        Sound_loop.Speed=floatMap(throttle,0,100,config.Min_Speed_Sound_0,config.Max_Speed_Sound_0)/100.0f;
}

// ======== Sound ======================================================
void handleSound(uint8_t idx, XT_Wav_Class* snd) {
    if(!snd)return;
    if(Sound_on[idx]||Sound_on_web[idx]){
        snd->Volume=config.Volumen_Sound[idx];
        if(!Sound_play[idx]){Sound_play[idx]=true;snd->LoadWavFile();if(config.Mode_Sound[idx]==1)snd->RepeatForever=true;I2SAudio.Play(snd);Sound_on_web[idx]=false;}
        else snd->Volume=config.Volumen_Sound[idx];
    }else{
        snd->RepeatForever=false;
        if(config.Mode_Sound[idx]==2&&Sound_play[idx])I2SAudio.Stop(snd);
        if(!snd->Playing&&Sound_play[idx]){snd->UnLoadWavFile();Sound_play[idx]=false;}
    }
}

// ======== Config: Quellen auswerten ==================================
void Config() {
    bool isV3 = (config.Hardware_Config >= 2);

    for (int x=0; x<=8; x++) {
        int src = config.Source_Start_Sound[x];
        if      (src>=0   && src<=15)  Sound_on[x] = BUS_OK && (channel_output[src] < 624);
        else if (src>=20  && src<=35)  Sound_on[x] = BUS_OK && (channel_output[src-20] > 1424);
        else if (src>=40  && src<=45)  {
            // PWM Low – nur V1/V2
            if (!isV3) { uint8_t pi=src-40; attachPWM_ISR(pi); unsigned int d=PWM_pulse_width[pi]; Sound_on[x]=(d<2600)&&(map(d,config.PWM_scale_min,config.PWM_scale_max,0,100)<25); }
            else Sound_on[x]=false;
        }
        else if (src>=50  && src<=55)  {
            // PWM High – nur V1/V2
            if (!isV3) { uint8_t pi=src-50; attachPWM_ISR(pi); unsigned int d=PWM_pulse_width[pi]; Sound_on[x]=(d<2600)&&(map(d,config.PWM_scale_min,config.PWM_scale_max,0,100)>75); }
            else Sound_on[x]=false;
        }
        else if (src>=60  && src<=65)  {
            // GPIO Pin – nur V1/V2
            if (!isV3) { uint8_t pi=src-60; Sound_on[x]=(pi<6)&&!digitalRead(Input_Pin[pi]); }
            else Sound_on[x]=false;
        }
        else if (src>=70  && src<=77)  Sound_on[x] = BUS_OK && bitRead(einkanal_Data, src-70);
        else if (src>=80  && src<=103) Sound_on[x] = ((src-80)==ebenenkanal_Data);
        else if (src==200)             Sound_on[x] = true;
        else                           Sound_on[x] = false;
    }

    // Motor Speed
    int spd = config.Source_Speed_Sound_0;
    if      (spd>=0 && spd<=15)        throttle = map(channel_output[spd], 82, 1900, 0, 100);
    else if (!isV3 && spd>=20 && spd<=25) { attachPWM_ISR(spd-20); throttle = map((long)PWM_pulse_width[spd-20], config.PWM_scale_min, config.PWM_scale_max, 0, 100); }
    else                               throttle = 0;
    throttle = constrain(throttle, 0, 100);

    if (config.throttle_mode) {
        if      (throttle > 50+config.throttle_dead_band) throttle=map((long)throttle,50+config.throttle_dead_band,100,0,100);
        else if (throttle < 50-config.throttle_dead_band) throttle=map((long)throttle,50-config.throttle_dead_band,0,0,100);
        else throttle=0;
    } else {
        throttle=(throttle>config.throttle_dead_band)?map((long)throttle,config.throttle_dead_band,100,0,100):0;
    }
    throttle = constrain(throttle, 0, 100);
}

// ======== Hilfsfunktionen ============================================
float floatMap(float x,float a,float b,float c,float d){if(b==a)return c;return(x-a)*(d-c)/(b-a)+c;}
void SDCardInit(){pinMode(SD_CS,OUTPUT);digitalWrite(SD_CS,HIGH);if(!SD.begin(SD_CS))Serial.println("FEHLER: SD!");}
void LoadFiles(){Sound_loop.LoadWavFile();Sound_shut.LoadWavFile();Sound_start.LoadWavFile();}

float ramp_throttle(float tn){
    unsigned long rt=currentTime-previousTime_ramp; previousTime_ramp=currentTime;
    if(rt>1000)rt=1000; float step=(config.throttle_ramp/1000.0f)*(float)rt;
    if(tn>last_throttle){float s=last_throttle+step;tn=(s<tn)?s:tn;}
    else{float s=last_throttle-step;tn=(s>tn)?s:tn;}
    last_throttle=tn; return tn;
}

// ======== Einkanal SBUS ==============================================
void einkanalFunction(uint16_t ch) {
    einkanal_Data=ch;
    if(config.Einkanal_mode==0){einkanal_Data/=8;}
    else if(config.Einkanal_mode<=9){
        if(config.Einkanal_RC_System==0)einkanal_Data/=8;
        else if(config.Einkanal_RC_System==1){einkanal_Data=constrain(einkanal_Data,206,1837);einkanal_Data=((einkanal_Data-206)*10+20)/64;}
        else if(config.Einkanal_RC_System==2){float v=((float)einkanal_Data-172.0f+1.5f)*0.155677655677655f;einkanal_Data=(uint16_t)v;}
    }else{
        uint16_t n=0;uint8_t v=0;
        switch(config.Einkanal_RC_System){
            case 0:n=(ch>=172)?(ch-172+1):0;v=(uint8_t)(n>>4);break;
            case 1:n=(ch>=220)?(ch-220):0;v=(uint8_t)((n+(n>>6))>>4);break;
            case 2:n=(ch>=172)?(ch-172):0;v=(uint8_t)(n>>4);break;
            case 3:n=(ch>=205)?(ch-205):0;v=(uint8_t)(n>>4);break;
        }
        uint8_t addr=(v>>4)&0b11,sw=(v>>1)&0b111,st=v&0b1;
        if(addr==(uint8_t)(config.Einkanal_mode-10))bitWrite(einkanal_SpeicherWM,sw,st);
        einkanal_Data=einkanal_SpeicherWM;
    }
}

// ======== Einkanal CRSF ==============================================
void einkanalFunctionCRSF(){
    uint8_t wmc=crsf.get_crfs_buffer(5),cmd=crsf.get_crfs_buffer(6);
    if(wmc!=Multiswitch)return;
    switch(cmd){
        case MWset4:{uint8_t a=crsf.get_crfs_buffer(7);if(a==(uint8_t)config.modul_adress){uint16_t s=((uint16_t)crsf.get_crfs_buffer(8)<<8)|crsf.get_crfs_buffer(9);einkanal_Data=compressSwitches(s);}break;}
        case MWset4m:{uint8_t c=min((uint8_t)crsf.get_crfs_buffer(7),(uint8_t)7);for(uint8_t i=0;i<c;i++){uint8_t a=crsf.get_crfs_buffer(8+(3*i));if(a==(uint8_t)config.modul_adress){uint16_t s=((uint16_t)crsf.get_crfs_buffer(9+(3*i))<<8)|crsf.get_crfs_buffer(10+(3*i));einkanal_Data=compressSwitches(s);}}break;}
        case MWset:{uint8_t a=crsf.get_crfs_buffer(7);if(a==(uint8_t)config.modul_adress)einkanal_Data=crsf.get_crfs_buffer(8);break;}
        case MWprop:break;
    }
}
uint8_t compressSwitches(uint16_t s){uint8_t r=0;for(uint8_t i=0;i<8;i++)r|=((s>>(i*2))&0x1)<<i;return r;}

// ======== Ebenen-Umschaltung =========================================
void ebenenFunction(){
    Source_Ebenen_Um_Kanal_wert=1; Source_Ebenen_Kanal_wert=0;
    int um=config.Source_Ebenen_Um_Kanal,ek=config.Source_Ebenen_Kanal;
    if(um>=0&&um<=15)  Source_Ebenen_Um_Kanal_wert=map(channel_output[um],207,1833,0,100);
    else if(um>=20&&um<=25){attachPWM_ISR(um-20);Source_Ebenen_Um_Kanal_wert=map((long)PWM_pulse_width[um-20],938,2063,0,100);}
    if(ek>=0&&ek<=15)  Source_Ebenen_Kanal_wert=map(channel_output[ek],207,1833,0,100);
    else if(ek>=20&&ek<=25){attachPWM_ISR(ek-20);Source_Ebenen_Kanal_wert=map((long)PWM_pulse_width[ek-20],938,2063,0,100);}
    Source_Ebenen_Um_Kanal_wert/=33;
    struct{int l,h;uint16_t e;}r[]={{0,5,0},{6,18,1},{19,30,2},{31,43,3},{44,55,99},{56,68,4},{69,80,5},{81,93,6},{94,110,7}};
    for(auto& x:r){if(Source_Ebenen_Kanal_wert>=x.l&&Source_Ebenen_Kanal_wert<=x.h){ebenenkanal_Data=(x.e==99)?99:(Source_Ebenen_Um_Kanal_wert*8+x.e);break;}}
}
