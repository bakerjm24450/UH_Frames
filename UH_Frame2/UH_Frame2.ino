// For Frame 2 -- system includes ESP8266 connected to PN532 RFID sensor, with a
// PL5110 low power controller. On startup, we look to see if the right RFID tag
// is present. If so, we connect to wifi and send an MQTT message to the server.
// We then turn back off (low power controller is set up to wake up every 5 seconds
// or so).
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

#include <PN532_I2C.h>
#include <PN532.h>

#define FRAME_DEBUG 1

#define SLEEP_PIN 13

String clientId("Frame2");

const char ssid[] = "UH_Jefferson_1";
const char passwd[] = "&G%$bmIX^64Tx$dc2dPSQ3r@";
const unsigned int mqttPort = 1883;
const char brokerIpAddr[] = "192.168.4.1";

PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);

const uint32_t DESIRED_TAG = 0xbd97730f;    // this is the tag we're looking for
bool foundTag = false;
bool mqttIsConnected = false;

MQTT mqtt(clientId.c_str(), brokerIpAddr, mqttPort);



void myConnectedCb()
{
#ifdef FRAME_DEBUG
  Serial.println("connected to MQTT server");
#endif

  mqttIsConnected = true;
}

void myDisconnectedCb()
{
#ifdef FRAME_DEBUG
  Serial.println("Disconnected");
#endif

  // disconnected, so we can turn off wifi
  WiFi.disconnect();
}

void myPublishedCb()
{
#ifdef FRAME_DEBUG
  Serial.println("published.");
#endif

  // message was published, so we can disconnect
  mqtt.disconnect();
}

void myDataCb(String& topic, String& data)
{
  // do nothing
}



void setup() {
  // make sure DONE pin is off so we can run
  pinMode(SLEEP_PIN, OUTPUT);
  digitalWrite(SLEEP_PIN, LOW);

#ifdef FRAME_DEBUG
  Serial.begin(115200);
  Serial.println("Starting...");
#endif

  // connect to wifi access point
  bool wifiStatus = WiFi.begin(ssid, passwd);

  Serial.print("Started connection: ");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(10);
  }

  Serial.println("Connected");
  Serial.println(WiFi.isConnected());


  nfc.begin();

  // configure to read RFID tag
  nfc.SAMConfig();

#ifdef FRAME_DEBUG
  Serial.println("Looking for tag...");
#endif

  uint8_t success;
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};   // buffer to store returned UID      
  uint8_t uidLength;                       // 4 or 7 bytes depending on card type
  foundTag = false;                    // if correct tag is present

  // check for tag (timeout if we don't see one in 100 ms)
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
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
#endif

  }

  // release any tags
  nfc.inRelease(0);

  // publish MQTT message
  if (WiFi.isConnected()) {
    Serial.println("Connected");
    digitalWrite(0, HIGH);
    delay(1000);

    Serial.println("Setting up callbacks");
    mqtt.onConnected(myConnectedCb);
    mqtt.onDisconnected(myDisconnectedCb);
    mqtt.onPublished(myPublishedCb);
    mqtt.onData(myDataCb);

    Serial.println("Connecting to broker");
    mqtt.connect();

    // wait until we're connected
//    while (!mqttIsConnected) {
//      delay(10);
//    }

    // now publish the message
    String topic("/" + clientId + "/status");
    String data((foundTag) ? "on" : "off");

#ifdef FRAME_DEBUG
    Serial.print("Publishing: "); Serial.print(topic);
    Serial.print(" "); Serial.println(data);
#endif

    mqtt.publish(topic, data);
  }

  // wait until wifi is disconnected
  while (WiFi.isConnected()) {
    delay(10);
  }
  
#ifdef FRAME_DEBUG
  Serial.println("Going to sleep.");
#endif
}

void loop() {
//  digitalWrite(SLEEP_PIN, HIGH);
  delay(100);
//  digitalWrite(SLEEP_PIN, LOW);
  delay(1000);
}
