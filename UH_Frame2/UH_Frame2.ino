// For Frame 2 -- system includes ESP8266 connected to PN532 RFID sensor.
// On startup, we connect to the cabinet's wifi network and mqtt broker.
// We then start looking every 5 seconds for the RFID tag.
//
// Used libraries from https://github.com/elechouse/PN532 and
//  https://github.com/i-n-g-o/esp-mqtt-arduino
//
// Author: Mac Baker
// Revision history:
//  1.0 (8/13/2018) -- initial version

//#define MQTT_DEBUG_ON 1   // un-comment to turn on debug msgs for mqtt cliet
//#define DEBUG 1           // debug msgs for pn532
//#define FRAME_DEBUG 1     // debug msgs for the frame

#include <Wire.h>
#include <ESP8266WiFi.h>
#include "MQTT.h"
#include <ESP8266WebServer.h>   // Include the WebServer library
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <PN532_I2C.h>
#include <PN532.h>


String clientId("Frame2");      // mqtt client name
const uint32_t DESIRED_TAG = 0xbd97730f;    // this is the tag we're looking for

ESP8266WebServer server(80);    // Create a webserver object that listens for HTTP request on port 80

// OTA config
const char OTAName[] = "UH_Jefferson_Frame2";         // A name and a password for the OTA service
const char OTAPassword[] = "3Z@isoBD8i&47Bc3p9JxSR$M";
bool otaStatus = false;

// wifi config
const char ssid[] = "UH_Jefferson_1";
const char passwd[] = "&G%$bmIX^64Tx$dc2dPSQ3r@";

// mqtt broker
const unsigned int mqttPort = 1883;
const char brokerIpAddr[] = "192.168.4.1";

// interface to pn532
PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);

// mqtt client
MQTT mqtt(clientId.c_str(), brokerIpAddr, mqttPort);

// some flags to indicate system status
bool connectedToBroker = false;
bool publisherReady = false;
bool foundTag = false;

//**************************************************************************
// Function prototypes
void checkWifiStatus();         // see if we're still connected to wifi
bool checkForTag();             // search for the rfid tag
void publishTag(bool foundTag); // publish status to mqtt broker

void connectPn532();            // initialize pn532 chip
void connectWifi();             // connect to network
void connectMqtt();             // connect to MQTT broker
void setupWebServer();          // start up a diagnostic web server
void setupOTA();                // start up server for OTA updates

void mqttConnectedCb();         // callback when connected to mqtt broker
void mqttDisconnectedCb();      // when disconnected from broker
void mqttPublishedCb();         // when a msg is published successfully
void mqttDataCb();              // when data is received
void handleRoot();              // function prototypes for HTTP handlers
void handleReset();
void handleNotFound();


