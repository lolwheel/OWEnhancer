#include "network.h"

#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>

#include <functional>

#include "ArduinoJson.h"
#include "async_ota.h"
#include "bms_relay.h"
#include "data.h"
#include "settings.h"
#include "task_queue.h"

namespace {
DNSServer dnsServer;
AsyncWebServer webServer(80);
AsyncWebSocket ws("/rawdata");

const String defaultPass("****");
BmsRelay *relay;

const String owie_version = "2.1.0";

bool lockingPreconditionsMet() {
  return strlen(Settings->ap_self_password) > 0;
}

const char *lockedStatusDataAttrValue() {
  return Settings->is_locked ? "1" : "";
}

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

DynamicJsonDocument generatePackageStatsJson() {
  DynamicJsonDocument packageStats(250);
  JsonObject root = packageStats.to<JsonObject>();
  JsonArray statsArray = root.createNestedArray("stats");
  for (const IndividualPacketStat &stat :
      relay->getPacketTracker().getIndividualPacketStats()) {
        if (stat.id < 0) {
          continue;
        }
        JsonObject statObj = statsArray.createNestedObject();
        char buffer[16];
        snprintf_P(buffer, sizeof(buffer), PSTR("%X"), stat.id);
        statObj["id"] = buffer;
        statObj["period"] = stat.mean_period_millis();
        statObj["deviation"] = stat.deviation_millis();
        statObj["count"] = stat.total_num;
  }
  root["unknown_bytes"] = relay->getPacketTracker().getGlobalStats().total_unknown_bytes_received;
  root["missmatches"] = relay->getPacketTracker()
                    .getGlobalStats()
                    .total_packet_checksum_mismatches;
  return packageStats;
}

DynamicJsonDocument generateMetadataJson() {
  DynamicJsonDocument metadata(1024);
  JsonObject root = metadata.to<JsonObject>();

  // owie version
  root["owie_version"] = owie_version;
  
  // the current OWIE Wifi Name
  char apDisplayName[64];
  if (strlen(Settings->ap_self_name) > 0) {
    snprintf(apDisplayName, sizeof(apDisplayName), "%s",
              Settings->ap_self_name);
  } else {
    snprintf(apDisplayName, sizeof(apDisplayName), "Owie-%04X",
              ESP.getChipId() & 0xFFFF);
  }
  root["display_ap_name"] =String(apDisplayName);

  // current uptime 
  root["uptime"] = uptimeString();

  // charging
  root["charging"] = relay->isCharging();

  // shutdown count
  root["graceful_shutdown_count"] = String(Settings->graceful_shutdown_count);

  // board locking is armed
  root["can_enable_locking"] = lockingPreconditionsMet() ? "1" : "";
  
  // board locking enabled
  root["locking_enabled"] = Settings->locking_enabled ? "1" : "";

  // board locked
  root["is_locked"] = lockedStatusDataAttrValue();

  // current AP values (owie wireless)
  root["ap_self_name"] = Settings->ap_self_name;
  root["ap_password"] = (strlen(Settings->ap_password) > 0) ? Settings->ap_self_password: defaultPass;
  root["wifi_power"] = Settings->wifi_power;
  JsonArray wifi_power_options = root.createNestedArray("wifi_power_options");
  for (int i = 8; i < 18; i++) {
    JsonObject power_option = wifi_power_options.createNestedObject();
    power_option["value"] = String(i);
    power_option["selected"] = (i == Settings->wifi_power) ? true: false;
  }

  // WiFi connect (connect to an existing WiFi)
  root["ssid"] = Settings->ap_name;
  root["pass"] = "";

  // package stats in monitoring section
  root["package_stats"] = generatePackageStatsJson();

  // add current read out BMS Serial Nr.  
  root["bms_serial_captured"] = relay->getCapturedBMSSerial();
  
  return metadata;
}

DynamicJsonDocument generateOwieStatusJson() {
  DynamicJsonDocument statusDoc(1512);
  JsonObject root = statusDoc.to<JsonObject>();
  const uint16_t *cellMillivolts = relay->getCellMillivolts();
  const int8_t *thermTemps = relay->getTemperaturesCelsius();

  // owie_percentage
  JsonObject owie_percentage = root.createNestedObject("owie_percentage");
  owie_percentage["value"] = String(relay->getOverriddenSOC());
  owie_percentage["unit"] = "%";
 
  // bms_percentage
  JsonObject bms_percentage = root.createNestedObject("bms_percentage");
  bms_percentage["value"] = String(relay->getBmsReportedSOC());
  bms_percentage["unit"] = "%";

  // uptime
  JsonObject uptime = root.createNestedObject("uptime");
  uptime["value"] = uptimeString();
  uptime["unit"] = "";

  // usage
  JsonObject usage = root.createNestedObject("usage");
  usage["value"] = relay->getUsedChargeMah();
  usage["unit"] = "mAh";

  // regen
  JsonObject regen = root.createNestedObject("regen");
  regen["value"] = relay->getRegeneratedChargeMah();
  regen["unit"] = "mAh";
  
  // voltage
  JsonObject voltage = root.createNestedObject("voltage");
  voltage["value"] = String(relay->getTotalVoltageMillivolts() / 1000.0, 2);
  voltage["unit"] = "V";

  // current
  JsonObject current = root.createNestedObject("current");
  current["value"] = String(relay->getCurrentMilliamps() / 1000.0, 1);
  current["unit"] = "A";
  
  // charging
  JsonObject charging = root.createNestedObject("charging");
  charging["value"] = relay->isCharging();
  charging["unit"] = "";
 
  // battery_cells
  JsonObject battery_cells = root.createNestedObject("battery_cells");
  JsonArray batteryValues = battery_cells.createNestedArray("value");
  for (int i = 0; i < 15; i++) {
    batteryValues.add(String(cellMillivolts[i] / 1000.0));
  }
  battery_cells["unit"] = "V";

  // temperatures
  JsonObject temperatures = root.createNestedObject("temperatures");
  JsonArray tempValues = temperatures.createNestedArray("value");
  for (int i = 0; i < 5; i++) {
    tempValues.add(thermTemps[i]);
  }
  temperatures["unit"] = "&deg;C";
  
  // add current read out BMS Serial Nr.
  JsonObject bms_serial = root.createNestedObject("bms_serial_captured");
  bms_serial["value"] = relay->getCapturedBMSSerial();
  bms_serial["unit"] = "";
  
  // add new battery status values (used only at stats page.)
  // normaly there would be a reset for the stats, but as the project is more or less abandoned from the maintainer...
  // (no effort is put into this any more...)
  // add current read out BMS Serial Nr.
  JsonObject voltage_based_soc = root.createNestedObject("voltage_based_soc");
  voltage_based_soc["value"] = String(relay->getBatteryFuelGauge().getVoltageBasedSoc());
  voltage_based_soc["unit"] = "%";

  JsonObject bottom_soc = root.createNestedObject("bottom_soc");
  bottom_soc["value"] = String(relay->getBatteryFuelGauge().getState().bottomSoc);
  bottom_soc["label"] = "Lowest Ah-tracked SOC";
  bottom_soc["unit"] = "%";

  JsonObject top_soc = root.createNestedObject("top_soc");
  top_soc["value"] = String(relay->getBatteryFuelGauge().getState().topSoc);
  top_soc["label"] = "Highest Ah-tracked SOC";
  top_soc["unit"] = "%";

  JsonObject bottom_milliamp_hours = root.createNestedObject("bottom_milliamp_hours");
  bottom_milliamp_hours["value"] = String(
        relay->getBatteryFuelGauge().getState().bottomMilliampSeconds / 3600);
  bottom_milliamp_hours["label"] = "Ah-tracked range size";      
  bottom_milliamp_hours["unit"] = "mAh";

  JsonObject current_milliamp_hours = root.createNestedObject("current_milliamp_hours");
  current_milliamp_hours["value"] = String(
        relay->getBatteryFuelGauge().getState().currentMilliampSeconds / 3600);
  current_milliamp_hours["label"] = "Current discharge depth";
  current_milliamp_hours["unit"] = "mAh";

  return statusDoc;

}
}
void setupWifi() {
  WiFi.setOutputPower(Settings->wifi_power);
  bool stationMode = (strlen(Settings->ap_name) > 0);
  WiFi.mode(stationMode ? WIFI_AP_STA : WIFI_AP);
  char apName[64];
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
  AsyncOta.listen(&webServer);
  webServer.addHandler(&ws);
  webServer.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("http://" + request->client()->localIP().toString() +
                      "/");
  });
  webServer.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(
        200, "image/x-icon", FAVICON_ICO_PROGMEM_ARRAY, FAVICON_ICO_SIZE);
    response->addHeader("Cache-Control", "max-age=3600");
    request->send(response);
  });

  webServer.on("/autoupdate", HTTP_GET, [](AsyncWebServerRequest *request) {
    String serializedJson;
    serializeJson(generateOwieStatusJson(), serializedJson);
    request->send(200, "application/json", serializedJson);
  });
  
  webServer.on("/metadata", HTTP_GET, [](AsyncWebServerRequest *request) {
    String serializedJson;
    serializeJson(generateMetadataJson(), serializedJson);
    request->send(200, "application/json", serializedJson);
  });

  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(
      200,"text/html", INDEX_HTML_PROGMEM_ARRAY, INDEX_HTML_SIZE);
      request->send(response);
  });
  webServer.on("/owie.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(
        200, "application/javascript", OWIE_JS_PROGMEM_ARRAY, OWIE_JS_SIZE);
    response->addHeader("Cache-Control", "max-age=3600");
    request->send(response);
  });
  webServer.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(
        200, "text/css", STYLES_CSS_PROGMEM_ARRAY, STYLES_CSS_SIZE);
    response->addHeader("Cache-Control", "max-age=3600");
    request->send(response);
  });
  
  webServer.on("/wifi", HTTP_ANY, [](AsyncWebServerRequest *request) {
    switch (request->method()) {
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
  webServer.on("/settings", HTTP_ANY, [](AsyncWebServerRequest *request) {
    switch (request->method()) {
      case HTTP_POST:
        const auto apSelfPassword = request->getParam("pw", true);
        const auto apSelfName = request->getParam("apselfname", true);
        const auto wifiPower = request->getParam("wifipower", true);
        if (apSelfPassword == nullptr ||
            apSelfPassword->value().length() >
                sizeof(Settings->ap_self_password) ||
            (apSelfPassword->value().length() < 8 &&
             apSelfPassword->value().length() >
                 0)) {  // this check is necessary so the user can't set a too
                        // small password and thus the network wont' show up
          request->send(400, "text/html",
                        "AP password must be between 8 and 31 characters");
          return;
        }
        if (apSelfName == nullptr ||
            apSelfName->value().length() > sizeof(Settings->ap_self_name) ||
            (apSelfName->value().length() < 1 &&
             apSelfName->value().length() > 0)) {
          request->send(400, "text/html", "Invalid Custom AP Name.");
          return;
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

  webServer.on("/battery", HTTP_POST, [](AsyncWebServerRequest *request) {
    const auto resetType = request->getParam("type", true);
  
    if (strcmp(resetType->value().c_str(), "stats") == 0) {
      relay->getBatteryFuelGauge().reset();
      request->send(200, "text/html", "Battery stats resetted!");
      return;
    }
    if (strcmp(resetType->value().c_str(), "settings") == 0) {
      Settings->battery_state = BatteryStateMsg_init_default;
      saveSettings();
      request->send(200, "text/html", "Battery settings resetted!");
      return;
    }
    request->send(400, "text/html",
                        "You must provide a correct type to be resettet (stats||settings)!");
      return;
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
