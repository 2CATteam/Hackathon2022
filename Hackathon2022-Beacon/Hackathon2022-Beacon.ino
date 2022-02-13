//Libraries
//Connects to WiFi
#include <ESP8266WiFi.h>
#include <EEPROM.h>

void setup() {
  //Write the ID
  EEPROM.begin(4);
  //Set the initial ID
  //EEPROM.write(0, 255); EEPROM.commit();
  //Read this device's ID
  int id = EEPROM.read(0);
  //Turn on WiFi
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Electric Boogie Beacon " + String(id));
}

void loop() {
  // put your main code here, to run repeatedly:

}
