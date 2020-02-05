#include <SoftwareSerial.h>
#include "CSE7766.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>

#ifndef APSSID
#define APSSID "pump-monitor"
#define APPSK  ""
#endif


#define PIN_LED 13
#define PIN_BUTTON 0
#define PIN_RELAY 12
#define SEL_PIN  16
#define CF1_PIN  14
#define CF_PIN   13
#define UPDATE_TIME 1000

#define LED_ON() digitalWrite(PIN_LED, LOW)
#define LED_OFF() digitalWrite(PIN_LED, HIGH)
#define LED_TOGGLE() digitalWrite(PIN_LED, digitalRead(PIN_LED) ^ 0x01)


#define EERELAY 0
#define EETIME 10
#define EECURRENT 20
/* Set these to your desired credentials. */
const char *ssid = APSSID;
const char *password = APPSK;

float current = 0, voltage = 0, activePower = 0, energy = 0, onTime = 0, onCurrent = 0, onTimeCounter = 0;
bool relayState = 0, buttonState, lastButtonState = LOW, readState = 0; // the previous reading from the input pin
unsigned long lastDebounceTime = 0, debounceDelay = 50, mLastTime = 0;;   // the debounce time; increase if the output flickers
byte buttonCounter = 0;
bool protectionState = 1;


ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
CSE7766 myCSE7766;

const char INDEX_HTML[] =
  "<!DOCTYPE HTML>"
  "<html>"
  "<head>"
  "<meta name = \"viewport\" content = \"width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0\">"
  "<title>Pump-Monitor</title>"
  "<style>"
  "\"body { background-color: #808080; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }\""
  "</style>"
  "</head>"
  "<body>"
  "<form action=\"/submit\" method=\"get\">"
  "  <table>"
  "   <tr>"
  "      <td><label for=\"onTime\">Reset-Time in Min: </label></td>"
  "      <td><input style=\"width:145px\" id=\"onTime\" type=\"text\" name=\"onTime\"</td>"
  "    </tr>"
  "    <tr>"
  "      <td><label for=\"onCurrent\">Current: </label></td>"
  "      <td><input style=\"width:145px\" id=\"onCurrent\" type=\"text\" name=\"onCurrent\"></td>"
  "    </tr>"
  "    <tr>"
  "      <td></td>"
  "      <td><input style=\"width:150px\"  type=\"submit\" value=\"Save\"></td>"
  "    </tr>"
  "  </table>"
  "</form>"
  "<br>"
  "<form action=\"/on\">"
  "  <input style=\"width:150px\" type=\"submit\" value=\"Relay ON\" />"
  "</form>"
  "<form action=\"/off\">"
  "  <input style=\"width:150px\" type=\"submit\" value=\"Relay OFF\" />"
  "</form>"
  "<br>"
  "<form action=\"/protectionOn\">"
  "  <input style=\"width:150px\" type=\"submit\" value=\"Protection ON\" />"
  "</form>"
  "<form action=\"/protectionOff\">"
  "  <input style=\"width:150px\" type=\"submit\" value=\"Protection OFF\" />"
  "</form>"
  "<br>"
  "<form action=\"/measure\">"
  "  <input style=\"width:150px\" type=\"submit\" value=\"Status\" />"
  "</form>"
  "<br>"
  "<form action=\"/update\">"
  "  <input style=\"width:150px\" type=\"submit\" value=\"Update\" />"
  "</form>"
  "</body>"
  "</html>";

void relayOff() {
  digitalWrite(PIN_RELAY, LOW);
  relayState = 0;
  EEPROM.write(EERELAY, relayState);
  EEPROM.commit();
}

void relayOn() {
  digitalWrite(PIN_RELAY, HIGH);
  relayState = 1;
  EEPROM.write(EERELAY, relayState);
  EEPROM.commit();
}

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleProtectionOn() {
  protectionState = 1;
  server.send(200, "text/html", "Protection On");
}

void handleProtectionOff() {
  protectionState = 0;
  server.send(200, "text/html", "Protection Off");
}

void handleOn() {
  relayOn();
  server.send(200, "text/html", "Relay On");
}

void handleOff() {
  relayOff();
  server.send(200, "text/html", "Relay Off");
}

