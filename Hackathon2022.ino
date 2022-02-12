//Libraries
//Connects to WiFi
#include <ESP8266WiFI.h>
//Serves HTML files
#include <ESP8266WebServer.h>
//Connects to the websocket server
#include <SocketIoClient.h>
//Reads data from "disk"
#include <EEPROM.h>

//
char ssid[64];
char password[64];
char room[32];
char name[32];
int id;

int broadcasting = 0;
bool transmitting = false;

void setup() {
  // put your setup code here, to run once:
  
}

void loop() {
  // put your main code here, to run repeatedly:

}
