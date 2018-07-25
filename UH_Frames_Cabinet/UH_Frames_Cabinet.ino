// For the cabinet with the hidden doors -- system includes ESP8266 and two relays to
// open the hidden doors. The ESP8266 runs an MQTT broker and processes MQTT messages
// from two picture frames. When both frames signal, the doors are unlocked.
//
// Used libraries from https://github.com/martin-ger/uMQTTBroker
// Revision history:
//  1.0 (7/25/2018) -- initial version

#include <ESP8266WiFi.h>

const char ssid[] = "UH_Jefferson_1";
const char passwd[] = "&G%$bmIX^64Tx$dc2dPSQ3r@";
const int ap_channel = 1;

void setup()
{
  Serial.begin(115200);
  Serial.println();

  Serial.print("Setting soft-AP ... ");
  boolean result = WiFi.softAP(ssid, passwd, ap_channel, true);
  
  if(result == true)
  {
    Serial.println("Ready");
  }
  else
  {
    Serial.println("Failed!");
  }
}

void loop()
{
  Serial.printf("Stations connected = %d\n", WiFi.softAPgetStationNum());
  delay(3000);
}
