/*
  7sClock: A smart 7-segment LED clock using ESP8266
  - WiFiManager for WiFi + config portal
  - NTP time sync with automatic DST using TZ strings
  - Custom LED segment control for hours/minutes
  - Configurable via web interface (colors, brightness, blink, 24h, sync interval)
  - Dark mode web UI with color preview
  - Auto-dimming
  - Mobile-friendly web interface with reboot
*/

#include <ESP8266WiFi.h>
#include <ESPAsyncWiFiManager.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>


#define HOUR_PIN    D2
#define MINUTE_PIN  D6
#define NUM_LEDS    15

Adafruit_NeoPixel hourStrip(NUM_LEDS, HOUR_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel minuteStrip(NUM_LEDS, MINUTE_PIN, NEO_GRB + NEO_KHZ800);

AsyncWebServer server(80);
DNSServer dns;

const uint8_t segmentMap[10] = {
    0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
    0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011
};

const uint8_t minuteSegmentMap[10] = {
    0b1110111, 0b0010010, 0b1011101, 0b1011011, 0b0111010,
    0b1101011, 0b1101111, 0b1010010, 0b1111111, 0b1111011
};

struct ClockConfig {
  String timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
  String ntpServer = "pool.ntp.org";
  bool blinkDots = true;
  uint8_t brightness = 50;
  String segmentColor = "#FF0000";
  bool use24h = false;
  bool hideLeadingZero24h = false;
  bool autoDim = true;
  uint8_t dimStartHour = 22;
  uint8_t dimEndHour = 6;
  uint32_t ntpSyncInterval = 60;
};

ClockConfig config;
bool dotState = true;

void saveConfig() {
  File f = LittleFS.open("/config.json", "w");
  if (f) {
    f.printf("{\"timezone\":\"%s\",\"ntpServer\":\"%s\",\"blinkDots\":%s,\"brightness\":%d,\"color\":\"%s\",\"use24h\":%s,\"hideLeadingZero24h\":%s,\"autoDim\":%s,\"dimStart\":%d,\"dimEnd\":%d,\"ntpSyncInterval\":%u}",
             config.timezone.c_str(), config.ntpServer.c_str(), config.blinkDots ? "true" : "false",
             config.brightness, config.segmentColor.c_str(), config.use24h ? "true" : "false",
             config.hideLeadingZero24h ? "true" : "false", config.autoDim ? "true" : "false",
             config.dimStartHour, config.dimEndHour, config.ntpSyncInterval);
    f.close();
  }
}

void loadConfig() {
  if (!LittleFS.exists("/config.json")) return;
  File f = LittleFS.open("/config.json", "r");
  if (!f) return;
  String data = f.readString();
  f.close();
  JsonDocument doc;
  deserializeJson(doc, data);
  config.timezone = doc["timezone"].as<String>();
  config.ntpServer = doc["ntpServer"].as<String>();
  config.blinkDots = doc["blinkDots"] | true;
  config.brightness = doc["brightness"] | 50;
  config.segmentColor = doc["color"] | "#FF0000";
  config.use24h = doc["use24h"] | true;
  config.hideLeadingZero24h = doc["hideLeadingZero24h"] | true;
  config.autoDim = doc["autoDim"] | true;
  config.dimStartHour = doc["dimStart"] | 22;
  config.dimEndHour = doc["dimEnd"] | 6;
  config.ntpSyncInterval = doc["ntpSyncInterval"] | 60;
}

uint32_t parseColor(String hexColor) {
  hexColor.replace("#", "");
  return strtoul(hexColor.c_str(), NULL, 16);
}

void setupTime() {
  configTime(config.timezone.c_str(), config.ntpServer.c_str());
}

const uint8_t hourSegmentOrder[7] = {1, 0, 4, 5, 6, 2, 3};
const uint8_t minuteSegmentOrder[7] = {5, 4, 6, 3, 0, 2, 1};

void drawDigit(Adafruit_NeoPixel &strip, int startIndex, int digit, bool isMinute = false) {
  uint8_t segments = isMinute ? minuteSegmentMap[digit] : segmentMap[digit];
  const uint8_t* mapping = isMinute ? minuteSegmentOrder : hourSegmentOrder;
  for (int i = 0; i < 7; i++) {
    bool on = (segments >> (6 - i)) & 1;
    int ledIndex = startIndex + mapping[i];
    strip.setPixelColor(ledIndex, on ? parseColor(config.segmentColor) : 0);
  }
}

void updateDisplay() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int hour = timeinfo.tm_hour;
  if (!config.use24h) {
    if (hour > 12) hour -= 12;
    if (hour == 0) hour = 12;
  }
  int minute = timeinfo.tm_min;
  int h1 = hour / 10;
  int h2 = hour % 10;
  int m1 = minute / 10;
  int m2 = minute % 10;

  hourStrip.clear();
  minuteStrip.clear();
  hourStrip.setPixelColor(0, dotState ? parseColor(config.segmentColor) : 0);
  minuteStrip.setPixelColor(0, dotState ? parseColor(config.segmentColor) : 0);

  if (h1 > 0 || (config.use24h && !config.hideLeadingZero24h)) drawDigit(hourStrip, 8, h1);
  drawDigit(hourStrip, 1, h2);
  drawDigit(minuteStrip, 1, m1, true);
  drawDigit(minuteStrip, 8, m2, true);

  int dynamicBrightness = config.brightness;
  if (config.autoDim && (timeinfo.tm_hour >= config.dimStartHour || timeinfo.tm_hour < config.dimEndHour)) {
    dynamicBrightness = config.brightness / 3;
  }
  hourStrip.setBrightness(dynamicBrightness);
  minuteStrip.setBrightness(dynamicBrightness);

  hourStrip.show();
  minuteStrip.show();
}

void setupWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = R"rawliteral(
      <!DOCTYPE html>
      <html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>
      body { font-family: sans-serif; background: #111; color: #fff; padding: 1em; }
      h1 { text-align: center; }
      input, select, button { width: 100%; padding: 0.5em; margin: 0.5em 0; border-radius: 5px; border: none; }
      input, select { background: #222; color: #fff; }
      button { background: #0af; color: white; font-weight: bold; }
      label { display: block; margin-top: 1em; font-weight: bold; }
      .footer { margin-top: 2em; text-align: center; font-size: 0.9em; color: #888; }
      </style><title>7 Segment Clock settings</title></head><body><h1>7 Segment Clock settings</h1>
      <form method='POST' action='/save'>
      <label>Timezone</label>
      <select name='timezone'>
        <option value="CET-1CEST,M3.5.0,M10.5.0/3" %SEL_EUROPE_BERLIN%>Europe/Berlin</option>
        <option value="GMT0BST,M3.5.0/1,M10.5.0" %SEL_EUROPE_LONDON%>Europe/London</option>
        <option value="EST5EDT,M3.2.0/2,M11.1.0" %SEL_NY%>America/New_York</option>
        <option value="PST8PDT,M3.2.0,M11.1.0" %SEL_LA%>America/Los_Angeles</option>
        <option value="JST-9" %SEL_TOKYO%>Asia/Tokyo</option>
        <option value="UTC0" %SEL_UTC%>UTC</option>
        <option value="AEST-10AEDT,M10.1.0,M4.1.0/3" %SEL_SYDNEY%>Australia/Sydney</option>
        <option value="IST-5:30" %SEL_INDIA%>Asia/Kolkata</option>
        <option value="MSK-3" %SEL_MOSCOW%>Europe/Moscow</option>
        <option value="HKT-8" %SEL_HONGKONG%>Asia/Hong_Kong</option>
      </select>
      <label>NTP Server</label><input name='ntpServer' value='%NTPSERVER%'>
      <label>NTP Sync Interval (min)</label><input name='ntpSyncInterval' type='number' min='1' max='1440' value='%NTPSYNC%'>
      <label>LED Brightness</label><input type='range' name='brightness' min='5' max='255' value='%BRIGHTNESS%'>
      <label>LED Color</label><input type='color' name='color' value='%COLOR%'>
      <label><input type='checkbox' name='blinkDots' %BLINKDOTS%> Blink Dots</label>
      <label><input type='checkbox' name='use24h' %USE24H%> 24h Format</label>
      <label><input type='checkbox' name='hideLeadingZero24h' %HIDEZERO24H%> Hide leading zero (24h)</label>
      <label><input type='checkbox' name='autoDim' %AUTODIM%> Auto Dim</label>
      <label>Dim Start Hour</label><input name='dimStart' type='number' min='0' max='23' value='%DIMSTART%'>
      <label>Dim End Hour</label><input name='dimEnd' type='number' min='0' max='23' value='%DIMEND%'>
      <button type='submit'>Save</button></form>
      <form method='POST' action='/reboot'><button>Reboot</button></form>
      <br><form method="POST" action="/update" enctype="multipart/form-data">
      <input type="file" name="update">
      <button>Upload OTA</button>
      </form>
      <div id="msg"></div>
      <script>
      document.querySelector("form").onsubmit=function(e){document.getElementById('msg').innerText="Saved.";};
      </script>
      <div class='footer'>7sClock ESP8266</div></body></html>
    )rawliteral";

    html.replace("%NTPSERVER%", config.ntpServer);
    html.replace("%BRIGHTNESS%", String(config.brightness));
    html.replace("%COLOR%", config.segmentColor);
    html.replace("%BLINKDOTS%", config.blinkDots ? "checked" : "");
    html.replace("%USE24H%", config.use24h ? "checked" : "");
    html.replace("%HIDEZERO24H%", config.hideLeadingZero24h ? "checked" : "");
    html.replace("%AUTODIM%", config.autoDim ? "checked" : "");
    html.replace("%DIMSTART%", String(config.dimStartHour));
    html.replace("%DIMEND%", String(config.dimEndHour));
    html.replace("%NTPSYNC%", String(config.ntpSyncInterval));
    html.replace("%SEL_EUROPE_BERLIN%", config.timezone == "CET-1CEST,M3.5.0,M10.5.0/3" ? "selected" : "");
    html.replace("%SEL_EUROPE_LONDON%", config.timezone == "GMT0BST,M3.5.0/1,M10.5.0" ? "selected" : "");
    html.replace("%SEL_NY%", config.timezone == "EST5EDT,M3.2.0/2,M11.1.0" ? "selected" : "");
    html.replace("%SEL_LA%", config.timezone == "PST8PDT,M3.2.0,M11.1.0" ? "selected" : "");
    html.replace("%SEL_TOKYO%", config.timezone == "JST-9" ? "selected" : "");
    html.replace("%SEL_UTC%", config.timezone == "UTC0" ? "selected" : "");
    html.replace("%SEL_SYDNEY%", config.timezone == "AEST-10AEDT,M10.1.0,M4.1.0/3" ? "selected" : "");
    html.replace("%SEL_INDIA%", config.timezone == "IST-5:30" ? "selected" : "");
    html.replace("%SEL_MOSCOW%", config.timezone == "MSK-3" ? "selected" : "");
    html.replace("%SEL_HONGKONG%", config.timezone == "HKT-8" ? "selected" : "");
    request->send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("timezone", true)) config.timezone = request->getParam("timezone", true)->value();
    if (request->hasParam("ntpServer", true)) config.ntpServer = request->getParam("ntpServer", true)->value();
    if (request->hasParam("brightness", true)) config.brightness = request->getParam("brightness", true)->value().toInt();
    if (request->hasParam("color", true)) config.segmentColor = request->getParam("color", true)->value();
    config.blinkDots = request->hasParam("blinkDots", true);
    config.use24h = request->hasParam("use24h", true);
    config.hideLeadingZero24h = request->hasParam("hideLeadingZero24h", true);
    config.autoDim = request->hasParam("autoDim", true);
    if (request->hasParam("dimStart", true)) config.dimStartHour = request->getParam("dimStart", true)->value().toInt();
    if (request->hasParam("dimEnd", true)) config.dimEndHour = request->getParam("dimEnd", true)->value().toInt();
    if (request->hasParam("ntpSyncInterval", true)) config.ntpSyncInterval = request->getParam("ntpSyncInterval", true)->value().toInt();
    saveConfig();
    setupTime();
    String html = R"rawliteral(
      <!DOCTYPE html>
      <html><head><meta name='viewport' content='width=device-width, initial-scale=1'>
      <meta http-equiv='refresh' content='2;url=/'><style>
      body { font-family: sans-serif; background: #111; color: #fff; padding: 1em; }
      h1 { text-align: center; }
      input, select, button { width: 100%; padding: 0.5em; margin: 0.5em 0; border-radius: 5px; border: none; }
      input, select { background: #222; color: #fff; }
      button { background: #0af; color: white; font-weight: bold; }
      label { display: block; margin-top: 1em; font-weight: bold; }
      .footer { margin-top: 2em; text-align: center; font-size: 0.9em; color: #888; }
      </style><title>7 Segment Clock save</title></head><body><h1>Saved! setup time...</h1></body><html>)rawliteral";
    request->send(200, "text/html", html);
    delay(1000);
  });

  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    String html = R"rawliteral(
      <!DOCTYPE html>
      <html><head><meta name='viewport' content='width=device-width, initial-scale=1'>
      <meta http-equiv='refresh' content='5;url=/'><style>
      body { font-family: sans-serif; background: #111; color: #fff; padding: 1em; }
      h1 { text-align: center; }
      input, select, button { width: 100%; padding: 0.5em; margin: 0.5em 0; border-radius: 5px; border: none; }
      input, select { background: #222; color: #fff; }
      button { background: #0af; color: white; font-weight: bold; }
      label { display: block; margin-top: 1em; font-weight: bold; }
      .footer { margin-top: 2em; text-align: center; font-size: 0.9em; color: #888; }
      </style><title>7 Segment Clock restart</title></head><body><h1>Rebooting...</h1></body><html>)rawliteral";
    request->send(200, "text/html", html);
    delay(1000);
    ESP.restart();
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    String html = R"rawliteral(
      <!DOCTYPE html>
      <html><head><meta name='viewport' content='width=device-width, initial-scale=1'>
      <meta http-equiv='refresh' content='5;url=/'><style>
      body { font-family: sans-serif; background: #111; color: #fff; padding: 1em; }
      h1 { text-align: center; }
      input, select, button { width: 100%; padding: 0.5em; margin: 0.5em 0; border-radius: 5px; border: none; }
      input, select { background: #222; color: #fff; }
      button { background: #0af; color: white; font-weight: bold; }
      label { display: block; margin-top: 1em; font-weight: bold; }
      .footer { margin-top: 2em; text-align: center; font-size: 0.9em; color: #888; }
      </style><title>7 Segment Clock update</title></head><body><h1>Update complete. Rebooting...</h1></body><html>)rawliteral";
    request->send(200, "text/html", html);
    delay(1000);
    ESP.restart();
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
    }
    Update.write(data, len);
    if (final) {
      Update.end(true);
    }
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  LittleFS.begin();
  loadConfig();

  WiFi.hostname("7sclock");
  AsyncWiFiManager wm(&server, &dns);
  wm.setTimeout(180);
  if (!wm.autoConnect("7sClockSetup")) ESP.restart();

  // Start mDNS
  if (MDNS.begin("7sclock")) {
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Error setting up mDNS responder!");
  }

  ArduinoOTA.setHostname("7sclock");

  ArduinoOTA.onStart([]() {
    String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nUpdate complete");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error [%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();

  setupTime();

  hourStrip.begin();
  minuteStrip.begin();

  setupWeb();
}

unsigned long lastBlink = 0;
unsigned long lastSync = 0;

void loop() {
  unsigned long now = millis();
  if (now - lastBlink >= 1000) {
    dotState = config.blinkDots ? !dotState : true;
    lastBlink = now;
    updateDisplay();
  }
  if (now - lastSync >= config.ntpSyncInterval * 60 * 1000UL) {
    setupTime();
    lastSync = now;
  }
  ArduinoOTA.handle();
}
