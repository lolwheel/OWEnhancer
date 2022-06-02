#include "network.h"

#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>

#include "ArduinoJson.h"
#include "bms_relay.h"
#include "data.h"
#include "settings.h"
#include "task_queue.h"
#include "web_ota.h"
#include "vector"

using std::vector;

namespace {
DNSServer dnsServer;
AsyncWebServer webServer(80);
AsyncWebSocket ws("/rawdata");

const String defaultPass("****");
BmsRelay *relay;

const String owie_version = "1.1.1";

vector<String> wifi_array;

String uptimeString() {
  const unsigned long nowSecs = millis() / 1000;
  const int hrs = nowSecs / 3600;
  const int mins = (nowSecs % 3600) / 60;
  const int secs = nowSecs % 60;
  String ret;
  if (hrs) {
    ret.concat(hrs);
    ret.concat('h');
  }
  ret.concat(mins);
  ret.concat('m');
  ret.concat(secs);
  ret.concat('s');
  return ret;
}

void retrieveAvailableWifiNetworks() {
  // preset WIFI power setting to retrieve as many wifi networks as possible
  WiFi.setOutputPower(17);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks(false,false, 0, NULL);
  for (int i = 0; i < n; i++)
  {
    wifi_array.emplace_back(WiFi.SSID(i));
  }
  // sort and deduplicate wifi networks
  sort( wifi_array.begin(), wifi_array.end() );
  wifi_array.erase( unique( wifi_array.begin(), wifi_array.end() ), wifi_array.end() );
}


String generateOwieStatusJson() {
  DynamicJsonDocument status(1024);
  String jsonOutput;
  const uint16_t *cellMillivolts = relay->getCellMillivolts();
  String out;
  out.reserve(256);
  for (int i = 0; i < 3; i++) {
    out.concat("<tr>");
    for (int j = 0; j < 5; j++) {
      out.concat("<td>");
      out.concat(cellMillivolts[i * 5 + j] / 1000.0);
      out.concat("</td>");
    }
    out.concat("<tr>");
  }

  status["TOTAL_VOLTAGE"] =
      String(relay->getTotalVoltageMillivolts() / 1000.0, 2) + "v";
  status["CURRENT_AMPS"] = String(relay->getCurrentInAmps(), 1) + " Amps";
  status["BMS_SOC"] = String(relay->getBmsReportedSOC()) + "%";
  status["OVERRIDDEN_SOC"] = String(relay->getOverriddenSOC()) + "%";
  status["USED_CHARGE_MAH"] = String(relay->getUsedChargeMah()) + " mAh";
  status["REGENERATED_CHARGE_MAH"] =
      String(relay->getRegeneratedChargeMah()) + " mAh";
  status["UPTIME"] = uptimeString();
  status["CELL_VOLTAGE_TABLE"] = out;

  serializeJson(status, jsonOutput);
  return jsonOutput;
}

bool lockingPreconditionsMet() { return strlen(Settings->ap_self_password) > 0; }
const char *lockedStatusDataAttrValue() {
  return Settings->is_locked ? "1" : "";
};

String templateProcessor(const String &var) {
  if (var == "TOTAL_VOLTAGE") {
    return String(relay->getTotalVoltageMillivolts() / 1000.0,
                  /* decimalPlaces = */ 2);
  } else if (var == "CURRENT_AMPS") {
    return String(relay->getCurrentInAmps(),
                  /* decimalPlaces = */ 1);
  } else if (var == "BMS_SOC") {
    return String(relay->getBmsReportedSOC());
  } else if (var == "OVERRIDDEN_SOC") {
    return String(relay->getOverriddenSOC());
  } else if (var == "USED_CHARGE_MAH") {
    return String(relay->getUsedChargeMah());
  } else if (var == "REGENERATED_CHARGE_MAH") {
    return String(relay->getRegeneratedChargeMah());
  } else if (var == "OWIE_version") {
    return owie_version;
  } else if (var == "SSID") {
    return Settings->ap_name;
  } else if (var == "PASS") {
    if (strlen(Settings->ap_password) > 0) {
      return defaultPass;
    }
    return "";
  } else if (var == "GRACEFUL_SHUTDOWN_COUNT") {
    return String(Settings->graceful_shutdown_count);
  } else if (var == "UPTIME") {
    return uptimeString();
  } else if (var == "IS_LOCKED") {
    return lockedStatusDataAttrValue();
  } else if (var == "CAN_ENABLE_LOCKING") {
    return lockingPreconditionsMet() ? "1" : "";
  } else if (var == "LOCKING_ENABLED") {
    return Settings->locking_enabled ? "1" : "";
  } else if (var == "CELL_VOLTAGE_TABLE") {
    const uint16_t *cellMillivolts = relay->getCellMillivolts();
    String out;
    out.reserve(256);
    for (int i = 0; i < 3; i++) {
      out.concat("<tr>");
      for (int j = 0; j < 5; j++) {
        out.concat("<td>");
        out.concat(cellMillivolts[i * 5 + j] / 1000.0);
        out.concat("</td>");
      }
      out.concat("<tr>");
    }
    return out;
  } else if (var == "BMS_SERIAL_OVERRIDE") {
    // so it doesn't return 0 when no override is set
    if (Settings->bms_serial == 0) {
      return "";
    } else {
      return String(Settings->bms_serial);
    }
  } else if (var == "AP_PASSWORD") {
    return Settings->ap_self_password;
  } else if (var == "BMS_CURRENT_SERIAL") {
    return String(relay->getCapturedBMSSerial());
  } else if (var == "AP_SELF_NAME") {
    return Settings->ap_self_name;
  } else if (var == "DISPLAY_AP_NAME") {
    char apDisplayName[64];
    if (strlen(Settings->ap_self_name) > 0) {
      snprintf(apDisplayName, sizeof(apDisplayName), "%s",
               Settings->ap_self_name);
    } else {
      snprintf(apDisplayName, sizeof(apDisplayName), "Owie-%04X",
               ESP.getChipId() & 0xFFFF);
    }
    return String(apDisplayName);
  } else if (var == "WIFI_POWER") {
    return String(Settings->wifi_power);
  } else if (var == "WIFI_POWER_OPTIONS") {
    String opts;
    opts.reserve(256);
    for (int i = 8; i < 18; i++) {
      opts.concat("<option value='");
      opts.concat(String(i));
      opts.concat("'");
      if (i == Settings->wifi_power) {
        opts.concat(" selected ");
      }
      opts.concat(">");
      opts.concat(String(i));
      opts.concat("</option>");
    }
    return opts;
  } else if (var == "AVAIL_NETWORK_OPTIONS") {
    String wifi_network_opts;
    wifi_network_opts.reserve(65500);
    for (u_int i = 0; i < wifi_array.size(); i++)
    {
      String wifi_name = wifi_array[i];
      wifi_network_opts.concat("<option value='");
      wifi_network_opts.concat(wifi_name);
      wifi_network_opts.concat("'");
      if (wifi_name == String(Settings->ap_name)) {
        wifi_network_opts.concat(" selected ");
      }
      wifi_network_opts.concat(">");
      wifi_network_opts.concat(wifi_name);
      wifi_network_opts.concat("</option>");
    }
    return wifi_network_opts;
  }
  return "<script>alert('UNKNOWN PLACEHOLDER')</script>";
}

}  // namespace

