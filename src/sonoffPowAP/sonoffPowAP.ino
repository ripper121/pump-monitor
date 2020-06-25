#include "CSE7766.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include <FS.h>

#define VERSION "1.0"

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

#define APPSK  ""
const char *password = APPSK;
float current = 0, voltage = 0, activePower = 0, energy = 0, onTime = 0, onCurrent = 0, onTimeCounter = 0;
bool relayState = 0, buttonState, lastButtonState = LOW, readState = 0; // the previous reading from the input pin
unsigned long lastDebounceTime = 0, debounceDelay = 50, mLastTime = 0;;   // the debounce time; increase if the output flickers
byte buttonCounter = 0;
bool protectionState = 1;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
MDNSResponder mdns;
CSE7766 myCSE7766;


const char HTML_HEADER[] PROGMEM = R"=====(
<!DOCTYPE HTML>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
    <title>Pump-Guard</title>
    <style>
    </style>
  </head>
  <body>
  <center>
)=====";
const char HTML_FOOTER[] PROGMEM = R"=====(
  </center>
  </body>
</html>
)=====";


String readSpiffs(String file, int line)
{
  String data = "";
  File f = SPIFFS.open( file, "r");
  if (!f) {
    data = "file open failed";
  }else{
    int lineCount = 0; 
    while (f.available()) {    
      String readLine = f.readStringUntil('\n');
      if(lineCount >= line){
        data = readLine;
        break;
      }
      lineCount++; 
    }
  }  
  f.close();
  
  return data;
}

void handleSpiffs()
{
  String data = "";
  File f = SPIFFS.open( "/lang.txt", "r"); // Datei zum lesen öffnen
  if (!f) {
    data = "file open failed";
  }else{
  data = f.readString();
  }  
  f.close(); // Wir schließen die Datei
  
  server.send(200, "text/html", data);
}

void handleReadLine()
{  
  server.send(200, "text/html", readSpiffs("/lang.txt", 0));
}




boolean InitalizeFileSystem() {
  bool initok = false;
  initok = SPIFFS.begin();
  if (!(initok)) // Format SPIFS, of not formatted. - Try 1
  {
    Serial.println("SPIFFS Dateisystem formatiert.");
    SPIFFS.format();
    initok = SPIFFS.begin();
  }
  if (!(initok)) // Format SPIFS. - Try 2
  {
    SPIFFS.format();
    initok = SPIFFS.begin();
  }
  if (initok) {
    Serial.println("SPIFFS ist  OK");
  } else {
    Serial.println("SPIFFS ist nicht OK");
  }
  return initok;
}

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
  String message = (String)HTML_HEADER;
  message += "<a href=\"http://pump-guard/\"><h1>";
  message += readSpiffs("/lang.txt", 0);
  message += "</h1></a>";
  message += "<form action=\"/submit\" method=\"get\"><table><tr><td width=\"200\">";
  message += readSpiffs("/lang.txt", 1);
  message += ":</td>";
  message += "<td><input style=\"width:100px\" id=\"onTime\" type=\"text\" name=\"onTime\" value=\"";
  message += String(onTime);
  message += "\"/></td>";
  message += "</tr><tr><td width=\"150\">";
  message += readSpiffs("/lang.txt", 2);
  message += "</td>";
  message += "<td><input style=\"width:100px\" id=\"onCurrent\" type=\"text\" name=\"onCurrent\" value=\"";
  message += String(onCurrent);
  message += "\"/></td>";
  message += "</tr><tr><td></td><td><input style=\"width:105px\" type=\"submit\" value=\"";
  message += readSpiffs("/lang.txt", 3);
  message += "\"></td></tr></table></form><br>";
  message += "<table><tr><td width=\"150\">";
  message += readSpiffs("/lang.txt", 4);
  message += ":</td>";
  if (relayState){
    message += "<td><input style=\"width:200px;background:green;\" type=\"button\" onclick=\"location.href='/relayOff';\" value=\"";
    message += readSpiffs("/lang.txt", 7);
    message += "\" /></td>";
  }
  else{
    message += "<td><input style=\"width:200px;background:red;\" type=\"button\" onclick=\"location.href='/relayOn';\" value=\"";
    message += readSpiffs("/lang.txt", 6);
    message += "\" /></td>";
  }
  message += "</tr><tr><td width=\"150\">";
  message += readSpiffs("/lang.txt", 5);
  message += ":</td>";
  if (protectionState){
    message += "<td><input style=\"width:200px;background:green;\" type=\"button\" onclick=\"location.href='/protectionOff';\" value=\"";
    message += readSpiffs("/lang.txt", 7);
    message += "\" /></td>";
  }
  else{
    message += "<td><input style=\"width:200px;background:red;\" type=\"button\" onclick=\"location.href='/protectionOn';\" value=\"";
    message += readSpiffs("/lang.txt", 6);
    message += "\" /></td>";
  }
  message += "</tr>";
  message += "<tr><td width=\"150\"><br></td><td><br></td></tr>";
  message += "<tr><td width=\"150\">";
  message += readSpiffs("/lang.txt", 13);
  message += ":</td><td>";
  message += String((float)(onTimeCounter / 60));
  message += " / ";
  message += String(onTime);
  message += " ";
  message += readSpiffs("/lang.txt", 14);
  message += "</td></tr>";
  message += "<tr><td width=\"150\">";
  message += readSpiffs("/lang.txt", 15);
  message += ":</td><td>";
  message += String(current);
  message += " / ";
  message += String(onCurrent);
  message += " A</td></tr>";
  message += "<tr><td width=\"150\">";
  message += readSpiffs("/lang.txt", 16);
  message += ":</td><td>";
  message += String(voltage);
  message += " V</td></tr>";
  message += "<tr><td width=\"150\"><br></td><td><input style=\"width:200px\" type=\"button\" onclick=\"location.href='/';\" value=\"";
  message += readSpiffs("/lang.txt", 17);
  message += "\" />";
  message += "</td></tr></table>";
  message += "<br><br><input style=\"width:200px\" type=\"button\" onclick=\"location.href='/update';\" value=\"";
  message += readSpiffs("/lang.txt", 18);
  message += "\" />";
  message += "<br><br>";
  message += readSpiffs("/lang.txt", 19);
  message += ": " + (String)(VERSION) + "<br>E-Mail: <a href=\"mailto:ripper121@gmail.com\">ripper121@gmail.com</a>" + (String)HTML_FOOTER;
  server.send(200, "text/html", message);
}

