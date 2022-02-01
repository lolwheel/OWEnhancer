#include "recovery.h"

#include <ArduinoOTA.h>
#include <AsyncElegantOTA.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "settings.h"
#include "task_queue.h"

#define SSID_NAME "Owie-recovery"

namespace {
DNSServer dnsServer;
AsyncWebServer webServer(80);
}  // namespace

void recovery_setup() {
  WiFi.setOutputPower(0);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID_NAME);
  ArduinoOTA.setHostname("owie");
  ArduinoOTA.begin(true);
  ArduinoOTA.onStart([]() {
    disableFlashPageRotation();
    saveSettings();
  });
  dnsServer.start(53, "*", WiFi.softAPIP());  // DNS spoofing.
  webServer.onNotFound([&](AsyncWebServerRequest *request) {
    request->redirect("http://" + WiFi.softAPIP().toString() + "/update");
  });
  AsyncElegantOTA.begin(&webServer);
  webServer.begin();
  TaskQueue.postRecurringTask([&]() {
    dnsServer.processNextRequest();
    ArduinoOTA.handle();
  });
}
