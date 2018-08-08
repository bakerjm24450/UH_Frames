// For the cabinet with the hidden doors -- system includes ESP8266 and two relays to
// open the hidden doors. The ESP8266 runs an MQTT broker and processes MQTT messages
// from two picture frames. When both frames signal, the doors are unlocked.
//
// Used libraries from https://github.com/martin-ger/uMQTTBroker
// Revision history:
//  1.0 (8/7/2018) -- initial version

#include <ESP8266WiFi.h>
#include <uMQTTBroker.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>   // Include the WebServer library
#include <ArduinoOTA.h>

ESP8266WebServer server(80);    // Create a webserver object that listens for HTTP request on port 80

void handleRoot();              // function prototypes for HTTP handlers
void handleNotFound();

// Soft AP config
const char ssid[] = "UH_Jefferson_1";
const char passwd[] = "&G%$bmIX^64Tx$dc2dPSQ3r@";
const int ap_channel = 1;

// OTA config
const char OTAName[] = "UH_Jefferson_Cabinet";         // A name and a password for the OTA service
const char OTAPassword[] = "3Z@isoBD8i&47Bc3p9JxSR$M";

// standard MQTT setup
const unsigned int mqttPort = 1883;
const unsigned int maxSubscriptions = 10;
const unsigned int maxRetainedTopics = 10;

// status of each picture frame (on or off)
bool frame1Status = false;
bool frame2Status = false;

// time of last update from each picture frame
unsigned long frame1UpdateTime = 0;
unsigned long frame2UpdateTime = 0;

// system status
bool apStatus = false;
bool mdnsStatus = false;
bool webServerStatus = false;
bool otaStatus = false;
bool mqttStatus = false;

// Relay controls
#define OFF 1             // relays are active low
#define ON 0

// Pins controlling the cabinet doors
#define DOOR1 12
#define DOOR2 13

// Open or close the door locks
void openDoor(bool openCmd)
{
  digitalWrite(DOOR1, openCmd ? ON : OFF);
  digitalWrite(DOOR2, openCmd ? ON : OFF);
}

// Process received messages from the frames and set the frame's status
void frameStatusCallback(uint32_t *client, const char* topic, uint32_t topicLen,
                         const char *data, uint32_t dataLen)
{
  // sanitize the input -- no overflows. Then, convert to a String
  char topicArray[32];
  if (topicLen > 31) topicLen = 31;
  memcpy(topicArray, topic, topicLen);
  topicArray[topicLen] = '\0';
  String topicStr(topicArray);

  char dataArray[32];
  if (dataLen > 31) dataLen = 31;
  memcpy(dataArray, data, dataLen);
  dataArray[dataLen] = '\0';
  String dataStr(dataArray);

  Serial.print("Received topic: "); Serial.print(topicStr);
  Serial.print(", data: "); Serial.println(dataStr);

  // process the message
  if (topicStr == "/Frame1/status") {
    frame1Status = (dataStr == "on") ? true : false;
    frame1UpdateTime = millis();
  }

  else if (topicStr == "/Frame2/status") {
    frame2Status = (dataStr == "on") ? true : false;
    frame2UpdateTime =  millis();
  }

  // if both frames are on, then open the doors
  openDoor(frame1Status && frame2Status);
}

// Set up the access point (AP)
// Returns true (success) or false (failure)
bool setupAP()
{
  // set up AP as a hidden network
  Serial.print("Setting up AP ... ");
  if (!WiFi.softAP(ssid, passwd, ap_channel, true)) {
    Serial.println("Failed to initialize soft AP");
    return false;
  }

  Serial.println("Done");

  return true;
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

bool setupMqttBroker()
{
  // register the callback to handle received messages
  MQTT_server_onData(frameStatusCallback);

  // set up MQTT broker
  Serial.print("Starting MQTT broker ... ");
  if (!MQTT_server_start(mqttPort, maxSubscriptions, maxRetainedTopics)) {
    Serial.println("Failed to initialize MQTT broker");
    return false;
  }

  Serial.println("Done");

  // subscribe to the two frames
  MQTT_local_subscribe((unsigned char *) "/Frame1/status", 0);
  MQTT_local_subscribe((unsigned char *) "/Frame2/status", 0);

  Serial.println("Waiting for messages...");

  return true;
}

bool setupWebServer()
{
  server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
  server.on("/OpenDoor", handleDoor);
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

void setup()
{
  // digital outs for the doors
  pinMode(DOOR1, OUTPUT);
  pinMode(DOOR2, OUTPUT);

  // lock the doors
  openDoor(false);

  // set up serial port for diagnostics
  Serial.begin(115200);
  Serial.println();

  // set up the wifi network and MQTT broker
  apStatus = setupAP();
  mdnsStatus = setupMdns();
  mqttStatus = setupMqttBroker();
  webServerStatus = setupWebServer();
  otaStatus = setupOTA();
}

void loop(void) {
  server.handleClient();                    // Listen for HTTP requests from clients

  ArduinoOTA.handle();                      // listen for OTA updates
}

void handleRoot() {
  String htmlCode;
  unsigned long currentTime = millis();

  // output system status
  htmlCode += "<h3>System Status</h3>";

  htmlCode += "<p>AP status: ";
  htmlCode += (apStatus) ? "ON" : "OFF";
  htmlCode += "</p>";

  htmlCode += "<p>MDNS status: ";
  htmlCode += (mdnsStatus) ? "ON" : "OFF";
  htmlCode += "</p>";

  htmlCode += "<p>Web Server status: ";
  htmlCode += (webServerStatus) ? "ON" : "OFF";
  htmlCode += "</p>";

  htmlCode += "<p>MQTT status: ";
  htmlCode += (mqttStatus) ? "ON" : "OFF";
  htmlCode += "</p>";

  htmlCode += "<p>OTA status: ";
  htmlCode += (otaStatus) ? "ON" : "OFF";
  htmlCode += "</p>";

  // output frame status
  htmlCode += "<hr>";
  htmlCode += "<h3>Frame status:</h3>";
  htmlCode += "<p>Frame 1: ";
  htmlCode += (frame1Status) ? "ON " : "OFF";
  if (currentTime - frame1UpdateTime > 20000) {
    // frame has quit responding
    htmlCode += " -- NOT RESPONDING";
  }
  htmlCode += "</p>";

  htmlCode += "<p>Frame 2: ";
  htmlCode += (frame2Status) ? "ON " : "OFF";
  if (currentTime - frame2UpdateTime > 20000) {
    htmlCode += " -- NOT RESPONDING";
  }
  htmlCode += "</p>";

  // add button to open cabinet doors
  htmlCode += "<hr>";
  htmlCode += "<form action=\"/OpenDoor\" method=\"POST\"><input type=\"submit\" value=\"Open Cabinet\"></form>";

  // add button to restart system
  htmlCode += "<hr>";
  htmlCode += "<form action=\"/Restart\" method=\"POST\"><input type=\"submit\" value=\"Restart System\"></form>";

  server.send(200, "text/html", htmlCode);
}

// open the cabinet doors
void handleDoor() {
  openDoor(true);
  server.sendHeader("Location", "/");       // Add a header to respond with a new location for the browser to go to the home page again
  server.send(303);                         // Send it back to the browser with an HTTP status 303 (See Other) to redirect
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
