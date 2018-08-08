// For Frame 1 -- system includes ESP8266 connected to PN532 RFID sensor.
// On startup, we connect to the cabinet's wifi network and mqtt broker.
// We then start looking every 5 seconds for the RFID tag.
//
// Used libraries from https://github.com/elechouse/PN532 and
//  https://github.com/i-n-g-o/esp-mqtt-arduino
//
// Author: Mac Baker
// Revision history:
//  1.0 (7/26/2018) -- initial version

#include <Wire.h>
#include <ESP8266WiFi.h>
#include <MQTT.h>
#include <ESP8266WebServer.h>   // Include the WebServer library
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <PN532_I2C.h>
#include <PN532.h>

// uncomment this to turn on debug messages
#define FRAME_DEBUG 1

String clientId("Frame1");
const uint32_t DESIRED_TAG = 0xced29ab2;    // this is the tag we're looking for

ESP8266WebServer server(80);    // Create a webserver object that listens for HTTP request on port 80

void handleRoot();              // function prototypes for HTTP handlers
void handleNotFound();

// OTA config
const char OTAName[] = "UH_Jefferson_Frame1";         // A name and a password for the OTA service
const char OTAPassword[] = "3Z@isoBD8i&47Bc3p9JxSR$M";
bool otaStatus = false;

const char ssid[] = "UH_Jefferson_1";
const char passwd[] = "&G%$bmIX^64Tx$dc2dPSQ3r@";
const unsigned int mqttPort = 1883;
const char brokerIpAddr[] = "192.168.4.1";

PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);

MQTT mqtt(clientId.c_str(), brokerIpAddr, mqttPort);
bool connectedToBroker = false;

bool foundTag = false;

// Callback when connected to MQTT broker
void myConnectedCb()
{
#ifdef FRAME_DEBUG
  Serial.println("connected to MQTT server");
#endif

  connectedToBroker = true;

  // turn on led for visual check
  digitalWrite(0, LOW);
}

// Callback when disconnected from MQTT broker
void myDisconnectedCb()
{
#ifdef FRAME_DEBUG
  Serial.println("Disconnected");
#endif

  connectedToBroker = false;

  // turn off led
  digitalWrite(0, HIGH);

  // try to reconnect
  mqtt.connect();

}

void myPublishedCb()
{
#ifdef FRAME_DEBUG
  Serial.println("published.");
#endif
}

void myDataCb(String& topic, String& data)
{
  // do nothing
}


// Search for the desired tag
bool checkForTag()
{
#ifdef FRAME_DEBUG
  Serial.println("Looking for tag...");
#endif

  uint8_t success;
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};   // buffer to store returned UID
  uint8_t uidLength;                       // 4 or 7 bytes depending on card type
  bool foundTag = false;                    // if correct tag is present

  // check for tag (timeout if we don't see one in 100 ms)
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {

#ifdef FRAME_DEBUG
    Serial.println("Found a tag");
#endif
    // did we find the right tag?
    uint32_t tag = 0;
    for (uint8_t i = 0; i < 4; i++) {
      tag = (tag << 8) | uid[i];
    }

    foundTag = (uidLength == 4) && (tag == DESIRED_TAG);

#ifdef FRAME_DEBUG
    if (foundTag) {
      Serial.println("Found it!");
    }
    else {
      Serial.println("Wrong tag");
    }
#endif

    // release any tags
    nfc.inRelease(0);

  }

  return foundTag;
}


// Publish whether or not we've found the tag
void publishTag(bool foundTag)
{
  // now publish the message
  String topic("/" + clientId + "/status");
  String data((foundTag) ? "on" : "off");

#ifdef FRAME_DEBUG
  Serial.print("Publishing: "); Serial.print(topic);
  Serial.print(" "); Serial.println(data);
#endif

  mqtt.publish(topic, data);
}

// Set up wifi connection, keep trying until successful.
void connectWifi()
{
  bool connectedWifi = false;

  // connect to wifi access point
  WiFi.begin(ssid, passwd);

#ifdef FRAME_DEBUG
  Serial.print("Starting WiFi connection: ");
#endif

  // wait for connection
  for (int i = 0; !connectedWifi && i < 1000; ++i) {
#ifdef FRAME_DEBUG
    Serial.print(".");
#endif

    delay(10);

    connectedWifi = (WiFi.status() == WL_CONNECTED);
  }

  // did we connect?
  if (!connectedWifi)  {
    // still not connected, so reset
#ifdef FRAME_DEBUG
    Serial.println("Can't connect to WiFi, retrying in 30 seconds.");
#endif

    // wait 30 seconds and retry
    delay(30000);

    ESP.restart();
  }

  else {
#ifdef FRAME_DEBUG
    Serial.println("Connected");
#endif
  }

}


