#include "network.h"

#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>

#include <cstring>

#include "data.h"
#include "settings.h"
#include "task_queue.h"

namespace {
DNSServer dnsServer;
AsyncWebServer webServer(80);
AsyncWebSocket ws("/rawdata");

const String defaultPass("****");

String templateProcessor(const String &var) {
  if (var == "OWIE_version") {
    return "0.0.1";
  } else if (var == "SSID") {
    return Settings.ap_name;
  } else if (var == "PASS") {
    if (strlen(Settings.ap_password) > 0) {
      return defaultPass;
    }
    return "";
  } else if (var == "COEFFICIENT") {
    char bufStr[16]; // 16 bytes should be large enough
    snprintf(bufStr, "%2.2g", Settings.coefficient);
    return bufStr;
  } else if (var == "OFFSET") {
    char bufStr[16]; // 16 bytes should be large enough
    snprintf(bufStr, "%2.2g", Settings.offset);
    return bufStr;
  }
  return "<script>alert('UNKNOWN PLACEHOLDER')</script>";
}

} // namespace

void setupWifi() {
  WiFi.setOutputPower(6);
  bool stationMode = (strlen(Settings.ap_name) > 0);
  WiFi.mode(stationMode ? WIFI_AP_STA : WIFI_AP);
  char apName[64];
  sprintf(apName, "Owie-%04X", ESP.getChipId() & 0xFFFF);
  WiFi.softAP(apName);
  if (stationMode) {
    WiFi.begin(Settings.ap_name, Settings.ap_password);
    WiFi.hostname(apName);
  }
  MDNS.begin("owie");
  dnsServer.start(53, "*", WiFi.softAPIP()); // DNS spoofing.
  TaskQueue.postRecurringTask([]() {
    dnsServer.processNextRequest();
    MDNS.update();
  });
}

void setupWebServer() {
  webServer.addHandler(&ws);
  webServer.onNotFound([](AsyncWebServerRequest *request) {
    if (request->host().indexOf("owie.local") >= 0) {
      request->send(404);
      return;
    }
    request->redirect("http://" + WiFi.softAPIP().toString() + "/");
  });
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML_PROGMEM_ARRAY, INDEX_HTML_SIZE,
                    templateProcessor);
  });
  webServer.on("/wifi", HTTP_ANY, [](AsyncWebServerRequest *request) {
    switch (request->method()) {
    case HTTP_GET:
      request->send_P(200, "text/html", WIFI_HTML_PROGMEM_ARRAY, WIFI_HTML_SIZE,
                      templateProcessor);
      return;
    case HTTP_POST:
      const auto ssidParam = request->getParam("s", true);
      const auto passwordParam = request->getParam("p", true);
      if (ssidParam == nullptr || passwordParam == nullptr ||
          ssidParam->value().length() > sizeof(Settings.ap_name) ||
          passwordParam->value().length() > sizeof(Settings.ap_password)) {
        request->send(400, "text/html", "Invalid SSID or Password.");
        return;
      }
      std::strncpy(Settings.ap_name, ssidParam->value().c_str(),
                   sizeof(Settings.ap_name));
      std::strncpy(Settings.ap_password, passwordParam->value().c_str(),
                   sizeof(Settings.ap_password));
      saveSettingsAndRestartSoon();
      request->send(200, "text/html", "WiFi settings saved, restarting...");
      return;
    }
    request->send(404);
  });
  webServer.on("/settings", HTTP_ANY, [](AsyncWebServerRequest *request) {
    switch (request->method()) {
    case HTTP_GET:
      request->send_P(200, "text/html", SETTINGS_HTML_PROGMEM_ARRAY,
                      SETTINGS_HTML_SIZE, templateProcessor);
      return;
    case HTTP_POST:
      const auto coefficient = request->getParam("coefficient", true);
      const auto offset = request->getParam("offset", true);
      if (coefficient == nullptr || offset == nullptr) {
        request->send(400, "text/html", "Invalid value entered.");
        return;
      }
      Settings.coefficient = coefficient->value().toFloat();
      Settings.offset = offset->value().toFloat();

      saveSettingsAndRestartSoon();
      request->send(200, "text/html", "Scaling settings saved, restarting...");
      return;
    }
    request->send(404);
  });
  webServer.on("/monitor", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", MONITOR_HTML_PROGMEM_ARRAY,
                    MONITOR_HTML_SIZE, templateProcessor);
  });

  webServer.begin();
}

void streamBMSPacket(uint8_t *const data, size_t len) {
  ws.binaryAll((char *const)data, len);
}