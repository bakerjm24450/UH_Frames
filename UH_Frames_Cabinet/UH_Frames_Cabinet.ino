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

// Relay controls
#define OFF 1             // relays are active low
#define ON 0

// Pins controlling the cabinet doors
#define DOOR1 12
#define DOOR2 13

// Whether doors are open or locked
#define LOCKED false
#define OPEN true

// Soft AP config
const char ssid[] = "UH_Jefferson_1";
const char passwd[] = "&G%$bmIX^64Tx$dc2dPSQ3r@";
const int ap_channel = 2;

// OTA config
const char OTAName[] = "UH_Jefferson_Cabinet";         // A name and a password for the OTA service
const char OTAPassword[] = "3Z@isoBD8i&47Bc3p9JxSR$M";

// standard MQTT setup
const unsigned int mqttPort = 1883;
const unsigned int maxSubscriptions = 30;
const unsigned int maxRetainedTopics = 30;

// MDNS name
const char localName[] = "uh_jefferson_cabinet";      // name will be uh_jefferson_cabinet.local

// web server on port 80
ESP8266WebServer server(80); 

// status of each picture frame (on or off)
bool frame1Status = false;
bool frame2Status = false;

// time of last update from each picture frame
unsigned long frame1UpdateTime = 0;
unsigned long frame2UpdateTime = 0;

// status of the doors
bool doorStatus = LOCKED;

// system status
bool apStatus = false;
bool mdnsStatus = false;
bool webServerStatus = false;
bool otaStatus = false;
bool mqttStatus = false;

//*************************************************************************
// Function prototypes
//*************************************************************************
bool setupAP();                           // wifi access point setup
bool setupMdns();                         // MDNS server
bool setupMqttBroker();                   // server for MQTT traffic
bool setupWebServer();
bool setupOTA();                          // Over The Air software updates

void openDoor(bool openCmd);              // open or close the doors

// handle MQTT message
void frameStatusCallback(uint32_t *client, const char* topic, uint32_t topicLen,
                         const char *data, uint32_t dataLen);

// HTTP handlers                       
void handleStatusRequest();
void handleDoor();
void handleRestartRequest();
void handleNotFound();


//********************************************************************************
// Initialize the system. Set up all the servers and initialize the relays
// controlling the doors.
//********************************************************************************
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

  // set up the wifi network
  apStatus = setupAP();

  // set up MDNS server
  mdnsStatus = setupMdns();

  // set up MQTT broker
  mqttStatus = setupMqttBroker();

  // set up the web server
  webServerStatus = setupWebServer();

  // set up the OTA server for updates
  otaStatus = setupOTA();
}


//************************************************************************
// loop -- handle web requests and OTA requests. Note that MQTT requests
// are handled elsewhere by the callback functions.
//************************************************************************
void loop(void) {
  // handle web requests
  server.handleClient();

  // check for OTA updates
  ArduinoOTA.handle();
}


//*************************************************************************
// Set up the Wifi access point (AP)
// Returns true (success) or false (failure)
//************************************************************************
bool setupAP()
{
  Serial.print("Setting up AP ... ");
  
  WiFi.mode(WIFI_AP);
  
  // set up AP as a hidden network
  if (!WiFi.softAP(ssid, passwd, ap_channel, true)) {
    Serial.println("Failed to initialize soft AP");
    return false;
  }

  Serial.println("Done");

  return true;
}

//***************************************************************************
// Set up the MDNS server to handle local name requests.
// Returns true if successful
//***************************************************************************
bool setupMdns()
{
  // start up the server
  if (MDNS.begin(localName)) {
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


//****************************************************************************
// Initialize the MQTT broker to handle pub/sub traffic. Note we also subscribe
// to messages from the picture frames.
// Returns true if successful
//****************************************************************************
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


//************************************************************************
// Initialize the web server to serve up status info.
// Returns true if successful
//************************************************************************
bool setupWebServer()
{
  // callbacks for status, opening the door, and reset
  server.on("/", handleStatusRequest); 
  server.on("/OpenDoor", handleDoor);
  server.on("/Restart", handleRestartRequest);
  server.onNotFound(handleNotFound);

  server.begin();                           // Actually start the server
  Serial.println("HTTP server started");

  return true;
}

//*************************************************************************
// Set up the Over The Air service to receive software updates.
// Returns true if successful
//*************************************************************************
bool setupOTA()
{ // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  // Callbacks
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

//************************************************************************
// Open or close the door locks
// Input: openCmd = ON to open the doors, OFF to close them
//************************************************************************
void openDoor(bool openCmd)
{
  digitalWrite(DOOR1, openCmd ? ON : OFF);
  digitalWrite(DOOR2, openCmd ? ON : OFF);
}

//************************************************************************
// Process received messages from the frames and set the frame's status. We
// expect the topics/data to be "/Frame1/status on" or "/Frame1/status off", and 
// similar for Frame2.
// Note that we also record the time that the msg was received, so that we can
// figure out if a frame has stopped responding (i.e., the battery is dead).
// Input:
//    client = MQTT client
//    topic = MQTT topic that was received
//    topicLen = length of topic string
//    data = content of MQTT topic msg
//    dataLen = length of data string
//************************************************************************
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

  // if both frames are on and doors are locked, then open the doors
  openDoor(frame1Status && frame2Status && !doorStatus);
  doorStatus = frame1Status && frame2Status;    // remember current state of door
}



//*******************************************************************************
// Generate the web page for the system status.
//*******************************************************************************
void handleStatusRequest() {
  String htmlCode;
  unsigned long currentTime = millis();

  htmlCode += "<!DOCTYPE html>";
  htmlCode += "<html>";
  htmlCode += "<head>";
  htmlCode += "<meta http-equiv=\"refresh\" content=\"20\">";
  htmlCode += "<title>Cabinet Status</title>";
  htmlCode += "</head>";

  htmlCode += "<body>";

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

  htmlCode += "</body>";
  htmlCode += "</html>";

  server.send(200, "text/html", htmlCode);
}

//***********************************************************************************
// Handle web request to open the cabinet doors
//***********************************************************************************
void handleDoor() {
  // open the doors
  openDoor(true);

  // send them back to the status web page
  server.sendHeader("Location", "/");     
  server.send(303);                       
}

//**********************************************************************************
// Handle web request to restart the system
//**********************************************************************************
void handleRestartRequest() {
  // send them back to status page
  server.sendHeader("Location", "/");
  server.send(303);

  // wait for web redirect to finish
  delay(1000);

  // restart the system
  ESP.restart();
}

//************************************************************************************
// Handle any invalid web request.
//************************************************************************************
void handleNotFound() {
  server.send(404, "text/plain", "404: Not found");
}
