/*
 * config.cpp  -  ESP32-RC-Sound v0.70
 */

#include "config.h"
#include <Preferences.h>

ConfigData    config;
bool          configDirty   = false;
unsigned long configDirtyMs = 0;

static constexpr const char* NVS_NS = "rcsound";

void markDirty() { configDirty = true; configDirtyMs = millis(); }

void Reset_all() {
    for (int i = 0; i < 9; i++) {
        config.Source_Start_Sound[i] = 999;
        config.Volumen_Sound[i]      = 100;
        config.Mode_Sound[i]         = 0;
    }
    config.Source_Speed_Sound_0   = 999;
    config.throttle_mode          = 0;
    config.Min_Speed_Sound_0      = 100;
    config.Max_Speed_Sound_0      = 300;
    config.shutdowndelay          = 0;
    config.engine_on_toggle       = 0;
    config.throttle_ramp          = 20;
    config.throttle_dead_band     = 10;
    config.Einkanal_Channel       = 999;
    config.Einkanal_mode          = 0;
    config.Einkanal_RC_System     = 0;
    config.modul_adress           = 0;
    config.Source_Ebenen_Um_Kanal = 999;
    config.Source_Ebenen_Kanal    = 999;
    config.Hardware_Config        = 0;
    config.PWM_scale_min          = 1000;
    config.PWM_scale_max          = 2000;
    strncpy(config.WiFi_SSID,     "ESP32-RC-Sound", sizeof(config.WiFi_SSID)     - 1);
    strncpy(config.WiFi_Password, "123456789",      sizeof(config.WiFi_Password) - 1);
    strncpy(config.WiFi_IP,       "192.168.1.1",    sizeof(config.WiFi_IP)       - 1);
    strncpy(config.Device_Name,   "RC-Sound",       sizeof(config.Device_Name)   - 1);
    config.WiFi_SSID[sizeof(config.WiFi_SSID)-1]         = '\0';
    config.WiFi_Password[sizeof(config.WiFi_Password)-1] = '\0';
    config.WiFi_IP[sizeof(config.WiFi_IP)-1]             = '\0';
    config.Device_Name[sizeof(config.Device_Name)-1]     = '\0';
    configDirty = true;
}

static void applyPreset(const int srcs[9], int spd) {
    for (int i = 0; i < 9; i++) { config.Source_Start_Sound[i]=srcs[i]; config.Volumen_Sound[i]=100; config.Mode_Sound[i]=0; }
    config.Source_Speed_Sound_0=spd; config.Min_Speed_Sound_0=100; config.Max_Speed_Sound_0=300;
    config.shutdowndelay=0; config.engine_on_toggle=0; config.Einkanal_Channel=999;
    config.Source_Ebenen_Um_Kanal=999; config.Source_Ebenen_Kanal=999;
    config.throttle_ramp=20; config.throttle_dead_band=10;
    config.PWM_scale_min=1000; config.PWM_scale_max=2000;
    configDirty=true;
}
void set_sbus() { const int s[]={10,11,12,13,14,999,999,999,999}; applyPreset(s,1);  }
void set_pwm()  { const int s[]={41,42,43,44,45,999,999,999,999}; applyPreset(s,20); }
void set_pin()  { const int s[]={61,62,63,64,999,999,999,999,999}; applyPreset(s,999); }

