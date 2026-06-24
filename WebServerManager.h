// WebServerManager.h  -  ESP32-RC-Sound v0.60
// Non-blocking WebServer (ESP32 WebServer.h)

#pragma once
#include <WiFi.h>
#include <WebServer.h>

class WebServerManager {
public:
  static void begin(const char* apSsid, const char* apPassword);
  static void Webpage();   // handleClient() aufrufen

private:
  static WebServer server;
  static int       Menu;
  static String    valueString;
  static void      handleRequest();
  static String    buildPage();
  // Helper
  static String    urlDecode(const String& s);
};