//**********************************************************************************
// System initialization
//**********************************************************************************
void setup() 
{
  // use LED for status info
  pinMode(0, OUTPUT);
  digitalWrite(0, HIGH);

  // SDA and SCL pins -- make sure they start in sane state
  pinMode(4, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  
#ifdef FRAME_DEBUG
  Serial.begin(115200);
#endif

  // sleep for a little bit
  delay(100);

  // initialize PN532 board
  connectPn532();

  // connect to wifi
  connectWifi();

  // connect to mqtt
  connectMqtt();

  // set up webserver
  setupWebServer();

  // set up OTA for updates
  setupOTA();
  
}




//******************************************************************************************
// Look for a tag every 5 seconds and publish status to mqtt broker. Also check for web
// requests and ota updates.
//******************************************************************************************
void loop() {
  checkWifiStatus();
  
  foundTag = checkForTag();

  publishTag(foundTag);

  // handle web requests
  server.handleClient();

  // handle OTA updates
  ArduinoOTA.handle();

  // wait 5 seconds and check again
  delay(5000);
}


//****************************************************************************
// Check if we're still connected to the wifi (usual cause would be the server
// may have rebooted). If not connected, wait for 15 sec and reboot to force
// reconnetion.
//****************************************************************************
void checkWifiStatus()
{
  if (!WiFi.isConnected()) {
    // wait a little for server to reboot
    delay(15000);

    // and reboot
    ESP.restart();
  }
}


//*****************************************************************************
// Search for the desired tag. We read the pn532, returning whether or not
// our tag was found.
// Returns true if desired tag is detected, false if not
//*****************************************************************************
bool checkForTag()
{
#ifdef FRAME_DEBUG
  Serial.print("Looking for tag...");
#endif

  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};   // buffer to store returned UID
  uint8_t uidLength;                       // 4 or 7 bytes depending on card type
  bool foundTag = false;                    // if correct tag is present

  // check for a tag
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 3)) {

#ifdef FRAME_DEBUG
    Serial.print("Found a tag...");
#endif
    // did we find the right tag?
    uint32_t tag = 0;
    for (uint8_t i = 0; i < 4; i++) {
      tag = (tag << 8) | uid[i];
    }

    foundTag = (uidLength == 4) && (tag == DESIRED_TAG);

#ifdef FRAME_DEBUG
    if (foundTag) {
      Serial.println("Correct tag");
    }
    else {
      Serial.println("Wrong tag");
    }
#endif

  }

  // release tag to turn off RF field for pn532
  nfc.inRelease();

  return foundTag;
}


//***************************************************************************************
// Publish whether or not we've found the tag. If the mqtt publisher is ready, then
// we send a status message (either "on" or "off") to our topic.
// Input: foundTag -- whether or not we have detected the rfid tag
//***************************************************************************************
void publishTag(bool foundTag)
{
  // don't publish again if we're still waiting on the last message
  if (publisherReady) {
    //  build the message
    String topic("/" + clientId + "/status");
    String data((foundTag) ? "on" : "off");

#ifdef FRAME_DEBUG
    Serial.print("Publishing: "); Serial.print(topic);
    Serial.print(" "); Serial.println(data);
#endif

    // and publish it
    if (mqtt.publish(topic, data, 0, 0)) {

      publisherReady = false;
    }
    else {
#ifdef FRAME_DEBUG
      Serial.println("Can't publish mqtt message");
#endif
    }
  }
  else {
#ifdef FRAME_DEBUG
    Serial.println("Publisher not ready");
#endif
  }

}



//***********************************************************************************
// initialize PN532 board for reading RFID
//***********************************************************************************
void connectPn532()
{
  // 4 = SDA, 5 = SCL
  Wire.begin(4, 5);
  
  nfc.begin();

  // don't run i2c clock too fast (default is 100000)
  Wire.setClock(50000);
  Wire.setClockStretchLimit(3000);    // deal with clock stretching from pn532

  // wait a bit for pn532 to wake up
  delay(100);

  // make sure we can find pn532
  uint32_t versiondata = nfc.getFirmwareVersion();
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

  // don't wait forever
  nfc.setPassiveActivationRetries(2);

}