void handleProtectionOn() {
  protectionState = 1;
  String message = (String)HTML_HEADER;
  message += "<div>";
  message += readSpiffs("/lang.txt", 10);
  message += "</div><br>";
  message += "<input style=\"width:100px\" type=\"button\" onclick=\"location.href='/';\" value=\"";
  message += readSpiffs("/lang.txt", 12);
  message += "\" />";
  message += (String)HTML_FOOTER;
  server.send(200, "text/html", message);
}

void handleProtectionOff() {
  protectionState = 0;
  String message = (String)HTML_HEADER;
  message += "<div>";
  message += readSpiffs("/lang.txt", 11);
  message += "</div><br>";
  message += "<input style=\"width:100px\" type=\"button\" onclick=\"location.href='/';\" value=\"";
  message += readSpiffs("/lang.txt", 12);
  message += "\" />";
  message += (String)HTML_FOOTER;
  server.send(200, "text/html", message);
}

void handleRelayOn() {
  relayOn();
  String message = (String)HTML_HEADER;
  message += "<div>";
  message += readSpiffs("/lang.txt", 8);
  message += "</div><br>";
  message += "<input style=\"width:100px\" type=\"button\" onclick=\"location.href='/';\" value=\"";
  message += readSpiffs("/lang.txt", 12);
  message += "\" />";
  message += (String)HTML_FOOTER;
  server.send(200, "text/html", message);
}

void handleRelayOff() {
  relayOff();
  String message = (String)HTML_HEADER;
  message += "<div>";
  message += readSpiffs("/lang.txt", 9);
  message += "</div><br>";
  message += "<input style=\"width:100px\" type=\"button\" onclick=\"location.href='/';\" value=\"";
  message += readSpiffs("/lang.txt", 12);
  message += "\" />";
  message += (String)HTML_FOOTER;
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

  String message = (String)HTML_HEADER;
  message += readSpiffs("/lang.txt", 13);
  message += ":";
  message += String(onTime);
  message += "<br>";
  message += readSpiffs("/lang.txt", 15);
  message += ":";
  message += String(onCurrent);
  message += "<div>";
  message += readSpiffs("/lang.txt", 20);
  message += "!</div><br>";
  message += "<input style=\"width:100px\" type=\"button\" onclick=\"location.href='/';\" value=\"";
  message += readSpiffs("/lang.txt", 12);
  message += "\" />";
  message += (String)HTML_FOOTER;
  server.send(200, "text/html", message);
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  LED_OFF();

  InitalizeFileSystem();

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
  char charBuf[25];
  String AP_SSID = WiFi.softAPmacAddress();
  AP_SSID = "pump-guard-" + String(AP_SSID[12]) + String(AP_SSID[13]) + String(AP_SSID[15]) + String(AP_SSID[16]) + '.';
  AP_SSID.toCharArray(charBuf, AP_SSID.length());
  WiFi.mode(WIFI_AP);
  WiFi.softAP(charBuf, password);

  mdns.begin("pump-guard", WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/submit", handleSubmit);
  server.on("/protectionOn", handleProtectionOn);
  server.on("/protectionOff", handleProtectionOff);
  server.on("/relayOn", handleRelayOn);
  server.on("/relayOff", handleRelayOff);
  server.on("/spiffs", handleSpiffs);
  server.on("/readLine", handleReadLine);

  httpUpdater.setup(&server);
  server.begin();
  mdns.addService("http", "tcp", 80);
  
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
  mdns.update();
}
