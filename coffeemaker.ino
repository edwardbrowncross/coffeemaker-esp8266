#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include "HX711.h"

#define REF_PIN 16
#define SDA_PIN 4
#define SCL_PIN 5

#define SSID "*********"
#define PASS "*********"

#define HOSTNAME "coffeemaker"

#define SLACK_URL "https://hooks.slack.com/services/*****/*****/***************"
#define SLACK_HTTPS_THUMBPRINT "c10d5349d23ee52ba261d59e6f990d3dfd8bb2b3"

#define LED_THRESHOLD_LOW 500
#define LED_THRESHOLD_HIGH 700

#define FLASH_PERIOD 3000
#define BREW_TIME 540000
#define JUG_CLEANING_TIME 60000

#define MAKER_OFF 0
#define MAKER_ON 1
#define MAKER_BREWED 2
#define MAKER_STALE 3

#define JUG_REMOVED 0
#define JUG_PRESENT 1
#define JUG_CLEANING 2

#define SCALE_CALIBRATION 91.57
#define COFFEE_MAKER_WEIGHT 2000
#define COFFEE_JUG_WEIGHT 250
#define COFFEE_PORTION_WEIGHT 250
#define WEIGHT_CHANGE_THRESHOLD 20
#define WEIGHT_SETTLING_TIME 1000
#define WEIGHT_EASING 0

ESP8266WebServer server(80);
HTTPClient http;

HX711 scale(SDA_PIN, SCL_PIN);

int lightMeasurement;
bool lightIsOn = false;
int makerState = MAKER_OFF;
String makerStateNames[4] = { "off", "on", "brewed", "stale" };

uint32_t lastOnTime; 
uint32_t lastOffTime;

int32_t zeroWeight;
int32_t referenceWeight;
int32_t currentWeight;
int32_t weightMeasurement;
int32_t lastLoadedWeight;
int jugState = JUG_PRESENT;
String jugStateNames[3] = { "removed", "present", "MIA" };

uint32_t lastWeightChangeTime;
uint32_t lastJugRemovedTime;
uint32_t lastJugReplacedTime;

void initScale () {
  scale.set_scale(SCALE_CALIBRATION);
}

void handleServer () {
  String res;
  res += "I am a coffee maker.\n";
  res += "Light measurement is " + String(lightMeasurement) + "\n";
  res += "My light is " + String(lightIsOn ? "on" : "off") + ".\n";
  res += "My state is " + makerStateNames[makerState] + ".\n";
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

void handleWeightChange (int32_t delta) {
  int32_t newWeight = currentWeight + delta;
  if (delta > COFFEE_JUG_WEIGHT*0.75 && jugState != JUG_PRESENT) {
    jugState = JUG_PRESENT;
    int32_t loadedDelta = newWeight - lastLoadedWeight;
    if (abs(loadedDelta) > WEIGHT_CHANGE_THRESHOLD) {
      handleCoffeeWeightChange(loadedDelta);
    }
    handleJugReplaced();
  } else if (delta < -COFFEE_JUG_WEIGHT*0.75 && jugState == JUG_PRESENT) {
    jugState = JUG_REMOVED;
    lastJugRemovedTime = millis();
    lastLoadedWeight = currentWeight;
    referenceWeight = newWeight + COFFEE_JUG_WEIGHT;
    handleJugRemoved();
  }

  Serial.print("[Coffee] Delta weight:");
  Serial.println(delta, DEC);
}

void handleCoffeeWeightChange (int delta) {
  if (delta > WEIGHT_CHANGE_THRESHOLD) {
      handleCoffeeGain(delta);
    } else if (delta < -WEIGHT_CHANGE_THRESHOLD) {
      handleCoffeeLoss(-delta);
    }
}

void handleJugReplaced () {
  Serial.println("[Coffee] Jug has been replaced");
}

void handleJugRemoved () {
  Serial.println("[Coffee] Jug has been removed");
}

void handleJugCleaning () {
  Serial.println("[Coffee] Jug has gone for cleaning");
}

void handleCoffeeLoss (int weight) {
  Serial.println((String)"[Coffee] " + weight + "g of coffee was consumed");
  Serial.println((String)"[Coffee] " + getCupsRemaining() + " cups remain");
}

void handleCoffeeGain (int weight) {
  Serial.println((String)"[Coffee] " + weight + "g of coffee was added");
  Serial.println((String)"[Coffee] " + getCupsRemaining() + " cups remain");
}

float getCupsRemaining () {
  if (jugState == JUG_PRESENT) {
    return float(currentWeight - referenceWeight) / COFFEE_PORTION_WEIGHT;
  } else {
    return float(lastLoadedWeight - referenceWeight) / COFFEE_PORTION_WEIGHT;
  }
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

bool weightHasBeenSettledFor (uint32_t t) {
  return millis() - lastWeightChangeTime > t;
}

bool weightHasChangedBy (uint32_t w) {
  return abs(currentWeight - weightMeasurement) > w;
}

bool jugHasBeenGoneFor (uint32_t t) {
  return jugState != JUG_PRESENT && millis() - lastJugRemovedTime > t;
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

  if (weightHasChangedBy(WEIGHT_CHANGE_THRESHOLD) && weightHasBeenSettledFor(WEIGHT_SETTLING_TIME)) {
    int32_t delta = weightMeasurement - currentWeight;
    handleWeightChange(delta);
    currentWeight = weightMeasurement;
}

  if (jugHasBeenGoneFor(JUG_CLEANING_TIME) && jugState == JUG_REMOVED) {
    jugState = JUG_CLEANING;
    handleJugCleaning();
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(REF_PIN, OUTPUT);
  digitalWrite(REF_PIN, HIGH);

  Serial.begin(115200);

  initScale();
  
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
  if (lightIsOn && lightMeasurement < LED_THRESHOLD_LOW) {
    lightIsOn = false;
    handleTurnOff();
  } else if (!lightIsOn && lightMeasurement > LED_THRESHOLD_HIGH) {
    lightIsOn = true;
    handleTurnOn();
  }

  int w = scale.get_units(1);
  if (abs(w - weightMeasurement) > WEIGHT_CHANGE_THRESHOLD) {
    lastWeightChangeTime = millis();
  }
  weightMeasurement = (weightMeasurement*WEIGHT_EASING + w) / (WEIGHT_EASING + 1);

  handleTick();
  server.handleClient();
  delay(20);
}