//******************************************************************************************
// Set up wifi connection, keep trying until successful. If we can't connect after several
// seconds, we give up (the server must be down). We then delay 30 seconds and reset ourself.
//******************************************************************************************
void connectWifi()
{
  bool connectedWifi = false;

  // connect to wifi access point
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, passwd);
  WiFi.mode(WIFI_STA);                // internet advice (but untested) -- set mode both before
                                      // and after begin() to make sure

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

  // did we connect or timeout?
  if (!connectedWifi)  {
    // still not connected, so reset the system
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


//******************************************************************************
// Connect to mqtt broker. Note that if connection can't be made, this routine will restart the ESP
//******************************************************************************
void connectMqtt()
{  
  // set up callback functions
  mqtt.onConnected(mqttConnectedCb);
  mqtt.onDisconnected(mqttDisconnectedCb);
  mqtt.onPublished(mqttPublishedCb);
  mqtt.onData(mqttDataCb);

#ifdef FRAME_DEBUG
  Serial.println("Connecting to MQTT broker");
#endif
  // connect to the broker
  mqtt.connect();

  // wait until we're connected
  for (int i = 0; !connectedToBroker && i < 1000; ++i) {
    delay(20);
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



//*******************************************************************************************
// Set up a simple webserver to serve up some status/diagnostic info.
//*******************************************************************************************
void setupWebServer()
{
  // set up callbacks for handling requests
  server.on("/", handleRoot);              
  server.onNotFound(handleNotFound);       

  // start the server
  server.begin();    

#ifdef FRAME_DEBUG
  Serial.println("HTTP server started");
#endif
}


//****************************************************************************************
// Set up the Over The Air service. This handles updates to the software.
//****************************************************************************************
void setupOTA()
{ 
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  // set up the callbacks
  ArduinoOTA.onStart([]() {
#ifdef FRAME_DEBUG
    Serial.println("Start");
#endif
  });
  
  ArduinoOTA.onEnd([]() {
#ifdef FRAME_DEBUG
    Serial.println("\r\nEnd");
#endif
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
#ifdef FRAME_DEBUG
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
#endif
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
#ifdef FRAME_DEBUG
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
#endif
  });
  
  ArduinoOTA.begin();

  otaStatus = true;
  
#ifdef FRAME_DEBUG
  Serial.println("OTA ready");
#endif
}

//*************************************************************************
// Display system status as a web page.
//*************************************************************************
void handleRoot() {
  String htmlCode;

  htmlCode += "<!DOCTYPE html>";
  htmlCode += "<html>";
  htmlCode += "<head>";
  //  htmlCode += "<meta http-equiv=\"refresh\" content=\"20\">";
  htmlCode += "<title>Frame 2 Status</title>";
  htmlCode += "</head>";

  htmlCode += "<body>";

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
  htmlCode += (nfc.getFirmwareVersion() != 0) ? "OK" : "Not found";
  htmlCode += "</p>";

  // output frame status
  htmlCode += "<hr>";
  htmlCode += "<p>Tag: ";
  htmlCode += (foundTag) ? "Present" : "Not Present";
  htmlCode += "</p>";

  htmlCode += "</body>";
  htmlCode += "</html>";

  server.send(200, "text/html", htmlCode);
}




//***************************************************************************************************
// Handle all other web requests.
//***************************************************************************************************
void handleNotFound() {
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}



//*******************************************************************************
// Callback when connected to MQTT broker. We update the status variables and
// turn on the LED for visual status.
//*******************************************************************************
void mqttConnectedCb()
{
#ifdef FRAME_DEBUG
  Serial.println("connected to MQTT server");
#endif

  connectedToBroker = true;
  publisherReady = true;

  // turn on led for visual check
  digitalWrite(0, LOW);
}

//**********************************************************************************
// Callback when disconnected from MQTT broker. We try to re-connect.
//**********************************************************************************
void mqttDisconnectedCb()
{
#ifdef FRAME_DEBUG
  Serial.println("Disconnected");
#endif

  // update status variables
  connectedToBroker = false;
  publisherReady = false;

  // turn off led since we're not connected
  digitalWrite(0, HIGH);

  // try to reconnect
  mqtt.connect();

}

//**********************************************************************************
// Callback when  a message is published. We update the status flag.
//**********************************************************************************
void mqttPublishedCb()
{
  publisherReady = true;

#ifdef FRAME_DEBUG
  Serial.println("published.");
#endif
}

//*********************************************************************************
// Callback for receiving data from an mqtt subscription. We don't have any subscriptions,
// so this is empty.
//*********************************************************************************
void mqttDataCb(String& topic, String& data)
{
#ifdef FRAME_DEBUG
  Serial.print("MQTT recv: ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(data);
#endif
}


