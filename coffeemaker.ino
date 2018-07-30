#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

#define REF_PIN 16

#define HOSTNAME "coffeemaker"

#define THRESHOLD_LOW 800
#define THRESHOLD_HIGH 950

#define FLASH_PERIOD 3000
#define BREW_TIME 540000

ESP8266WebServer server(80);
HTTPClient http;

int lightMeasurement;
bool lightIsOn = false;

void handleServer () {
  String res;
  res += "I am a coffee maker.\n";
  res += "Light measurement is " + String(lightMeasurement) + "\n";
  res += "My light is " + String(lightIsOn ? "on" : "off") + ".\n";
  server.send(200, "text/plain", res);
}

void handleTurnOn () {
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("[LDR] Bright");
}

void handleTurnOff () {
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("[LDR] Dark");
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(REF_PIN, OUTPUT);
  digitalWrite(REF_PIN, HIGH);

  Serial.begin(115200);
  
  Serial.println("[WIFI] Attempting to start WIFI");

  WiFiManager wifiManager;
  wifiManager.autoConnect("CoffeeMaker");

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
  server.handleClient();
  delay(20);
}