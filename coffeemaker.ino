#include <FS.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h> // https://projects.eclipse.org/projects/technology.paho/downloads
#include <AWSWebSocketClient.h> // https://github.com/odelot/aws-mqtt-websockets


#define REF_PIN 16

#define HOSTNAME "coffeemaker"
#define AP_NAME "Coffee Maker"

#define THRESHOLD_LOW 800
#define THRESHOLD_HIGH 950

#define FLASH_PERIOD 3000
#define BREW_TIME 540000

#define MQTT_PORT 443
#define MQTT_ID "coffee_maker"

#define LOGGING true

ESP8266WebServer server(80);
HTTPClient http;

const int maxMQTTpackageSize = 512;
const int maxMQTTMessageHandlers = 1;
AWSWebSocketClient awsClient(1000, 10000);
PubSubClient client(awsClient);

bool configChanged = false;
char mqttServer[52];
char mqttTopic[66];
char awsKeyID[22];
char awsSecret[42];
char awsRegion[12];

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

void mqttCallback (char* topic, byte* payload, unsigned int len) {
  debugLog("MQTT", "receiving message on topic" + String(topic) + ":");
  for (int i = 0; i < len; i++) { Serial.print((char)payload[i]); }
  Serial.println();
}

void mqttSend (String field, String value) {
  String payload = "{\"state\":{\"reported\":{\"" + field + "\":" + value + "}}}";
  char buf[100];
  payload.toCharArray(buf, 100);
  int rc = client.publish(mqttTopic, buf);
}

bool initConfig () {
  debugLog("CONFIG", "Loading config file...");
  if (!SPIFFS.begin()) {
    debugLog("CONFIG", "Failed to mount file system");
    return false;
  }
  if (SPIFFS.exists("/config.json.fault")) {
    debugLog("CONFIG", "Config fault file found. Deleting config");
    if (!SPIFFS.remove("/config.json")) {
      debugLog("CONFIG", "Failed to delete config file");
    }
    if (!SPIFFS.remove("/config.json.fault")) {
      debugLog("CONFIG", "Failed to delete config fault file");
    }
    return false;
  }
  if (!SPIFFS.exists("/config.json")) {
    debugLog("CONFIG", "Config file not found");
    return false;
  }
  File configFault = SPIFFS.open("/config.json.fault", "w");
  configFault.print("1");
  configFault.close();
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
  strcpy(mqttTopic, json["mqttTopic"]);
  success = success && strlen(mqttTopic) != 0;
  strcpy(awsKeyID, json["awsKeyID"]);
  success = success && strlen(awsKeyID) != 0;
  strcpy(awsSecret, json["awsSecret"]);
  success = success && strlen(awsSecret) != 0;
  strcpy(awsRegion, json["awsRegion"]);
  success = success && strlen(awsRegion) != 0;
  configFile.close();
  if (!SPIFFS.remove("/config.json.fault")) {
    debugLog("CONFIG", "Failed to delete config fault file");
  }
  return success;
}

bool saveConfig () {
  debugLog("CONFIG", "Saving config file...");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["mqttServer"] = mqttServer;
  json["mqttTopic"] = mqttTopic;
  json["awsKeyID"] = awsKeyID;
  json["awsSecret"] = awsSecret;
  json["awsRegion"] = awsRegion;
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    debugLog("CONFIG", "Failed to open config file for writing");
  }
  json.printTo(configFile);
  configFile.close();
}

void initWifi (bool forcePortal) {
  debugLog("WIFI", "Attempting to start WIFI");
  WiFiManagerParameter custMQTT("MQTT Server", "MQTT Server", mqttServer, 51);
  WiFiManagerParameter custTopic("MQTT Topci", "MQTT Topic", mqttTopic, 65);
  WiFiManagerParameter custAWSID("AWS Access Key ID", "AWS Access Key ID", awsKeyID, 21);
  WiFiManagerParameter custAWSSec("AWS Secret Access Key", "AWS Secret Access Key", awsSecret, 41);
  WiFiManagerParameter custAWSReg("AWS Region", "AWS Region", awsRegion, 10);
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custMQTT);
  wifiManager.addParameter(&custTopic);
  wifiManager.addParameter(&custAWSID);
  wifiManager.addParameter(&custAWSSec);
  wifiManager.addParameter(&custAWSReg);
  if (forcePortal) {
    wifiManager.startConfigPortal(AP_NAME);
  } else {
    wifiManager.autoConnect(AP_NAME);
  }
  strcpy(mqttServer, custMQTT.getValue());
  strcpy(mqttTopic, custTopic.getValue());
  strcpy(awsKeyID, custAWSID.getValue());
  strcpy(awsSecret, custAWSSec.getValue());
  strcpy(awsRegion, custAWSReg.getValue());
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

void initAWS () {
  awsClient.setAWSRegion(awsRegion);
  awsClient.setAWSDomain(mqttServer);
  awsClient.setAWSKeyID(awsKeyID);
  awsClient.setAWSSecretKey(awsSecret);
  awsClient.setUseSSL(true);
}

bool initMQTT () {
  debugLog("MQTT", "Connecting to MQTT endpoint");
  if (client.connected()) {    
    client.disconnect ();
  }  
  client.setServer(mqttServer, MQTT_PORT);
  if (!client.connect(MQTT_ID)) {
    debugLog("MQTT", "Failed to make connection. ");
    return false;
  }
  debugLog("MQTT", "Connection established");
  client.setCallback(mqttCallback);
  client.subscribe(mqttTopic);
  return true;
}

void setup() {
  Serial.begin(115200);

  initPins();
  bool configLoaded = initConfig();
  initWifi(!configLoaded);
  initServer();
  initMDNS();
  initAWS();
  initMQTT();
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
  if (awsClient.connected()) {    
      client.loop();
  } else {
    initMQTT();
  }
  delay(20);
}