void handleMeasure() {
  String message = "";
  message += "Current: \t";
  message += String(current);
  message += "<br>";
  message += "Voltage: \t";
  message += String(voltage);
  message += "<br>";
  message += "Active Power: \t";
  message += String(activePower);
  message += "<br>";
  message += "Energy: \t";
  message += String(energy);
  message += "<br>";
  message += "<br>";
  message += "Reset Time in Minutes: \t";
  message += String(onTime);
  message += "<br>";
  message += "On Time Counter in Seconds: \t";
  message += String(onTimeCounter);
  message += "<br>";
  message += "On Current: \t";
  message += String(onCurrent);
  message += "<br>";
  message += "<br>";
  message += "Relay State: \t";
  if (relayState)
    message += "ON";
  else
    message += "OFF";
  message += "<br>";
  message += "Protection State: \t";
  if (protectionState)
    message += "ON";
  else
    message += "OFF";
  message += "<br>";

  server.send(200, "text/html", message);
}

void handleSubmit()       // Wird ausgefuehrt wenn "http://<ip address>/1.html" aufgerufen wurde
{
  if (server.args() > 0 ) {
    for ( uint8_t i = 0; i < server.args(); i++ ) {
      if (server.argName(i) == "onCurrent") {
        onCurrent = server.arg(i).toFloat();
        EEPROM.put(EECURRENT, onCurrent);
      }
      if (server.argName(i) == "onTime") {
        onTime = server.arg(i).toFloat();
        EEPROM.put(EETIME, onTime);
      }
    }
  }

  String message = "";
  message += "Current:";
  message += String(onCurrent);
  message += "<br>";
  message += "Time:";
  message += String(onTime);
  message += "<br>Saved!";
  server.send(200, "text/html", message);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  LED_OFF();

  EEPROM.begin(512);
  EEPROM.get(EECURRENT, onCurrent);
  EEPROM.get(EETIME, onTime);
  relayState = EEPROM.read(EERELAY);

  pinMode(PIN_RELAY, OUTPUT);
  if (relayState)
    digitalWrite(PIN_RELAY, HIGH);
  else
    digitalWrite(PIN_RELAY, LOW);

  // Initialize
  myCSE7766.setRX(1);
  myCSE7766.begin(); // will initialize serial to 4800 bps

  /* You can remove the password parameter if you want the AP to be open. */
  WiFi.softAP(ssid, password);
  server.on("/", handleRoot);
  server.on("/submit", handleSubmit);
  server.on("/measure", handleMeasure);
  server.on("/protectionOn", handleProtectionOn);
  server.on("/protectionOff", handleProtectionOff);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  MDNS.begin("pump-monitor", WiFi.softAPIP());
  httpUpdater.setup(&server);
  server.begin();
  MDNS.addService("http", "tcp", 80);

  LED_ON();
  delay(3000);
}

void loop() {
  static unsigned long mLastTime = 0;
  readState = digitalRead(PIN_BUTTON);

  if ((millis() - mLastTime) >= UPDATE_TIME) {
    // Time
    mLastTime = millis();
    myCSE7766.handle();

    current = myCSE7766.getCurrent();
    voltage = myCSE7766.getVoltage();
    activePower = myCSE7766.getActivePower();
    energy = myCSE7766.getEnergy();

    if (protectionState) {
      // toggle led
      LED_TOGGLE();
      if (current >= onCurrent)
        onTimeCounter++;
      else
        onTimeCounter = 0;
    } else {
      LED_ON();
      onTimeCounter = 0;
    }


    if (!readState) {
      buttonCounter++;
      if (buttonCounter >= 3) {
        protectionState = !protectionState;
        onTimeCounter = 0;
        LED_OFF();
        delay(100);
        LED_ON();
        delay(100);
        LED_OFF();
        delay(100);
        LED_ON();
        delay(100);
        LED_OFF();
        delay(100);
        LED_ON();
        delay(100);
      }
    } else {
      buttonCounter = 0;
    }
  }


  if (readState != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (readState != buttonState) {
      buttonState = readState;
      if (buttonState == LOW) {
        if (digitalRead(PIN_RELAY)) {
          relayOff();
          onTimeCounter = 0;
        }
        else
        {
          relayOn();
          onTimeCounter = 0;
        }
      }
    }
  }
  lastButtonState = readState;

  if (onTimeCounter >= (onTime * 60)) {
    relayOff();
    onTimeCounter = 0;
  }

  server.handleClient();
  MDNS.update();
}
