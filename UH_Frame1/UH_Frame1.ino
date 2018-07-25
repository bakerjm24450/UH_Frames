// For Frame 1 -- system includes ESP8266 connected to PN532 RFID sensor, with a 
// PL5110 low power controller. On startup, we look to see if the right RFID tag
// is present. If so, we connect to wifi and send an MQTT message to the server.
// We then turn back off (low power controller is set up to wake up every 5 seconds
// or so).
//
// Used libraries from https://github.com/elechouse/PN532
//
// Author: Mac Baker
// Revision history:
//  1.0 (7/19/2018) -- initial version

#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>

#define FRAME_DEBUG 1

#define SLEEP_PIN 13

PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);

const uint32_t DESIRED_TAG = 0xced29ab2;    // this is the tag we're looking for

void setup() {
  // make sure DONE pin is off so we can run
  pinMode(SLEEP_PIN, OUTPUT);
  digitalWrite(SLEEP_PIN, LOW);
  
#ifdef FRAME_DEBUG
  Serial.begin(115200);
  Serial.println("Starting...");
#endif

  nfc.begin();
/*
  Serial.println("Scan I2C bus...");
  for (byte address = 1; address <= 127; address++ )
  {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0)
    {
      Serial.println(address, HEX);
    }
  }

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (versiondata) {
#ifdef FRAME_DEBUG
    Serial.print("Found chip PN5"); Serial.println((versiondata >> 24) & 0xff, HEX);
    Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xff, DEC);
    Serial.print('.'); Serial.println((versiondata >> 8) & 0xff, DEC);
#endif
*/
    // configure to read RFID tag
    nfc.SAMConfig();

#ifdef FRAME_DEBUG
    Serial.println("Looking for tag...");
#endif

    uint8_t success;
    uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};   // buffer to store returned UID
    uint8_t uidLength;                       // 4 or 7 bytes depending on card type

    // check for tag (timeout if we don't see one in 100 ms)
    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100);

    if (success) {
      // did we find the right tag?
      uint32_t foundTag = 0;
      for (uint8_t i = 0; i < 4; i++) {
        foundTag = (foundTag << 8) | uid[i];
      }

      if ((uidLength == 4) && (foundTag == DESIRED_TAG)) {
        // found it!
#ifdef FRAME_DEBUG
        Serial.println("Found it!");
#endif

        // connect to wifi access point

        // publish MQTT message
      }
    }

    // release any tags
    nfc.inRelease(0);
//  }

#ifdef FRAME_DEBUG
  Serial.println("Going to sleep.");
#endif
}

void loop() {
  digitalWrite(SLEEP_PIN, HIGH);
  delay(100);
  digitalWrite(SLEEP_PIN, LOW);
  delay(1000); 
}
