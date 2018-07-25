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
