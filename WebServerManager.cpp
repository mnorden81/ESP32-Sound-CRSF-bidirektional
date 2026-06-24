#include "WebServerManager.h"
#include "config.h"
#include <Arduino.h>
#include "XT_I2S_Audio.h"

WebServer  WebServerManager::server(80);
int        WebServerManager::Menu        = 0;
String     WebServerManager::valueString = "";

extern bool          Sound_on_web[9];
extern uint16_t      channel_output[16];
extern volatile unsigned int PWM_pulse_width[6];
extern XT_Wav_Class  Sound_loop;
extern XT_Wav_Class  Sound_shut;
extern XT_Wav_Class  Sound_start;
extern XT_Wav_Class  Sound1, Sound2, Sound3, Sound4;
extern XT_Wav_Class  Sound5, Sound6, Sound7, Sound8;
extern char          versionString[6];

// URL-Dekodierung
String WebServerManager::urlDecode(const String& s) {
  String result = "";
  result.reserve(s.length());
  for (int i = 0; i < (int)s.length(); i++) {
    if (s[i] == '+') { result += ' '; }
    else if (s[i] == '%' && i+2 < (int)s.length()) {
      char hex[3] = { s[i+1], s[i+2], 0 };
      result += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else { result += s[i]; }
  }
  return result;
}

// ── AP starten + Routen registrieren ────────────────────────────────────
void WebServerManager::begin(const char* apSsid, const char* apPassword) {
  WiFi.softAP(apSsid, apPassword);
  IPAddress apIP, gw, subnet(255,255,255,0);
  if (!apIP.fromString(config.WiFi_IP)) apIP.fromString("192.168.1.1");
  gw = apIP;
  WiFi.softAPConfig(apIP, gw, subnet);
  Serial.print("AP gestartet, IP: ");
  Serial.println(WiFi.softAPIP());

  // Alle Requests auf einen Handler
  server.onNotFound([]() { WebServerManager::handleRequest(); });
  server.on("/", []()    { WebServerManager::handleRequest(); });
  server.begin();
}

// ── handleClient(): non-blocking, kehrt sofort zurueck ──────────────────
void WebServerManager::Webpage() {
  server.handleClient();
}

// ── Haupt-Request-Handler ────────────────────────────────────────────────
// Bei WebServer.h: URI-Pfad via server.uri(), Parameter via server.arg("key")
// Alle GET-Requests landen hier (onNotFound + on("/"))
void WebServerManager::handleRequest() {
  const String uri = server.uri();

  // ── Navigation ────────────────────────────────────────────────────────
  if (uri == "/next")      { Menu++; }
  if (uri == "/back")      { Menu--; }
  if (uri == "/Sound/on")  { Sound_on_web[Menu] = true; }
  if (uri == "/save")      { markDirty(); saveConfigForce(); }
  if (uri == "/reset")     { Reset_all(); }
  if (uri == "/setsbus")   { set_sbus(); }
  if (uri == "/setpwm")    { set_pwm(); }
  if (uri == "/setpin")    { set_pin(); }

  // ── PWM Skalierung (eigene Route /setPWM?pwmMin=X&pwmMax=Y) ──────────
  if (uri == "/setPWM") {
    if (server.hasArg("pwmMin")) { config.PWM_scale_min = server.arg("pwmMin").toInt(); markDirty(); }
    if (server.hasArg("pwmMax")) { config.PWM_scale_max = server.arg("pwmMax").toInt(); markDirty(); }
  }

  // ── Numerische Parameter (alle via /?KEY=VAL& ) ───────────────────────
  // WebServer.h: server.arg("KEY") liefert den Wert, server.hasArg() prüft Existenz
  if (server.hasArg("VolumenSound"))         { config.Volumen_Sound[Menu]          = server.arg("VolumenSound").toInt();          markDirty(); }
  if (server.hasArg("Drehzahlmin"))          { config.Min_Speed_Sound_0            = server.arg("Drehzahlmin").toInt();           markDirty(); }
  if (server.hasArg("Drehzahlmax"))          { config.Max_Speed_Sound_0            = server.arg("Drehzahlmax").toInt();           markDirty(); }
  if (server.hasArg("shutdowndelay"))        { config.shutdowndelay                = server.arg("shutdowndelay").toInt();         markDirty(); }
  if (server.hasArg("MotorMODE"))            { config.throttle_mode                = server.arg("MotorMODE").toInt();             markDirty(); }
  if (server.hasArg("MotorOnToggle"))        { config.engine_on_toggle             = server.arg("MotorOnToggle").toInt();         markDirty(); }
  if (server.hasArg("SoundON"))              { config.Source_Start_Sound[Menu]     = server.arg("SoundON").toInt();               markDirty(); }
  if (server.hasArg("SoundSPEED"))           { config.Source_Speed_Sound_0         = server.arg("SoundSPEED").toInt();            markDirty(); }
  if (server.hasArg("RCSytem"))              { config.Einkanal_RC_System           = server.arg("RCSytem").toInt();               markDirty(); }
  if (server.hasArg("SbuschannelEinkanal"))  { config.Einkanal_Channel             = server.arg("SbuschannelEinkanal").toInt();   markDirty(); }
  if (server.hasArg("MODULADRESSE"))         { config.modul_adress                 = server.arg("MODULADRESSE").toInt();          markDirty(); }
  if (server.hasArg("EinkanalMode"))         { config.Einkanal_mode                = server.arg("EinkanalMode").toInt();          markDirty(); }
  if (server.hasArg("EbenenUmschaltungKanal")){ config.Source_Ebenen_Um_Kanal     = server.arg("EbenenUmschaltungKanal").toInt();markDirty(); }
  if (server.hasArg("EbenenKanal"))          { config.Source_Ebenen_Kanal          = server.arg("EbenenKanal").toInt();           markDirty(); }
  if (server.hasArg("SoundMODE"))            { config.Mode_Sound[Menu]             = server.arg("SoundMODE").toInt();             markDirty(); }
  if (server.hasArg("ThrottleRamp"))         { config.throttle_ramp                = server.arg("ThrottleRamp").toInt();          markDirty(); }
  if (server.hasArg("ThrottleDeadBand"))     { config.throttle_dead_band           = server.arg("ThrottleDeadBand").toInt();      markDirty(); }
  if (server.hasArg("HardwareConfig"))       { config.Hardware_Config              = server.arg("HardwareConfig").toInt();        markDirty(); }

  // ── String-Parameter (URL-dekodiert) ─────────────────────────────────
  if (server.hasArg("WiFi_SSID")) {
    valueString = urlDecode(server.arg("WiFi_SSID"));
    strncpy(config.WiFi_SSID, valueString.c_str(), sizeof(config.WiFi_SSID)-1);
    config.WiFi_SSID[sizeof(config.WiFi_SSID)-1] = '\0'; markDirty();
  }
  if (server.hasArg("WiFi_Password")) {
    valueString = urlDecode(server.arg("WiFi_Password"));
    strncpy(config.WiFi_Password, valueString.c_str(), sizeof(config.WiFi_Password)-1);
    config.WiFi_Password[sizeof(config.WiFi_Password)-1] = '\0'; markDirty();
  }
  if (server.hasArg("WiFi_IP")) {
    valueString = urlDecode(server.arg("WiFi_IP"));
    strncpy(config.WiFi_IP, valueString.c_str(), sizeof(config.WiFi_IP)-1);
    config.WiFi_IP[sizeof(config.WiFi_IP)-1] = '\0'; markDirty();
  }
  if (server.hasArg("Device_Name")) {
    valueString = urlDecode(server.arg("Device_Name"));
    strncpy(config.Device_Name, valueString.c_str(), sizeof(config.Device_Name)-1);
    config.Device_Name[sizeof(config.Device_Name)-1] = '\0'; markDirty();
  }

  Menu = constrain(Menu, 0, 11);

  // ── HTML senden ───────────────────────────────────────────────────────
  server.send(200, "text/html", buildPage());
}

// ── HTML-Seitenaufbau ────────────────────────────────────────────────────
String WebServerManager::buildPage() {
  String page = "";
  //HTML Seite angezeigen:
  page += "<!DOCTYPE html><html>\n";
  //client.println("<meta http-equiv='refresh' content='5'>");
  page += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  page += "<link rel=\"icon\" href=\"data:,\">\n";
  // CSS zum Stylen der Ein/Aus-Schaltflächen
  // Fühlen Sie sich frei, die Attribute für Hintergrundfarbe und Schriftgröße nach Ihren Wünschen zu ändern
  page += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  page += ".button { border: 4px solid black; color: white; padding: 10px 40px; width: 100%; border-radius: 10px;\n";
  page += "text-decoration: none; font-size: 20px; margin: 2px; cursor: pointer;}\n";
  page += ".slider { -webkit-appearance: none; width: 80%; height: 30px; background: #d3d3d3; outline: none; opacity: 0.7; -webkit-transition: .2s; transition: opacity .2s; }\n";
  page += ".slider::-moz-range-thumb { width: 30px; height: 30px; background: #04AA6D; cursor: pointer; }\n";
  page += ".buttonA {outline: none; cursor: pointer; padding: 14px; margin: 10px; width: 90%; font-family: Verdana, Helvetica, sans-serif; font-size: 20px; background-color: #2222; color: #111; border: 4px solid black; border-radius: 10px; text-align: center;}\n";
  page += ".text1 {font-family: Verdana, Helvetica, sans-serif; font-size: 25px; color: #111; text-align: center; margin-top: 0px; margin-bottom: 0px; }\n";
  page += ".text2 {font-family: Verdana, Helvetica, sans-serif; font-size: 20px; color: #111; text-align: center; margin-top: 0px; margin-bottom: 0px; }\n";
  page += ".text3 {font-family: Verdana, Helvetica, sans-serif; font-size: 15px; color: #111; text-align: center; margin-top: 0px; margin-bottom: 0px; }\n";
  page += ".text4 {font-family: Verdana, Helvetica, sans-serif; font-size: 10px; color: #111; text-align: center; margin-top: 0px; margin-bottom: 0px; }\n";

  page += ".center {text-align: center; }\n";
  page += ".left {text-align: left; }\n";
  page += ".right {text-align: right; }\n";
  
          
  page += ".button1 {background-color: #4CAF50;}\n";
  page += ".button2 {background-color: #ff0000;}\n";
  page += ".button3 {background-color: #4CAF50; float: left; width: 45%;}\n";
  page += ".button4 {background-color: #ff0000; float: right; width: 45%;}\n";
  page += ".button5 {background-color: #ffe453; float: left; width: 45%;}\n";
  page += ".button6 {background-color: #ffe453; float: center; width: 45%;}\n";
  page += ".button7 {background-color: #ffe453; float: right; width: 45%;}\n";
  page += ".disabled {opacity: 0.4; cursor: not-alloed;}\n";
  page += ".textbox {font-size: 25px; text-align: center;}\n";
  page += "</style></head>"; page += "\n";
           
  // Webseiten-Überschrift
  page += "<body>\n";
  page += "<p class=\"text1\" ><b>ESP32 RC-Sound</b></p>"; page += "\n";
  page += "<p class=\"text3\" >Version : \n";
  page += versionString; page += "\n";
  page += "</p>\n";

  switch (Menu) {
    case 0:

  page += "<p class=\"text2\" ><b>Motor Einstellung</b></p>"; page += "\n";
  
  page += "<p><a href=\"/back\"><button class=\"button button3\">Back</button></a>\n";
  page += "<a href=\"/next\"><button class=\"button button4\">Next</button></a></p>\n";

  page += "<br />\n";
  page += "<br />\n";
  page += "<br />\n";
  page += "<br />"; page += "\n";
        
  page += "<p class=\"text2\" >Motor Mode</p>"; page += "\n";

  // Motor Mode  
  page += "<p><select id=\"MotorMODE\" class=\"buttonA\" onchange=\"setmotormode()\">\n";
  page += "<option value=\"0\">Eine Richtung</option>\n";
  page += "<option value=\"1\">Zwei Richtungen</option>\n";
  page += "</select><br></p>\n";

  page += "<script> function setmotormode() { \n";
  page += "var sel = document.getElementById(\"MotorMODE\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?MotorMODE=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.throttle_mode, DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"MotorMODE\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>"; page += "\n";
        
  page += "<p class=\"text2\" >Motor EIN Modus</p>"; page += "\n";

  // Motor Toggle On  
  page += "<p><select id=\"MotorOnToggle\" class=\"buttonA\" onchange=\"setmotorontoggle()\">\n";
  page += "<option value=\"0\">Normal</option>\n";
  page += "<option value=\"1\">Toggle</option>\n";
  page += "</select><br></p>\n";

  page += "<script> function setmotorontoggle() { \n";
  page += "var sel = document.getElementById(\"MotorOnToggle\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?MotorOnToggle=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.engine_on_toggle, DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"MotorOnToggle\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>"; page += "\n";


  page += "<p class=\"text2\" >Quelle Einschalten Motor</p>\n";

  // Quelle Einschalten Motor
  page += "<p><select id=\"SoundON\" class=\"buttonA\" onchange=\"setsoundon()\">\n";
  page += "<optgroup label=\"BUS Kanal Low\">\n";
  page += "<option value=\"0\">BUS Kanal Low 01</option>\n";
  page += "<option value=\"1\">BUS Kanal Low 02</option>\n";
  page += "<option value=\"2\">BUS Kanal Low 03</option>\n";
  page += "<option value=\"3\">BUS Kanal Low 04</option>\n";
  page += "<option value=\"4\">BUS Kanal Low 05</option>\n";
  page += "<option value=\"5\">BUS Kanal Low 06</option>\n";
  page += "<option value=\"6\">BUS Kanal Low 07</option>\n";
  page += "<option value=\"7\">BUS Kanal Low 08</option>\n";
  page += "<option value=\"8\">BUS Kanal Low 09</option>\n";
  page += "<option value=\"9\">BUS Kanal Low 10</option>\n";
  page += "<option value=\"10\">BUS Kanal Low 11</option>\n";
  page += "<option value=\"11\">BUS Kanal Low 12</option>\n";
  page += "<option value=\"12\">BUS Kanal Low 13</option>\n";
  page += "<option value=\"13\">BUS Kanal Low 14</option>\n";
  page += "<option value=\"14\">BUS Kanal Low 15</option>\n";
  page += "<option value=\"15\">BUS Kanal Low 16</option>\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"BUS Kanal High\">\n";
      	    page += "<option value=\"20\">BUS Kanal High 01</option>"; page += "\n";
  page += "<option value=\"21\">BUS Kanal High 02</option>"; page += "\n";
  page += "<option value=\"22\">BUS Kanal High 03</option>"; page += "\n";
  page += "<option value=\"23\">BUS Kanal High 04</option>"; page += "\n";
  page += "<option value=\"24\">BUS Kanal High 05</option>"; page += "\n";
  page += "<option value=\"25\">BUS Kanal High 06</option>"; page += "\n";
  page += "<option value=\"26\">BUS Kanal High 07</option>"; page += "\n";
  page += "<option value=\"27\">BUS Kanal High 08</option>"; page += "\n";
  page += "<option value=\"28\">BUS Kanal High 09</option>"; page += "\n";
  page += "<option value=\"29\">BUS Kanal High 10</option>"; page += "\n";
  page += "<option value=\"30\">BUS Kanal High 11</option>"; page += "\n";
  page += "<option value=\"31\">BUS Kanal High 12</option>"; page += "\n";
  page += "<option value=\"32\">BUS Kanal High 13</option>"; page += "\n";
  page += "<option value=\"33\">BUS Kanal High 14</option>"; page += "\n";
  page += "<option value=\"34\">BUS Kanal High 15</option>"; page += "\n";
  page += "<option value=\"35\">BUS Kanal High 16</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"PWM Pin Low\">\n";
  page += "<option value=\"40\">PWM Pin Low 01</option>"; page += "\n";
  page += "<option value=\"41\">PWM Pin Low 02</option>"; page += "\n";
  page += "<option value=\"42\">PWM Pin Low 03</option>"; page += "\n";
  page += "<option value=\"43\">PWM Pin Low 04</option>"; page += "\n";
  page += "<option value=\"44\">PWM Pin Low 05</option>"; page += "\n";
  page += "<option value=\"45\">PWM Pin Low 06</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"PWM Pin High\">\n";
  page += "<option value=\"50\">PWM Pin High 01</option>"; page += "\n";
  page += "<option value=\"51\">PWM Pin High 02</option>"; page += "\n";
  page += "<option value=\"52\">PWM Pin High 03</option>"; page += "\n";
  page += "<option value=\"53\">PWM Pin High 04</option>"; page += "\n";
  page += "<option value=\"54\">PWM Pin High 05</option>"; page += "\n";
  page += "<option value=\"55\">PWM Pin High 06</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"Eingang Pin\">\n";
  page += "<option value=\"60\">Eingang Pin 01</option>"; page += "\n";
  page += "<option value=\"61\">Eingang Pin 02</option>"; page += "\n";
  page += "<option value=\"62\">Eingang Pin 03</option>"; page += "\n";
  page += "<option value=\"63\">Eingang Pin 04</option>"; page += "\n";
  page += "<option value=\"64\">Eingang Pin 05</option>"; page += "\n";
  page += "<option value=\"65\">Eingang Pin 06</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"Einkanal\">\n";
  page += "<option value=\"70\">Einkanal 01</option>"; page += "\n";
  page += "<option value=\"71\">Einkanal 02</option>"; page += "\n";
  page += "<option value=\"72\">Einkanal 03</option>"; page += "\n";
  page += "<option value=\"73\">Einkanal 04</option>"; page += "\n";
  page += "<option value=\"74\">Einkanal 05</option>"; page += "\n";
  page += "<option value=\"75\">Einkanal 06</option>"; page += "\n";
  page += "<option value=\"76\">Einkanal 07</option>"; page += "\n";
  page += "<option value=\"77\">Einkanal 08</option>"; page += "\n";
  page += "</optgroup>"; page += "\n";
  page += "<optgroup label=\"Optionen\">\n";
  page += "<option value=\"200\">Dauerbetrieb an</option>"; page += "\n";
  page += "<option value=\"999\">Deaktiviert</option>"; page += "\n";
  page += "</optgroup>"; page += "\n";
  page += "</select><br></p>\n";


  page += "<p class=\"text2\" >Quelle Motorspeed Motor</p>\n";

  // Quelle Motorspeed Motor
  page += "<p><select id=\"SoundSPEED\" class=\"buttonA\" onchange=\"setsoundspeed()\">\n";
  page += "<optgroup label=\"BUS Kanal\">\n";
  page += "<option value=\"0\">BUS Kanal 01</option>\n";
  page += "<option value=\"1\">BUS Kanal 02</option>\n";
  page += "<option value=\"2\">BUS Kanal 03</option>\n";
  page += "<option value=\"3\">BUS Kanal 04</option>\n";
  page += "<option value=\"4\">BUS Kanal 05</option>\n";
  page += "<option value=\"5\">BUS Kanal 06</option>\n";
  page += "<option value=\"6\">BUS Kanal 07</option>\n";
  page += "<option value=\"7\">BUS Kanal 08</option>\n";
  page += "<option value=\"8\">BUS Kanal 09</option>\n";
  page += "<option value=\"9\">BUS Kanal 10</option>\n";
  page += "<option value=\"10\">BUS Kanal 11</option>\n";
  page += "<option value=\"11\">BUS Kanal 12</option>\n";
  page += "<option value=\"12\">BUS Kanal 13</option>\n";
  page += "<option value=\"13\">BUS Kanal 14</option>\n";
  page += "<option value=\"14\">BUS Kanal 15</option>\n";
  page += "<option value=\"15\">BUS Kanal 16</option>\n";
  page += "<optgroup label=\"PWM Pin\">\n";
  page += "<option value=\"20\">PWM Pin 01</option>"; page += "\n";
  page += "<option value=\"21\">PWM Pin 02</option>"; page += "\n";
  page += "<option value=\"22\">PWM Pin 03</option>"; page += "\n";
  page += "<option value=\"23\">PWM Pin 04</option>"; page += "\n";
  page += "<option value=\"24\">PWM Pin 05</option>"; page += "\n";
  page += "<option value=\"25\">PWM Pin 06</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"Optionen\">"; page += "\n";
  page += "<option value=\"999\">Deaktiviert</option>"; page += "\n";
  page += "</optgroup>"; page += "\n";
  page += "</select><br></p>\n";

  page += "<script> function setsoundspeed() { \n";
  page += "var sel = document.getElementById(\"SoundSPEED\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?SoundSPEED=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.Source_Speed_Sound_0, DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"SoundSPEED\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>"; page += "\n";
   
   

  valueString = String(config.Volumen_Sound[Menu], DEC);
  page += "<p class=\"text2\">Volumen : <span id=\"textVolumenSound\">" + valueString + "</span></p>\n";
  page += "<p><input type=\"range\" min=\"0\" max=\"200\" step=\"5\" class=\"slider\" id=\"VolumenSound\" onchange=\"setVolumenSound(this.value)\" value=\"" + valueString + "\" /></p>"; page += "\n";

  valueString = String(config.Min_Speed_Sound_0, DEC);
  page += "<p class=\"text2\">Drehzahl min : <span id=\"textDrehzahlmin\">" + valueString + " %</span></p>\n";
  page += "<p><input type=\"range\" min=\"0\" max=\"200\" step=\"5\" class=\"slider\" id=\"Drehzahlmin\" onchange=\"setDrehzahlmin(this.value)\" value=\"" + valueString + "\" /></p>"; page += "\n";

  valueString = String(config.Max_Speed_Sound_0, DEC);
  page += "<p class=\"text2\">Drehzahl max : <span id=\"textDrehzahlmax\">" + valueString + " %</span></p>\n";
  page += "<p><input type=\"range\" min=\"100\" max=\"600\" step=\"5\" class=\"slider\" id=\"Drehzahlmax\" onchange=\"setDrehzahlmax(this.value)\" value=\"" + valueString + "\" /></p>\n";
  
  valueString = String(config.shutdowndelay, DEC);
  page += "<p class=\"text2\">Motor aus Standgas in : <span id=\"textshutdowndelay\">" + valueString + " s</span></p>\n";
  page += "<p><input type=\"range\" min=\"0\" max=\"60\" step=\"2\" class=\"slider\" id=\"Motor aus Verzögerung\" onchange=\"setshutdowndelay(this.value)\" value=\"" + valueString + "\" /></p>\n";

  valueString = String(config.throttle_ramp, DEC);
  page += "<p class=\"text2\">Motor Rampe in : <span id=\"textthrottle_ramp\">" + valueString + " %/s</span></p>\n";
  page += "<p><input type=\"range\" min=\"0\" max=\"50\" step=\"2\" class=\"slider\" id=\"Motor Rampe in\" onchange=\"setthrottle_ramp(this.value)\" value=\"" + valueString + "\" /></p>\n";

  valueString = String(config.throttle_dead_band, DEC);
  page += "<p class=\"text2\">Standgas Totband in : <span id=\"textthrottle_dead_band\">" + valueString + " %</span></p>\n";
  page += "<p><input type=\"range\" min=\"0\" max=\"50\" step=\"2\" class=\"slider\" id=\"Standgas Totband in\" onchange=\"setthrottle_dead_band(this.value)\" value=\"" + valueString + "\" /></p>\n";

  // Save Button
  page += "<p><a href=\"/save\"><button class=\"button button2\">Save</button></a></p>"; page += "\n";

  page += "<br />\n";
  page += "<br />\n";
  page += "<p class=\"text2\" >Voreinstellungen</p>\n";
  page += "<br />\n";
  page += "<p><a href=\"/setsbus\"><button class=\"button button2\">SBUS</button></a></p>\n";
  page += "<br />\n";
  page += "<p><a href=\"/setpwm\"><button class=\"button button2\">PWM</button></a></p>\n";
  page += "<br />\n";
  page += "<p><a href=\"/setpin\"><button class=\"button button2\">PIN</button></a></p>\n";
  page += "<br />\n";

  page += "<p><a href=\"/reset\"><button class=\"button button2\">Werkseinstellung</button></a></p>\n";

  break;

    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    
  page += "<p class=\"text2\" ><b>Sound " + String(Menu) + " Einstellung</b></p>"; page += "\n";
  
  page += "<p><a href=\"/back\"><button class=\"button button3\">Back</button></a>\n";
  page += "<a href=\"/next\"><button class=\"button button4\">Next</button></a></p>\n";

  page += "<br />\n";
  page += "<br />\n";
  page += "<br />\n";
  page += "<br />\n";

  page += "<p class=\"text2\" >Quelle Einschalten Sound</p>\n";


  // Quelle Einschalten Sound
  page += "<p><select id=\"SoundON\" class=\"buttonA\" onchange=\"setsoundon()\">\n";
  page += "<optgroup label=\"BUS Kanal Low\">\n";
  page += "<option value=\"0\">BUS Kanal Low 01</option>\n";
  page += "<option value=\"1\">BUS Kanal Low 02</option>\n";
  page += "<option value=\"2\">BUS Kanal Low 03</option>\n";
  page += "<option value=\"3\">BUS Kanal Low 04</option>\n";
  page += "<option value=\"4\">BUS Kanal Low 05</option>\n";
  page += "<option value=\"5\">BUS Kanal Low 06</option>\n";
  page += "<option value=\"6\">BUS Kanal Low 07</option>\n";
  page += "<option value=\"7\">BUS Kanal Low 08</option>\n";
  page += "<option value=\"8\">BUS Kanal Low 09</option>\n";
  page += "<option value=\"9\">BUS Kanal Low 10</option>\n";
  page += "<option value=\"10\">BUS Kanal Low 11</option>\n";
  page += "<option value=\"11\">BUS Kanal Low 12</option>\n";
  page += "<option value=\"12\">BUS Kanal Low 13</option>\n";
  page += "<option value=\"13\">BUS Kanal Low 14</option>\n";
  page += "<option value=\"14\">BUS Kanal Low 15</option>\n";
  page += "<option value=\"15\">BUS Kanal Low 16</option>\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"BUS Kanal High\">\n";
      	    page += "<option value=\"20\">BUS Kanal High 01</option>"; page += "\n";
  page += "<option value=\"21\">BUS Kanal High 02</option>"; page += "\n";
  page += "<option value=\"22\">BUS Kanal High 03</option>"; page += "\n";
  page += "<option value=\"23\">BUS Kanal High 04</option>"; page += "\n";
  page += "<option value=\"24\">BUS Kanal High 05</option>"; page += "\n";
  page += "<option value=\"25\">BUS Kanal High 06</option>"; page += "\n";
  page += "<option value=\"26\">BUS Kanal High 07</option>"; page += "\n";
  page += "<option value=\"27\">BUS Kanal High 08</option>"; page += "\n";
  page += "<option value=\"28\">BUS Kanal High 09</option>"; page += "\n";
  page += "<option value=\"29\">BUS Kanal High 10</option>"; page += "\n";
  page += "<option value=\"30\">BUS Kanal High 11</option>"; page += "\n";
  page += "<option value=\"31\">BUS Kanal High 12</option>"; page += "\n";
  page += "<option value=\"32\">BUS Kanal High 13</option>"; page += "\n";
  page += "<option value=\"33\">BUS Kanal High 14</option>"; page += "\n";
  page += "<option value=\"34\">BUS Kanal High 15</option>"; page += "\n";
  page += "<option value=\"35\">BUS Kanal High 16</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"PWM Pin Low\">\n";
  page += "<option value=\"40\">PWM Pin Low 01</option>"; page += "\n";
  page += "<option value=\"41\">PWM Pin Low 02</option>"; page += "\n";
  page += "<option value=\"42\">PWM Pin Low 03</option>"; page += "\n";
  page += "<option value=\"43\">PWM Pin Low 04</option>"; page += "\n";
  page += "<option value=\"44\">PWM Pin Low 05</option>"; page += "\n";
  page += "<option value=\"45\">PWM Pin Low 06</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"PWM Pin High\">\n";
  page += "<option value=\"50\">PWM Pin High 01</option>"; page += "\n";
  page += "<option value=\"51\">PWM Pin High 02</option>"; page += "\n";
  page += "<option value=\"52\">PWM Pin High 03</option>"; page += "\n";
  page += "<option value=\"53\">PWM Pin High 04</option>"; page += "\n";
  page += "<option value=\"54\">PWM Pin High 05</option>"; page += "\n";
  page += "<option value=\"55\">PWM Pin High 06</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"Eingang Pin\">\n";
  page += "<option value=\"60\">Eingang Pin 01</option>"; page += "\n";
  page += "<option value=\"61\">Eingang Pin 02</option>"; page += "\n";
  page += "<option value=\"62\">Eingang Pin 03</option>"; page += "\n";
  page += "<option value=\"63\">Eingang Pin 04</option>"; page += "\n";
  page += "<option value=\"64\">Eingang Pin 05</option>"; page += "\n";
  page += "<option value=\"65\">Eingang Pin 06</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"Einkanal\">\n";
  page += "<option value=\"70\">Einkanal 01</option>"; page += "\n";
  page += "<option value=\"71\">Einkanal 02</option>"; page += "\n";
  page += "<option value=\"72\">Einkanal 03</option>"; page += "\n";
  page += "<option value=\"73\">Einkanal 04</option>"; page += "\n";
  page += "<option value=\"74\">Einkanal 05</option>"; page += "\n";
  page += "<option value=\"75\">Einkanal 06</option>"; page += "\n";
  page += "<option value=\"76\">Einkanal 07</option>"; page += "\n";
  page += "<option value=\"77\">Einkanal 08</option>"; page += "\n";
  page += "</optgroup>"; page += "\n";
  page += "<optgroup label=\"Ebenen Umschaltung\">\n";
  page += "<option value=\"80\">Ebene 01 Kanal 01</option>"; page += "\n";
  page += "<option value=\"81\">Ebene 01 Kanal 02</option>"; page += "\n";
  page += "<option value=\"82\">Ebene 01 Kanal 03</option>"; page += "\n";
  page += "<option value=\"83\">Ebene 01 Kanal 04</option>"; page += "\n";
  page += "<option value=\"84\">Ebene 01 Kanal 05</option>"; page += "\n";
  page += "<option value=\"85\">Ebene 01 Kanal 06</option>"; page += "\n";
  page += "<option value=\"86\">Ebene 01 Kanal 07</option>"; page += "\n";
  page += "<option value=\"87\">Ebene 01 Kanal 08</option>"; page += "\n";
  page += "<option value=\"88\">Ebene 02 Kanal 01</option>"; page += "\n";
  page += "<option value=\"89\">Ebene 02 Kanal 02</option>"; page += "\n";
  page += "<option value=\"90\">Ebene 02 Kanal 03</option>"; page += "\n";
  page += "<option value=\"91\">Ebene 02 Kanal 04</option>"; page += "\n";
  page += "<option value=\"92\">Ebene 02 Kanal 05</option>"; page += "\n";
  page += "<option value=\"93\">Ebene 02 Kanal 06</option>"; page += "\n";
  page += "<option value=\"94\">Ebene 02 Kanal 07</option>"; page += "\n";
  page += "<option value=\"95\">Ebene 02 Kanal 08</option>\n";
  page += "<option value=\"96\">Ebene 03 Kanal 01</option>"; page += "\n";
  page += "<option value=\"97\">Ebene 03 Kanal 02</option>"; page += "\n";
  page += "<option value=\"98\">Ebene 03 Kanal 03</option>"; page += "\n";
  page += "<option value=\"99\">Ebene 03 Kanal 04</option>"; page += "\n";
  page += "<option value=\"100\">Ebene 03 Kanal 05</option>"; page += "\n";
  page += "<option value=\"101\">Ebene 03 Kanal 06</option>"; page += "\n";
  page += "<option value=\"102\">Ebene 03 Kanal 07</option>"; page += "\n";
  page += "<option value=\"103\">Ebene 03 Kanal 08</option>"; page += "\n";
  page += "</optgroup>"; page += "\n";
  page += "<optgroup label=\"Optionen\">\n";
  page += "<option value=\"200\">Dauerbetrieb an</option>"; page += "\n";
  page += "<option value=\"999\">Deaktiviert</option>"; page += "\n";
  page += "</optgroup>"; page += "\n";
  page += "</select><br></p>\n";

  

  valueString = String(config.Volumen_Sound[Menu], DEC);
  page += "<p class=\"text2\">Volumen : <span id=\"textVolumenSound\">" + valueString + "</span></p>\n";
  page += "<p><input type=\"range\" min=\"0\" max=\"200\" step=\"5\" class=\"slider\" id=\"VolumenSound\" onchange=\"setVolumenSound(this.value)\" value=\"" + valueString + "\" /></p>\n";
  
  page += "<p class=\"text2\" >Wiedergabe Sound</p>\n";
  // Loop Mode  
  page += "<p><select id=\"SoundMODE\" class=\"buttonA\" onchange=\"setsoundmode()\">\n";
  page += "<option value=\"0\">Normal</option>\n";
  page += "<option value=\"1\">Loop</option>\n";
  page += "<option value=\"2\">Tippbetrieb</option>\n";
  page += "</select><br></p>\n";

  page += "<script> function setsoundmode() { \n";
  page += "var sel = document.getElementById(\"SoundMODE\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?SoundMODE=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.Mode_Sound[Menu], DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"SoundMODE\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>"; page += "\n";

  // Save Button
  page += "<p><a href=\"/save\"><button class=\"button button2\">Save</button></a></p>\n";
  page += "<p><a href=\"/Sound/on\"><button class=\"button button2\">Test Sound</button></a></p>\n";
                
  break;                      
    case 9:
    
  page += "<p class=\"text2\" ><b>Einstellung</b></p>"; page += "\n";
  
  page += "<p><a href=\"/back\"><button class=\"button button3\">Back</button></a>\n";
  page += "<a href=\"/next\"><button class=\"button button4\">Next</button></a></p>\n";

  page += "<br />\n";
  page += "<br />\n";
  page += "<br />\n";
  page += "<br />\n";

  page += "<p class=\"text2\" >Einkanal Einstellungen</p>\n";

  if (config.Einkanal_RC_System != 4) {  // Keine ELRS (CRSF)
  page += "<p class=\"text2\" >Einkanal Kanal</p>\n";

  page += "<p><select id=\"SbuschannelEinkanal\" class=\"buttonA\" onchange=\"setsbuschanneleinkanal()\">\n";
  page += "<optgroup label=\"SBUS Kanal\">\n";
  page += "<option value=\"0\">SBUS Kanal 01</option>\n";
  page += "<option value=\"1\">SBUS Kanal 02</option>\n";
  page += "<option value=\"2\">SBUS Kanal 03</option>\n";
  page += "<option value=\"3\">SBUS Kanal 04</option>\n";
  page += "<option value=\"4\">SBUS Kanal 05</option>\n";
  page += "<option value=\"5\">SBUS Kanal 06</option>\n";
  page += "<option value=\"6\">SBUS Kanal 07</option>\n";
  page += "<option value=\"7\">SBUS Kanal 08</option>\n";
  page += "<option value=\"8\">SBUS Kanal 09</option>\n";
  page += "<option value=\"9\">SBUS Kanal 10</option>\n";
  page += "<option value=\"10\">SBUS Kanal 11</option>\n";
  page += "<option value=\"11\">SBUS Kanal 12</option>\n";
  page += "<option value=\"12\">SBUS Kanal 13</option>\n";
  page += "<option value=\"13\">SBUS Kanal 14</option>\n";
  page += "<option value=\"14\">SBUS Kanal 15</option>\n";
  page += "<option value=\"15\">SBUS Kanal 16</option>\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"Optionen\">\n";
  page += "<option value=\"999\">Deaktiviert</option>"; page += "\n";
  page += "</optgroup>"; page += "\n";
  page += "</select><br></p>\n";

  page += "<script> function setsbuschanneleinkanal() { \n";
  page += "var sel = document.getElementById(\"SbuschannelEinkanal\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?SbuschannelEinkanal=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.Einkanal_Channel, DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"SbuschannelEinkanal\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>\n";
  }

  if (config.Einkanal_RC_System == 4) {  // ELRS (CRSF)
  // CRSF Adresse; id= MODULADRESSE; script= setmoduladresse(); value= config.modul_adress;
  page += "<p class=\"text2\" >Modul Adresse</p>\n";

  page += "<p><select id=\"MODULADRESSE\" class=\"buttonA\" onchange=\"setmoduladresse()\">\n";
  page += "<optgroup label=\"Modul Adresse\">\n";
  page += "<option value=\"0\">0</option>\n";
  page += "<option value=\"1\">1</option>\n";
  page += "<option value=\"2\">2</option>\n";
  page += "<option value=\"3\">3</option>\n";
  page += "<option value=\"4\">4</option>\n";
  page += "<option value=\"5\">5</option>\n";
  page += "<option value=\"6\">6</option>\n";
  page += "<option value=\"7\">7</option>\n";
  page += "<option value=\"8\">8</option>\n";
  page += "<option value=\"9\">9</option>\n";
  page += "<option value=\"10\">10</option>\n";
  page += "<option value=\"11\">11</option>\n";
  page += "<option value=\"12\">12</option>\n";
  page += "<option value=\"13\">13</option>\n";
  page += "<option value=\"14\">14</option>\n";
  page += "<option value=\"15\">15</option>\n";
  page += "<option value=\"16\">16</option>\n";
  page += "<option value=\"17\">17</option>\n";
  page += "<option value=\"18\">18</option>\n";
  page += "<option value=\"19\">19</option>\n";
  page += "<option value=\"20\">20</option>\n";
  page += "</optgroup>\n";
  page += "</select><br></p>\n";

  page += "<script> function setmoduladresse() { \n";
  page += "var sel = document.getElementById(\"MODULADRESSE\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?MODULADRESSE=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.modul_adress, DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"MODULADRESSE\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>\n";
  }

  // RC-Sytem; id= RCSytem; script= setrcsytem(); value= Einkanal_RC_System;
  page += "<p class=\"text2\" >RC-Sytem</p>\n";

  page += "<p><select id=\"RCSytem\" class=\"buttonA\" onchange=\"setrcsytem()\">\n";
  page += "<option value=\"0\">FrSky</option>\n";
  page += "<option value=\"1\">FlySky</option>\n";
  page += "<option value=\"2\">ELRS (SBUS)</option>\n";
  page += "<option value=\"3\">Hott</option>"; page += "\n";
  page += "<option value=\"4\">ELRS (CRSF)</option>"; page += "\n";
  page += "</select><br></p>\n";

  page += "<script> function setrcsytem() { \n";
  page += "var sel = document.getElementById(\"RCSytem\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?RCSytem=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.Einkanal_RC_System, DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"RCSytem\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>\n";

  if (config.Einkanal_RC_System != 4) {  // Keine ELRS (CRSF)
  // Einkanal-Mode; id= EinkanalMode; script= seteinkanalmode(); value= config.einkanal_mode;
  page += "<p class=\"text2\" >Einkanal-Mode</p>\n";

  page += "<p><select id=\"EinkanalMode\" class=\"buttonA\" onchange=\"seteinkanalmode()\">\n";
  page += "<option value=\"0\">Normal</option>\n";
  page += "<option value=\"10\">SBUS WM Adr 0</option>\n";
  page += "<option value=\"11\">SBUS WM Adr 1</option>\n";
  page += "<option value=\"12\">SBUS WM Adr 2</option>"; page += "\n";
  page += "<option value=\"13\">SBUS WM Adr 3</option>"; page += "\n";
  page += "</select><br></p>\n";

  page += "<script> function seteinkanalmode() { \n";
  page += "var sel = document.getElementById(\"EinkanalMode\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?EinkanalMode=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.Einkanal_mode, DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"EinkanalMode\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>\n";
  }
 
  page += "<hr>\n";

  // PWM MIN MAX
  page += "<p class=\"text2\" >PWM Einstellungen</p>\n";
  if (config.Hardware_Config < 2) {
  page += "<p class=\"text2\">PWM min (&micro;s)</p>\n";

  // PWM Min
  page += "<input type=\"number\" class=\"buttonA\" "
          "value=\"" + String(config.PWM_scale_min) + "\" "
          "min=\"0\" max=\"4095\" "
          "onchange=\"location.href='/setPWM?pwmMin='+this.value+'&pwmMax=" + String(config.PWM_scale_max) + "'\">";
  page += "\n";
  
  page += "<p class=\"text2\">PWM max (&micro;s)</p>\n";

  // PWM Max
  page += "<input type=\"number\" class=\"buttonA\" "
          "value=\"" + String(config.PWM_scale_max) + "\" "
          "min=\"0\" max=\"4095\" "
          "onchange=\"location.href='/setPWM?pwmMin=" + String(config.PWM_scale_min) + "&pwmMax='+this.value\">";
  page += "\n";
  } // end Hardware_Config < 2


  page += "<hr>"; page += "\n";

  // Ebenen Umschaltung

  page += "<p class=\"text2\" >Ebenen Umschaltung Einstellungen</p>\n";
  page += "<p class=\"text2\" >Ebenen Umschaltung Kanal</p>\n";

  page += "<p><select id=\"EbenenUmschaltungKanal\" class=\"buttonA\" onchange=\"setebenenumschaltungkanal()\">\n";
  page += "<optgroup label=\"BUS Kanal\">\n";
  page += "<option value=\"0\">BUS Kanal 01</option>\n";
  page += "<option value=\"1\">BUS Kanal 02</option>\n";
  page += "<option value=\"2\">BUS Kanal 03</option>\n";
  page += "<option value=\"3\">BUS Kanal 04</option>\n";
  page += "<option value=\"4\">BUS Kanal 05</option>\n";
  page += "<option value=\"5\">BUS Kanal 06</option>\n";
  page += "<option value=\"6\">BUS Kanal 07</option>\n";
  page += "<option value=\"7\">BUS Kanal 08</option>\n";
  page += "<option value=\"8\">BUS Kanal 09</option>\n";
  page += "<option value=\"9\">BUS Kanal 10</option>\n";
  page += "<option value=\"10\">BUS Kanal 11</option>\n";
  page += "<option value=\"11\">BUS Kanal 12</option>\n";
  page += "<option value=\"12\">BUS Kanal 13</option>\n";
  page += "<option value=\"13\">BUS Kanal 14</option>\n";
  page += "<option value=\"14\">BUS Kanal 15</option>\n";
  page += "<option value=\"15\">BUS Kanal 16</option>\n";
  page += "<optgroup label=\"PWM Pin\">\n";
  page += "<option value=\"20\">PWM Pin 01</option>"; page += "\n";
  page += "<option value=\"21\">PWM Pin 02</option>"; page += "\n";
  page += "<option value=\"22\">PWM Pin 03</option>"; page += "\n";
  page += "<option value=\"23\">PWM Pin 04</option>"; page += "\n";
  page += "<option value=\"24\">PWM Pin 05</option>"; page += "\n";
  page += "<option value=\"25\">PWM Pin 06</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"Optionen\">\n";
  page += "<option value=\"999\">Deaktiviert</option>"; page += "\n";
  page += "</optgroup>"; page += "\n";
  page += "</select><br></p>\n";

  page += "<script> function setebenenumschaltungkanal() { \n";
  page += "var sel = document.getElementById(\"EbenenUmschaltungKanal\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?EbenenUmschaltungKanal=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.Source_Ebenen_Um_Kanal, DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"EbenenUmschaltungKanal\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>\n";

  page += "<p class=\"text2\" >Ebenen Kanal</p>\n";

  page += "<p><select id=\"EbenenKanal\" class=\"buttonA\" onchange=\"setebenenkanal()\">\n";
  page += "<optgroup label=\"BUS Kanal\">\n";
  page += "<option value=\"0\">BUS Kanal 01</option>\n";
  page += "<option value=\"1\">BUS Kanal 02</option>\n";
  page += "<option value=\"2\">BUS Kanal 03</option>\n";
  page += "<option value=\"3\">BUS Kanal 04</option>\n";
  page += "<option value=\"4\">BUS Kanal 05</option>\n";
  page += "<option value=\"5\">BUS Kanal 06</option>\n";
  page += "<option value=\"6\">BUS Kanal 07</option>\n";
  page += "<option value=\"7\">BUS Kanal 08</option>\n";
  page += "<option value=\"8\">BUS Kanal 09</option>\n";
  page += "<option value=\"9\">BUS Kanal 10</option>\n";
  page += "<option value=\"10\">BUS Kanal 11</option>\n";
  page += "<option value=\"11\">BUS Kanal 12</option>\n";
  page += "<option value=\"12\">BUS Kanal 13</option>\n";
  page += "<option value=\"13\">BUS Kanal 14</option>\n";
  page += "<option value=\"14\">BUS Kanal 15</option>\n";
  page += "<option value=\"15\">BUS Kanal 16</option>\n";
  page += "<optgroup label=\"PWM Pin\">\n";
  page += "<option value=\"20\">PWM Pin 01</option>"; page += "\n";
  page += "<option value=\"21\">PWM Pin 02</option>"; page += "\n";
  page += "<option value=\"22\">PWM Pin 03</option>"; page += "\n";
  page += "<option value=\"23\">PWM Pin 04</option>"; page += "\n";
  page += "<option value=\"24\">PWM Pin 05</option>"; page += "\n";
  page += "<option value=\"25\">PWM Pin 06</option>"; page += "\n";
  page += "</optgroup>\n";
  page += "<optgroup label=\"Optionen\">\n";
  page += "<option value=\"999\">Deaktiviert</option>"; page += "\n";
  page += "</optgroup>"; page += "\n";
  page += "</select><br></p>\n";

  page += "<script> function setebenenkanal() { \n";
  page += "var sel = document.getElementById(\"EbenenKanal\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?EbenenKanal=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.Source_Ebenen_Kanal, DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"EbenenKanal\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>\n";

  page += "<hr>\n";

  // Ebenen Umschaltung

  page += "<p class=\"text2\" >Hardware Einstellungen</p>\n";
  page += "<p class=\"text2\" >Hardwareconfig</p>\n";

  page += "<p><select id=\"HardwareConfig\" class=\"buttonA\" onchange=\"setHardwareConfig()\">\n";
  page += "<option value=\"0\">V1 (GPIO 22,0,2,4)</option>\n";
  page += "<option value=\"1\">V2 (GPIO 14,27,32,33)</option>\n";
  page += "<option value=\"2\">V3 (nur BUS+Einkanal)</option>\n";
  page += "</select><br></p>\n";

  page += "<script> function setHardwareConfig() { \n";
  page += "var sel = document.getElementById(\"HardwareConfig\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?HardwareConfig=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(constrain(config.Hardware_Config,0,2), DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"HardwareConfig\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>\n";

  // Save Button
  page += "<p><a href=\"/save\"><button class=\"button button2\">Save</button></a></p>\n";

  break;
    case 10:
    
  page += "<p class=\"text2\" ><b>Debug Info</b></p>"; page += "\n";
  
  page += "<p><a href=\"/back\"><button class=\"button button3\">Back</button></a>\n";
  page += "<a href=\"/next\"><button class=\"button button4\">Next</button></a></p>\n";
  
  page += "<br />\n";
  page += "<br />\n";
  
  // BUS Kanal 1 bis 8 Ausgeben
  page += "<div class=\"left\" >\n";
  valueString = String(channel_output[0], DEC);
  page += "<p><br> K1: " + valueString; page += "\n";
  valueString = String(channel_output[1], DEC);
  page += "<br> K2: " + valueString; page += "\n";
  valueString = String(channel_output[2], DEC);
  page += "<br> K3: " + valueString; page += "\n";
  valueString = String(channel_output[3], DEC);
  page += "<br> K4: " + valueString; page += "\n";
  valueString = String(channel_output[4], DEC);
  page += "<br> K5: " + valueString; page += "\n";
  valueString = String(channel_output[5], DEC);
  page += "<br> K6: " + valueString; page += "\n";
  valueString = String(channel_output[6], DEC);
  page += "<br> K7: " + valueString; page += "\n";
  valueString = String(channel_output[7], DEC);
  page += "<br> K8: " + valueString + "</p> <hr>\n";

  // PWM Pin 1 bis 6 Ausgeben
  page += "<p>PWM PIN 1: " + String(PWM_pulse_width[0], DEC) + " &micro;s -> " + String(map(PWM_pulse_width[0], config.PWM_scale_min, config.PWM_scale_max, 0, 100), DEC) + " %\n";
  page += "<br>PWM PIN 2: " + String(PWM_pulse_width[1], DEC) + " &micro;s -> " + String(map(PWM_pulse_width[1], config.PWM_scale_min, config.PWM_scale_max, 0, 100), DEC) + " %\n";
  page += "<br>PWM PIN 3: " + String(PWM_pulse_width[2], DEC) + " &micro;s -> " + String(map(PWM_pulse_width[2], config.PWM_scale_min, config.PWM_scale_max, 0, 100), DEC) + " %\n";
  page += "<br>PWM PIN 4: " + String(PWM_pulse_width[3], DEC) + " &micro;s -> " + String(map(PWM_pulse_width[3], config.PWM_scale_min, config.PWM_scale_max, 0, 100), DEC) + " %\n";
  page += "<br>PWM PIN 5: " + String(PWM_pulse_width[4], DEC) + " &micro;s -> " + String(map(PWM_pulse_width[4], config.PWM_scale_min, config.PWM_scale_max, 0, 100), DEC) + " %\n";
  page += "<br>PWM PIN 6: " + String(PWM_pulse_width[5], DEC) + " &micro;s -> " + String(map(PWM_pulse_width[5], config.PWM_scale_min, config.PWM_scale_max, 0, 100), DEC) + " %\n";
  page += "</p> <hr>\n";

  // Datei laden OK ?
  valueString = String(Sound_loop.FileOK, DEC);
  page += "<p> loop.wav OK: " + valueString; page += "\n";
  valueString = String(Sound_shut.FileOK, DEC);
  page += "<br> shut.wav OK : " + valueString; page += "\n";
  valueString = String(Sound_start.FileOK, DEC);
  page += "<br> start.wav OK : " + valueString; page += "\n";
  valueString = String(Sound1.FileOK, DEC);
  page += "<br> sound1.wav OK: " + valueString; page += "\n";
  valueString = String(Sound2.FileOK, DEC);
  page += "<br> sound2.wav OK: " + valueString; page += "\n";
  valueString = String(Sound3.FileOK, DEC);
  page += "<br> sound3.wav OK: " + valueString; page += "\n";
  valueString = String(Sound4.FileOK, DEC);
  page += "<br> sound4.wav OK: " + valueString; page += "\n";
  valueString = String(Sound5.FileOK, DEC);
  page += "<br> sound5.wav OK: " + valueString; page += "\n";
  valueString = String(Sound6.FileOK, DEC);
  page += "<br> sound6.wav OK: " + valueString; page += "\n";
  valueString = String(Sound7.FileOK, DEC);
  page += "<br> sound7.wav OK: " + valueString; page += "\n";
  valueString = String(Sound8.FileOK, DEC);
  page += "<br> sound8.wav OK: " + valueString + "</p> <hr>\n";

  // Konfig Sound 1 bis 9 Ausgeben
  valueString = String(config.Hardware_Config, DEC);
  page += "<p> Konfig Hardware: " + valueString; page += "\n";
  valueString = String(config.Source_Start_Sound[0], DEC);
  page += "<br> Konfig Motor Start: " + valueString; page += "\n";
  valueString = String(config.Source_Start_Sound[1], DEC);
  page += "<br> Konfig Sound 1 Start: " + valueString; page += "\n";
  valueString = String(config.Source_Start_Sound[2], DEC);
  page += "<br> Konfig Sound 2 Start: " + valueString; page += "\n";
  valueString = String(config.Source_Start_Sound[3], DEC);
  page += "<br> Konfig Sound 3 Start: " + valueString; page += "\n";
  valueString = String(config.Source_Start_Sound[4], DEC);
  page += "<br> Konfig Sound 4 Start: " + valueString; page += "\n";
  valueString = String(config.Source_Start_Sound[5], DEC);
  page += "<br> Konfig Sound 5 Start: " + valueString; page += "\n";
  valueString = String(config.Source_Start_Sound[6], DEC);
  page += "<br> Konfig Sound 6 Start: " + valueString; page += "\n";
  valueString = String(config.Source_Start_Sound[7], DEC);
  page += "<br> Konfig Sound 7 Start: " + valueString; page += "\n";
  valueString = String(config.Source_Start_Sound[8], DEC);
  page += "<br> Konfig Sound 8 Start: " + valueString; page += "\n";
  valueString = String(config.Source_Speed_Sound_0, DEC);
  page += "<br> Konfig Speed Sound 1: " + valueString; page += "\n";
  valueString = String(config.throttle_ramp, DEC);
  page += "<br> Konfig Gas Ranpe: " + valueString; page += "\n";
  valueString = String(config.throttle_dead_band, DEC);
  page += "<br> Konfig Totband: " + valueString; page += "\n";
  valueString = String(config.Source_Ebenen_Um_Kanal, DEC);
  page += "<br> Konfig Eben Kanal Umschalter: " + valueString; page += "\n";
  valueString = String(config.Source_Ebenen_Kanal, DEC);
  page += "<br> Konfig Eben Kanal Wert: " + valueString; page += "\n";
  valueString = String(config.Einkanal_RC_System, DEC);
  page += "<br> Konfig Einkanal RC System: " + valueString; page += "\n";
  valueString = String(config.Einkanal_Channel, DEC);
  page += "<br> Konfig Einkanal Channel: " + valueString; page += "\n";
  valueString = String(config.Einkanal_mode, DEC);
  page += "<br> Konfig Einkanal Mode: " + valueString; page += "\n";
  valueString = String(config.modul_adress, DEC);
  page += "<br> Konfig CRSF Modul Adress: " + valueString + "</p> </div>\n";
  break;

  case 11:
  
  page += "<p class=\"text2\" ><b>WiFi Einstellung</b></p>"; page += "\n";
  
  page += "<p><a href=\"/back\"><button class=\"button button3\">Back</button></a>\n";
  page += "<a href=\"/next\"><button class=\"button button4\">Next</button></a></p>\n";
  
  page += "<br />\n";
  
  // WiFi SSID Eingabe
  page += "<p class=\"text2\" >WiFi SSID</p>\n";
  page += "<p><input type=\"text\" id=\"WiFi_SSID\" maxlength=\"31\" style=\"width:200px; height:25px;\" onchange=\"setWiFiSSID()\"></p>\n";
  
  page += "<script> function setWiFiSSID() { \n";
  page += "var val = document.getElementById(\"WiFi_SSID\").value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?WiFi_SSID=\" + encodeURIComponent(val) + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";
  
  // Aktuellen SSID anzeigen
  page += "<script>\n";
  page += "document.getElementById(\"WiFi_SSID\").value = \"" + String(config.WiFi_SSID) + "\";\n";
  page += "</script>\n";
  
  page += "<br />\n";
  
  // WiFi Password Eingabe
  page += "<p class=\"text2\" >WiFi Passwort</p>\n";
  page += "<p><input type=\"password\" id=\"WiFi_Password\" maxlength=\"63\" style=\"width:200px; height:25px;\" onchange=\"setWiFiPassword()\"></p>\n";
  
  page += "<script> function setWiFiPassword() { \n";
  page += "var val = document.getElementById(\"WiFi_Password\").value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?WiFi_Password=\" + encodeURIComponent(val) + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";
  
  // Aktuelles Password anzeigen
  page += "<script>\n";
  page += "document.getElementById(\"WiFi_Password\").value = \"" + String(config.WiFi_Password) + "\";\n";
  page += "</script>\n";
  
  page += "<br />\n";

  // AP IP-Adresse
  page += "<p class=\"text2\" >AP IP-Adresse</p>\n";
  page += "<p><input type=\"text\" id=\"WiFi_IP\" maxlength=\"15\" style=\"width:200px; height:25px;\" placeholder=\"192.168.1.1\" onchange=\"setWiFiIP()\"></p>\n";
  page += "<p class=\"text3\">(Neue IP gilt nach Neustart)</p>\n";
  page += "<script> function setWiFiIP() { \n";
  page += "var val = document.getElementById(\"WiFi_IP\").value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?WiFi_IP=\" + encodeURIComponent(val) + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";
  page += "<script>\n";
  page += "document.getElementById(\"WiFi_IP\").value = \"" + String(config.WiFi_IP) + "\";\n";
  page += "</script>\n";

  page += "<br />\n";

  // Geraetename TBS Agent
  page += "<p class=\"text2\" >Geraetename (TBS Agent)</p>\n";
  page += "<p><input type=\"text\" id=\"Device_Name\" maxlength=\"23\" style=\"width:200px; height:25px;\" onchange=\"setDeviceName()\"></p>\n";
  page += "<p class=\"text3\">(Im Agent sichtbar als Name@Adresse)</p>\n";
  page += "<script> function setDeviceName() { \n";
  page += "var val = document.getElementById(\"Device_Name\").value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?Device_Name=\" + encodeURIComponent(val) + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";
  page += "<script>\n";
  page += "document.getElementById(\"Device_Name\").value = \"" + String(config.Device_Name) + "\";\n";
  page += "</script>\n";

  page += "<br />\n";
  
  // Save Button
  page += "<p><a href=\"/save\"><button class=\"button button2\">Save</button></a></p>\n";
  
  break;
  }

  page += "<script> function setsoundon() { \n";
  page += "var sel = document.getElementById(\"SoundON\");\n";
  page += "var opt = sel.options[sel.selectedIndex];\n";
  page += "var val = opt.value;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?SoundON=\" + val + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  valueString = String(config.Source_Start_Sound[Menu], DEC);

  page += "<script> \n";
  page += "var selectedOption =" + valueString + ";\n";
  page += "var selectElement = document.getElementById(\"SoundON\");\n";
  page += "for (var i = 0; i < selectElement.options.length; i++) {\n";
  page += "var option = selectElement.options[i];\n";
  page += "if (option.value == selectedOption) {\n";
  page += "option.selected = true;\n";
  page += "break; }  } </script>\n";

  page += "<script> function setVolumenSound(pos) { \n";
  page += "var VolumenValue = document.getElementById(\"VolumenSound\").value;\n";
  page += "document.getElementById(\"textVolumenSound\").innerHTML = VolumenValue;\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?VolumenSound=\" + pos + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  page += "<script> function setDrehzahlmin(pos) { \n";
  page += "var VolumenValue = document.getElementById(\"Drehzahlmin\").value;\n";
  page += "document.getElementById(\"textDrehzahlmin\").innerHTML = VolumenValue + \" %\";\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?Drehzahlmin=\" + pos + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  page += "<script> function setDrehzahlmax(pos) { \n";
  page += "var VolumenValue = document.getElementById(\"Drehzahlmax\").value;\n";
  page += "document.getElementById(\"textDrehzahlmax\").innerHTML = VolumenValue + \" %\";\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?Drehzahlmax=\" + pos + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  page += "<script> function setshutdowndelay(pos) { \n";
  page += "var VolumenValue = document.getElementById(\"Motor aus Verzögerung\").value;\n";
  page += "document.getElementById(\"textshutdowndelay\").innerHTML = VolumenValue + \" s\";\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?shutdowndelay=\" + pos + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  page += "<script> function setthrottle_ramp(pos) { "; page += "\n";
  page += "var VolumenValue = document.getElementById(\"Motor Rampe in\").value;\n";
  page += "document.getElementById(\"textthrottle_ramp\").innerHTML = VolumenValue + \" %/s\";\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?ThrottleRamp=\" + pos + \"&\", true);\n";
  page += "xhr.send(); } </script>\n";

  page += "<script> function setthrottle_dead_band(pos) { \n";
  page += "var VolumenValue = document.getElementById(\"Standgas Totband in\").value;\n";
  page += "document.getElementById(\"textthrottle_dead_band\").innerHTML = VolumenValue + \" %\";\n";
  page += "var xhr = new XMLHttpRequest();\n";
  page += "xhr.open('GET', \"/?ThrottleDeadBand=\" + pos + \"&\", true);\n";
  page += "xhr.send(); } </script>"; page += "\n";
  
  page += "</body></html>\n";
  page += "</body></html>\n";
  return page;
}