void setupWifi() {
  // fetch available wireless networks
  retrieveAvailableWifiNetworks();

  bool stationMode = (strlen(Settings->ap_name) > 0);
  WiFi.setOutputPower(Settings->wifi_power);
  WiFi.mode(stationMode ? WIFI_AP_STA : WIFI_AP);
  char apName[64];
  // sprintf isn't causing the issue of bungled SSID anymore (can't reproduce)
  // but snprintf should be safer, so trying that now
  // 9 bytes should be sufficient
  if (strlen(Settings->ap_self_name) > 0) {
    snprintf(apName, sizeof(apName), Settings->ap_self_name);
  } else {
    snprintf(apName, sizeof(apName), "Owie-%04X", ESP.getChipId() & 0xFFFF);
  }
  WiFi.softAP(apName, Settings->ap_self_password);
  if (stationMode) {
    WiFi.begin(Settings->ap_name, Settings->ap_password);
    WiFi.hostname(apName);
  }
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());  // DNS spoofing.
  TaskQueue.postRecurringTask([]() { dnsServer.processNextRequest(); });
}

void setupWebServer(BmsRelay *bmsRelay) {
  relay = bmsRelay;
  WebOta::begin(&webServer);
  webServer.addHandler(&ws);
  webServer.onNotFound([](AsyncWebServerRequest *request) {
    if (request->host().indexOf("owie.local") >= 0) {
      request->send(404);
      return;
    }
    request->redirect("http://" + WiFi.softAPIP().toString() + "/");
  });
  webServer.on("/autoupdate", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", generateOwieStatusJson());
  });

  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML_PROGMEM_ARRAY, INDEX_HTML_SIZE,
                    templateProcessor);
  });
  webServer.on("/wifi", HTTP_ANY, [](AsyncWebServerRequest *request) {
    switch (request->method()) {
      case HTTP_GET:
        request->send_P(200, "text/html", WIFI_HTML_PROGMEM_ARRAY,
                        WIFI_HTML_SIZE, templateProcessor);
        return;
      case HTTP_POST:
        const auto ssidParam = request->getParam("s", true);
        const auto passwordParam = request->getParam("p", true);
        if (ssidParam == nullptr || passwordParam == nullptr ||
            ssidParam->value().length() > sizeof(Settings->ap_name) ||
            passwordParam->value().length() > sizeof(Settings->ap_password)) {
          request->send(200, "text/html", "Invalid SSID or Password.");
          return;
        }
        snprintf(Settings->ap_name, sizeof(Settings->ap_name), "%s",
                 ssidParam->value().c_str());
        snprintf(Settings->ap_password, sizeof(Settings->ap_password), "%s",
                 passwordParam->value().c_str());
        saveSettingsAndRestartSoon();
        request->send(200, "text/html", "WiFi settings saved, restarting...");
        return;
    }
    request->send(404);
  });
  webServer.on("/monitor", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", MONITOR_HTML_PROGMEM_ARRAY,
                    MONITOR_HTML_SIZE, templateProcessor);
  });
  webServer.on("/settings", HTTP_ANY, [](AsyncWebServerRequest *request) {
    switch (request->method()) {
      case HTTP_GET:
        request->send_P(200, "text/html", SETTINGS_HTML_PROGMEM_ARRAY,
                        SETTINGS_HTML_SIZE, templateProcessor);
        return;
      case HTTP_POST:
        const auto bmsSerialParam = request->getParam("bs", true);
        const auto apSelfPassword = request->getParam("pw", true);
        const auto apSelfName = request->getParam("apselfname", true);
        const auto wifiPower = request->getParam("wifipower", true);
        if (bmsSerialParam == nullptr) {
          request->send(400, "text/html", "Invalid BMS Serial number.");
          return;
        }
        if (apSelfPassword == nullptr ||
            apSelfPassword->value().length() >
                sizeof(Settings->ap_self_password) ||
            (apSelfPassword->value().length() < 8 &&
             apSelfPassword->value().length() >
                 0)) {  // this check is necessary so the user can't set a too
                        // small password and thus the network wont' show up
          request->send(400, "text/html", "AP password must be between 8 and 31 characters");
          return;
        }
        if (apSelfName == nullptr ||
            apSelfName->value().length() > sizeof(Settings->ap_self_name) ||
            (apSelfName->value().length() < 1 &&
             apSelfName->value().length() > 0)) {
          request->send(400, "text/html", "Invalid Custom AP Name.");
          return;
        }
        // allows user to leave bms serial field blank instead of having to put
        // 0
        if (bmsSerialParam->value().length() == 0) {
          Settings->bms_serial = 0;
        } else {
          Settings->bms_serial = bmsSerialParam->value().toInt();
        }

        // Set wifi power
        // add aditional sanity checks, so that the power range is between 8 and
        // 17 only!
        if (wifiPower == nullptr || wifiPower->value().toInt() < 8 ||
            wifiPower->value().toInt() > 17) {
          request->send(
              400, "text/html",
              "Wifi Power range MUST be between 8 (dBm) and 17 (dBm).");
          return;
        }
        Settings->wifi_power = wifiPower->value().toInt();
        snprintf(Settings->ap_self_password, sizeof(Settings->ap_self_password),
                 "%s", apSelfPassword->value().c_str());
        snprintf(Settings->ap_self_name, sizeof(Settings->ap_self_name), "%s",
                 apSelfName->value().c_str());
        saveSettingsAndRestartSoon();
        request->send(200, "text/html", "Settings saved, restarting...");
        return;
    }
    request->send(404);
  });
  webServer.on("/lock", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("unlock")) {
      Settings->is_locked = false;
      saveSettings();
      request->send(200, "text/html", "Board unlocked, restart your board.");
      return;
    } else if (request->hasParam("toggleArm")) {
      if (!lockingPreconditionsMet()) {
        request->send(500, "text/html",
                      "Cannot enable locking with empty WiFi password");
        return;
      }
      Settings->locking_enabled = !Settings->locking_enabled;
      Settings->is_locked = false;
      saveSettings();
      request->send(200, "text/html", Settings->locking_enabled ? "1" : "");
      return;
    }
    request->send(404);
  });

  webServer.begin();
}

void streamBMSPacket(uint8_t *const data, size_t len) {
  ws.binaryAll((char *const)data, len);
}
