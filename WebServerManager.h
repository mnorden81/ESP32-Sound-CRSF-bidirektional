// WebServerManager.h  –  ESP32-RC-Sound v0.84
#pragma once
#include <WiFi.h>
#include <WebServer.h>

class WebServerManager {
public:
  static void begin(const char* apSsid, const char* apPassword);
  static void Webpage();

private:
  static WebServer server;
  static int       Menu;
  static String    valueString;
  static void      handleRequest();
  static void      handleSport();
  static String    buildPage();
  static String    urlDecode(const String& s);
};
