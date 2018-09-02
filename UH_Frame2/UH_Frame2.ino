// For Frame 2 -- system includes ESP8266 connected to PN532 RFID sensor.
// We look every 5 seconds for an RFID tag. We connect to WiFi and
// publish an MQTT msg whenever there's a change in the tag detection.
// We also send a msg every minute if there's no change, just as a watchdog
// or keep-alive msg.
//
// Used libraries from https://github.com/elechouse/PN532 and
//  https://github.com/i-n-g-o/esp-mqtt-arduino
//
// Author: Mac Baker
// Revision history:
//  1.0 (8/13/2018) -- initial version
//  2.0 (9/1/2018) -- modified to send tag UID as MQTT data

//#define MQTT_DEBUG_ON 1   // un-comment to turn on debug msgs for mqtt cliet
//#define DEBUG 1           // debug msgs for pn532
#define FRAME_DEBUG 1     // debug msgs for the frame

extern "C" {
#include "user_interface.h"
}

#include <Wire.h>
#include <ESP8266WiFi.h>
#include "MQTT.h"
#include <ArduinoOTA.h>
#include <PN532_I2C.h>
#include <PN532.h>


String clientId("Frame2");      // mqtt client name


// OTA config
const char OTAName[] = "UH_Jefferson_Frame2";         // A name and a password for the OTA service
const char OTAPassword[] = "";
bool otaStatus = false;

// wifi config
const char ssid[] = "";
const char passwd[] = "";

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

//**************************************************************************
// Function prototypes
uint32_t  getTag();             // search for the rfid tag
void publishTag(uint32_t tag);  // publish tag to mqtt broker

void connectPn532();            // initialize pn532 chip

void connectWifi();             // connect to network
void disconnectWifi();          // disconnect from network

void setupMqtt();               // setup MQTT callbacks
void connectMqtt();             // connect to MQTT broker
void disconnectMqtt();          // disconnect from MQTT broker

void setupOTA();                // start up server for OTA updates

void mqttConnectedCb();         // callback when connected to mqtt broker
void mqttDisconnectedCb();      // when disconnected from broker
void mqttPublishedCb();         // when a msg is published successfully


//**********************************************************************************
// System initialization
//**********************************************************************************
void setup()
{
  // use LED for status info
  pinMode(0, OUTPUT);
  digitalWrite(0, LOW);  // on initially

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

  // prepare to connect to mqtt
  setupMqtt();

  // connect to wifi  and the MQTT broker
  connectWifi();
  connectMqtt();


  // set up OTA for updates
  setupOTA();
}




//******************************************************************************************
// Every 5 seconds, look for a tag. Send an MQTT message at least once a minute, or as soon
// as we see a change in tag.
//******************************************************************************************
void loop() {
  static int count = 0;           // how long since we last sent an update?
  static uint32_t oldTag = -1;     // last tag we saw (0 means no tag detected)
  uint32_t tag;                   // tag we're sensing

  count++;                        // send a msg every  12 times (12x5 sec = 1 minute)

  // look for a tag
  tag = getTag();

  // time  to send a message? Either there's a change in tag, or it's been a minute
  if ( (tag != oldTag) || (count >= 12) ) {

    // send a msg
    publishTag(tag);

    // reset the count
    count = 0;
  }

  // remember tag for next time
  oldTag = tag;

  // while we're on wifi, check for firmware updates
  ArduinoOTA.handle();

  // wait 5 seconds and check again
  delay(5000);
}


//*****************************************************************************
// Search for a tag. We read the pn532, returning the tag that we find (0 if
// no tag is detected. Note that the 4-byte tag is turned into a 32-bit uint.
//*****************************************************************************
uint32_t getTag()
{
#ifdef FRAME_DEBUG
  Serial.print("Looking for tag...");
#endif

  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};   // buffer to store returned UID
  uint8_t uidLength;                       // 4 or 7 bytes depending on card type
  uint32_t tag = 0;                        // UID converted to 32-bit value

  // check for a tag
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 3)) {

#ifdef FRAME_DEBUG
    Serial.print("Found a tag...");
#endif
    // convert bytes to 32-bit uint
    for (uint8_t i = 0; i < 4; i++) {
      tag = (tag << 8) | uid[i];
    }

#ifdef FRAME_DEBUG
    Serial.print("Found tag: ");
    Serial.println(tag);
#endif
  }

#ifdef FRAME_DEBUG
  else {
    Serial.println("No tag found.");
  }
#endif

  // release tag to turn off RF field for pn532
  nfc.inRelease();

  return tag;
}


//***************************************************************************************
// Publish a tag value. If the mqtt publisher is ready, then
// we send a status message (either "on" or "off") to our topic.
// Input: tag -- tag to publish
//***************************************************************************************
void publishTag(uint32_t tag)
{
  //  build the message
  String topic("/" + clientId + "/tag");
  String data(tag);

#ifdef FRAME_DEBUG
  Serial.print("Publishing: "); Serial.print(topic);
  Serial.print(" "); Serial.println(data);
#endif

  // and publish it
  publisherReady = false;
  if (!mqtt.publish(topic, data, 0, 0)) {
#ifdef FRAME_DEBUG
    Serial.println("Can't publish mqtt message");
#endif
  }

  // wait until it's sent (timeout in 30 sec)
  for (int i = 0; !publisherReady && i < 3000; i++) {
    delay(10);
  }

  // if timeout, then reset
  if (!publisherReady) {
    disconnectMqtt();
    disconnectWifi();
    ESP.restart();
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

//*****************************************************************************
// Disconnect from wifi network.
//*****************************************************************************
void disconnectWifi()
{
  WiFi.disconnect();
}


//*****************************************************************************
// Setup the callback functions for the MQTT connection.
//*****************************************************************************
void setupMqtt()
{
  // set up callback functions
  mqtt.onConnected(mqttConnectedCb);
  mqtt.onDisconnected(mqttDisconnectedCb);
  mqtt.onPublished(mqttPublishedCb);
}


//******************************************************************************
// Connect to mqtt broker. Note that if connection can't be made, this routine will restart the ESP
//******************************************************************************
void connectMqtt()
{
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

    mqtt.disconnect();
    
    delay(30000);
    ESP.restart();
  }

}


//*****************************************************************************************
// Disconnect from the MQTT server.
//*****************************************************************************************
void disconnectMqtt()
{
  mqtt.disconnect();

  // wait until we disconnect
  for (int i = 0; connectedToBroker && i < 1000; ++i) {
    delay(10);
  }

  if (connectedToBroker) {
#ifdef FRAME_DEBUG
    Serial.println("Can't disconnect from MQTT broker -- restarting in 10 seconds");
#endif

    delay(10000);
    ESP.restart();
  }
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

  // turn off led for visual check
  digitalWrite(0, HIGH);
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

  // turn on led since we're not connected
  digitalWrite(0, LOW);

  // try to reconnect
  disconnectWifi();             // see if resetting wifi helps
  connectWifi();
  connectMqtt();

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


