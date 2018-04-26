#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>

#define REF_PIN 16

#define SSID "*********"
#define PASS "*********"

#define HOSTNAME "coffeemaker"

#define SLACK_URL "https://hooks.slack.com/services/*****/*****/***************"
#define SLACK_HTTPS_THUMBPRINT "c10d5349d23ee52ba261d59e6f990d3dfd8bb2b3"

#define THRESHOLD_LOW 500
#define THRESHOLD_HIGH 700

#define FLASH_PERIOD 3000
#define BREW_TIME 540000

#define MAKER_OFF 0
#define MAKER_ON 1
#define MAKER_BREWED 2
#define MAKER_STALE 3

ESP8266WebServer server(80);
HTTPClient http;

int lightMeasurement;
bool lightIsOn = false;
int makerState = MAKER_OFF;
String stateNames[4] = { "off", "on", "brewed", "stale" };

uint32_t lastOnTime; 
uint32_t lastOffTime;

void handleServer () {
  String res;
  res += "I am a coffee maker.\n";
  res += "Light measurement is " + String(lightMeasurement) + "\n";
  res += "My light is " + String(lightIsOn ? "on" : "off") + ".\n";
  res += "My state is " + stateNames[makerState] + ".\n";
  server.send(200, "text/plain", res);
}

int sendSlackMessage (String message) {
  http.begin(SLACK_URL, SLACK_HTTPS_THUMBPRINT);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpCode = http.POST("payload={\"text\":\"" + message + "\"}");
  http.end();
  if(httpCode > 0) {
    Serial.println("[HTTP] Success:" + http.getString());
  } else {
    Serial.println(String("[HTTP] Fail:") + http.errorToString(httpCode).c_str());
  }
  return httpCode;
}

void handleTurnOn () {
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("[LDR] Bright");
  lastOnTime = millis();
}

void handleTurnOff () {
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("[LDR] Dark");
  lastOffTime = millis();
}

void handleMakerOn () {
  Serial.println("[Coffee] Maker is now on");
  sendSlackMessage(":hotsprings: Coffee is brewing...");
}

void handleMakerBrewed () {
  Serial.println("[Coffee] Coffee is now brewed");
  sendSlackMessage(":coffee: Coffee is ready!");
}

void handleMakerStale () {
  Serial.println("[Coffee] Coffee is now stale");
  sendSlackMessage(":tea: Coffee is going stale.");
}

void handleMakerOff () {
  Serial.println("[Coffee] Maker is now off");
  sendSlackMessage(":weary: Coffee maker is off. Need more coffee.");
}

bool hasBeenOnFor (uint32_t t) {
  return (lightIsOn && millis() - lastOnTime > t);
}

bool hasBeenOffFor (uint32_t t) {
  return (!lightIsOn && millis() - lastOffTime > t);
}

bool hasFlashed (uint32_t t) {
  return (lightIsOn && lastOffTime > 0 && millis() - lastOffTime < t) || (!lightIsOn && lastOnTime > 0 && millis() - lastOnTime < t);
}

void handleTick () {
  if (hasBeenOffFor(2*FLASH_PERIOD) && makerState != MAKER_OFF) {
    makerState = MAKER_OFF;
    handleMakerOff();
  } else if (hasBeenOnFor(2*FLASH_PERIOD) && !hasBeenOnFor(BREW_TIME) && makerState == MAKER_OFF) {
    makerState = MAKER_ON;
    handleMakerOn();
  } else if (hasBeenOnFor(BREW_TIME) && makerState == MAKER_ON) {
    makerState = MAKER_BREWED;
    handleMakerBrewed();
  } else if (hasFlashed(FLASH_PERIOD) && makerState == MAKER_BREWED) {
    makerState = MAKER_STALE;
    handleMakerStale();
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(REF_PIN, OUTPUT);
  digitalWrite(REF_PIN, HIGH);

  Serial.begin(115200);
  
  Serial.printf("[WIFI] Connecting to %s\n", SSID);

  WiFi.disconnect(false);
  WiFi.softAPdisconnect(false);
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(10);
  }

  Serial.print("[WIFI] Connected! IP address:");
  Serial.println(WiFi.localIP());

  server.onNotFound(handleServer);
  server.begin();
  Serial.println("[Server] Server started");

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[MDNS] MDNS started");
  }
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
  handleTick();
  server.handleClient();
  delay(20);
}