void loadConfig() {
    Reset_all(); configDirty=false;
    Preferences p; p.begin(NVS_NS,true);
    if(!p.isKey("sss0")){ p.end(); configDirty=true; return; }
    char key[8];
    for(int i=0;i<9;i++){
        snprintf(key,sizeof(key),"sss%d",i); config.Source_Start_Sound[i]=p.getInt(key,999);
        snprintf(key,sizeof(key),"vs%d",i);  config.Volumen_Sound[i]=p.getInt(key,100);
        snprintf(key,sizeof(key),"ms%d",i);  config.Mode_Sound[i]=p.getInt(key,0);
    }
    config.Source_Speed_Sound_0   = p.getInt("spd",  999);
    config.throttle_mode          = p.getInt("thrm", 0);
    config.Min_Speed_Sound_0      = p.getInt("minsp",100);
    config.Max_Speed_Sound_0      = p.getInt("maxsp",300);
    config.shutdowndelay          = p.getInt("shdly",0);
    config.engine_on_toggle       = p.getInt("entog",0);
    config.throttle_ramp          = p.getInt("thrr", 20);
    config.throttle_dead_band     = p.getInt("thrdb",10);
    config.Einkanal_Channel       = p.getInt("ekch", 999);
    config.Einkanal_mode          = p.getInt("ekmo", 0);
    config.Einkanal_RC_System     = p.getInt("ekrc", 0);
    config.modul_adress           = p.getInt("madr", 0);
    config.Source_Ebenen_Um_Kanal = p.getInt("ebum", 999);
    config.Source_Ebenen_Kanal    = p.getInt("ebk",  999);
    config.Hardware_Config        = p.getInt("hwcfg",0);
    config.PWM_scale_min          = p.getInt("pwmin",1000);
    config.PWM_scale_max          = p.getInt("pwmax",2000);
    String ssid=p.getString("ssid","ESP32-RC-Sound");
    String pass=p.getString("pass","123456789");
    String ip  =p.getString("wip", "192.168.1.1");
    String dnam=p.getString("dnam","RC-Sound");
    p.end();
    strncpy(config.WiFi_SSID,    ssid.c_str(),sizeof(config.WiFi_SSID)-1);
    strncpy(config.WiFi_Password,pass.c_str(),sizeof(config.WiFi_Password)-1);
    strncpy(config.WiFi_IP,      ip.c_str(),  sizeof(config.WiFi_IP)-1);
    strncpy(config.Device_Name,  dnam.c_str(),sizeof(config.Device_Name)-1);
    config.WiFi_SSID[sizeof(config.WiFi_SSID)-1]='\0';
    config.WiFi_Password[sizeof(config.WiFi_Password)-1]='\0';
    config.WiFi_IP[sizeof(config.WiFi_IP)-1]='\0';
    config.Device_Name[sizeof(config.Device_Name)-1]='\0';
    configDirty=false;
    Serial.println("Config aus NVS geladen.");
}

void saveConfig() { if(!configDirty)return; saveConfigForce(); }

void saveConfigForce() {
    Preferences p; p.begin(NVS_NS,false);
    char key[8];
    for(int i=0;i<9;i++){
        snprintf(key,sizeof(key),"sss%d",i); p.putInt(key,config.Source_Start_Sound[i]);
        snprintf(key,sizeof(key),"vs%d",i);  p.putInt(key,config.Volumen_Sound[i]);
        snprintf(key,sizeof(key),"ms%d",i);  p.putInt(key,config.Mode_Sound[i]);
    }
    p.putInt("spd",  config.Source_Speed_Sound_0);
    p.putInt("thrm", config.throttle_mode);
    p.putInt("minsp",config.Min_Speed_Sound_0);
    p.putInt("maxsp",config.Max_Speed_Sound_0);
    p.putInt("shdly",config.shutdowndelay);
    p.putInt("entog",config.engine_on_toggle);
    p.putInt("thrr", config.throttle_ramp);
    p.putInt("thrdb",config.throttle_dead_band);
    p.putInt("ekch", config.Einkanal_Channel);
    p.putInt("ekmo", config.Einkanal_mode);
    p.putInt("ekrc", config.Einkanal_RC_System);
    p.putInt("madr", config.modul_adress);
    p.putInt("ebum", config.Source_Ebenen_Um_Kanal);
    p.putInt("ebk",  config.Source_Ebenen_Kanal);
    p.putInt("hwcfg",config.Hardware_Config);
    p.putInt("pwmin",config.PWM_scale_min);
    p.putInt("pwmax",config.PWM_scale_max);
    p.putString("ssid",config.WiFi_SSID);
    p.putString("pass",config.WiFi_Password);
    p.putString("wip", config.WiFi_IP);
    p.putString("dnam",config.Device_Name);
    p.end();
    configDirty=false;
    Serial.println("Config in NVS gespeichert.");
}
