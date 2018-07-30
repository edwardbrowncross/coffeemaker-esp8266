#include <FS.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson

#define REF_PIN 16

#define HOSTNAME "coffeemaker"
#define AP_NAME "Coffee Maker"

#define THRESHOLD_LOW 800
#define THRESHOLD_HIGH 950

#define FLASH_PERIOD 3000
#define BREW_TIME 540000

#define LOGGING true

ESP8266WebServer server(80);
HTTPClient http;

bool configChanged = false;
char mqttServer[50];

int lightMeasurement;
bool lightIsOn = false;


void debugLog (String name, String message) {
  if (!LOGGING) {
    return;
  }
  Serial.println(String("")+"["+name+"] "+message);
}

void handleServer () {
  String res;
  res += "I am a coffee maker.\n";
  res += "Light measurement is " + String(lightMeasurement) + "\n";
  res += "My light is " + String(lightIsOn ? "on" : "off") + ".\n";
  server.send(200, "text/plain", res);
}

void handleTurnOn () {
  digitalWrite(LED_BUILTIN, LOW);
  debugLog("LDR", "Bright");
}

void handleTurnOff () {
  digitalWrite(LED_BUILTIN, HIGH);
  debugLog("LDR", "Dark");
}

void initPins () {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(REF_PIN, OUTPUT);
  digitalWrite(REF_PIN, HIGH);
}

void saveConfigCallback () {
  debugLog("CONFIG", "Config set by user");
  configChanged = true;
}

bool initConfig () {
  debugLog("CONFIG", "Loading config file...");
  if (!SPIFFS.begin()) {
    debugLog("CONFIG", "Failed to mount file system");
    return false;
  }
  if (!SPIFFS.exists("/config.json")) {
    debugLog("CONFIG", "Config file not found");
    return false;
  }
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    debugLog("CONFIG", "Failed to open config file");
    return false;
  }
  size_t size = configFile.size();
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  debugLog("CONFIG", "Config file loaded. Parsing...");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  if (!json.success()) {
    debugLog("CONFIG", "Failed to parse config JSON");
    return false;
  }
  debugLog("CONFIG", "Config file parsed");
  bool success = true;
  strcpy(mqttServer, json["mqttServer"]);
  success = success && strlen(mqttServer) != 0;
  configFile.close();
  return success;
}

bool saveConfig () {
  debugLog("CONFIG", "Saving config file...");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["mqttServer"] = mqttServer;
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    debugLog("CONFIG", "Failed to open config file for writing");
  }
  json.printTo(configFile);
  configFile.close();
}

void initWifi (bool forcePortal) {
  debugLog("WIFI", "Attempting to start WIFI");
  WiFiManagerParameter custMQTT("MQTT Server", "***.iot.eu-west-2.amazonaws.com", mqttServer, 50);
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custMQTT);
  if (forcePortal) {
    wifiManager.startConfigPortal(AP_NAME);
  } else {
    wifiManager.autoConnect(AP_NAME);
  }
  strcpy(mqttServer, custMQTT.getValue());
  debugLog("WIFI", "Connected! IP address: " + WiFi.localIP());
  if (configChanged) {
    saveConfig();
  }
}

void initServer () {
  server.onNotFound(handleServer);
  server.begin();
  debugLog("Server", "Server started");
}

bool initMDNS () {
  if (!MDNS.begin(HOSTNAME)) {
    return false;
  }
  MDNS.addService("http", "tcp", 80);
  debugLog("MDNS", "MDNS started");
  return true;
}

void setup() {
  Serial.begin(115200);

  initPins();
  bool configLoaded = initConfig();
  initWifi(!configLoaded);
  initServer();
  initMDNS();
}

void loop() {
  lightMeasurement = analogRead(A0);
  if (lightIsOn && lightMeasurement < THRESHOLD_LOW) {
    lightIsOn = false;
    handleTurnOff();
  } else if (!lightIsOn && lightMeasurement > THRESHOLD_HIGH) {
    lightIsOn = true;
    handleTurnOn();
  }
  server.handleClient();
  delay(20);
}