// Connect to mqtt broker.
// Note that if connection can't be made, this routine will restart the ESP
void connectMqtt()
{
  // connect to MQTT broker
#ifdef FRAME_DEBUG
  Serial.println("Setting up MQTT callbacks");
#endif

  mqtt.onConnected(myConnectedCb);
  mqtt.onDisconnected(myDisconnectedCb);
  mqtt.onPublished(myPublishedCb);
  mqtt.onData(myDataCb);

#ifdef FRAME_DEBUG
  Serial.println("Connecting to MQTT broker");
#endif

  mqtt.connect();

  // wait until we're connected
  for (int i = 0; !connectedToBroker && i < 1000; ++i) {
    delay(10);
  }

  // if still not connected, restart
  if (!connectedToBroker) {
#ifdef FRAME_DEBUG
    Serial.println("Can't connect to MQTT broker -- restarting in 30 seconds");
#endif

    delay(30000);
    ESP.restart();
  }

}


bool setupMdns()
{
  if (MDNS.begin("uhcabinet")) {              // Start the mDNS responder for _uhcabinet.local
    Serial.println("mDNS responder started");

    // respond to http queries
    MDNS.addService("http", "tcp", 80);

    return true;
  }
  else {
    Serial.println("Error setting up MDNS responder!");
    return false;
  }
}


bool setupWebServer()
{
  server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
  server.on("/Restart", restartSystem);
  server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"

  server.begin();                           // Actually start the server
  Serial.println("HTTP server started");

  return true;
}

// Set up the Over The Air service.
bool setupOTA()
{ // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\r\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready\r\n");

  return true;
}

// initialize PN532 board for reading RFID
void connectPn532()
{
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  for (int i = 0; !versiondata && i < 10; i++) {
    delay(200);

    versiondata = nfc.getFirmwareVersion();
  }

  if (! versiondata) {
#ifdef FRAME_DEBUG
    Serial.print("Didn't find PN53x board");
#endif
    ESP.restart();
  }
  else {
#ifdef FRAME_DEBUG
    Serial.println("Initialized PN532 board");
#endif
  }

  // configure to read RFID tag
  nfc.SAMConfig();

}

void setup() {

  pinMode(0, OUTPUT);
  digitalWrite(0, HIGH);

#ifdef FRAME_DEBUG
  Serial.begin(115200);
#endif

  // sleep for a little bit
  delay(1000);

  // connect to wifi
  connectWifi();

  setupMdns();

  // set up webserver
  setupWebServer();

  // set up OTA for updates
  otaStatus = setupOTA();

  // initialize PN532 board
  connectPn532();

  // connect to mqtt
  connectMqtt();
}

void loop() {
  foundTag = checkForTag();

  publishTag(foundTag);

  server.handleClient();

  ArduinoOTA.handle();

  delay(5000);
}

void handleRoot() {
  String htmlCode;

  // output system status
  htmlCode += "<h3>System Status for ";
  htmlCode += clientId;
  htmlCode += "</h3>";

  htmlCode += "<p>MQTT status: ";
  htmlCode += (connectedToBroker) ? "OK" : "Not connected";
  htmlCode += "</p>";

  htmlCode += "<p>OTA status: ";
  htmlCode += (otaStatus) ? "OK" : "OFF";
  htmlCode += "</p>";

  htmlCode += "<p>RFID sensor: ";
  uint32_t versiondata = nfc.getFirmwareVersion();
  for (int i = 0; !versiondata && i < 10; i++) {
    delay(200);

    versiondata = nfc.getFirmwareVersion();
  }
  htmlCode += (versiondata) ? "OK" : "Not found";
  htmlCode += "</p>";

  // output frame status
  htmlCode += "<hr>";
  htmlCode += "<p>Tag: ";
  htmlCode += (foundTag) ? "Present" : "Not Present";
  htmlCode += "</p>";

  // add button to restart system
  htmlCode += "<hr>";
  htmlCode += "<form action=\"/Restart\" method=\"POST\"><input type=\"submit\" value=\"Restart System\"></form>";

  server.send(200, "text/html", htmlCode);
}

// restart the system
void restartSystem() {
  server.sendHeader("Location", "/");
  server.send(303);

  ESP.restart();
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}
