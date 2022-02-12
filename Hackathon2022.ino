//Libraries
//Connects to WiFi
#include <ESP8266WiFi.h>
//Serves HTML files
#include <ESP8266WebServer.h>
//Connects to the webSocket server
#include <SocketIoClient.h>
//Reads bytes from "disk"
#include <EEPROM.h>
//Reads files from disk
#include <LittleFS.h>

//Pin allows to force board to take input
#define WiFiOverride 14

ESP8266WebServer server(80);
SocketIoClient webSocket;

//WiFi settings
char ssid[64];
char password[64];
//The room used for Socket.io
char room[32];
//The name of this user's board
char username[32];
//The ID of this particular board
int id;
//Tracks whether this is connected to the server
bool isConnected = false;

void setup() {
  // Begin serial
  Serial.begin(115200);
  Serial.println("Starting");

  //Read values from EEPROM and put them into memory
  EEPROM.begin(256);
  //SSID
  for (int i = 0; i < 64; i++) {
    ssid[i] = EEPROM.read(i);
  }
  //Password
  for (int i = 0; i < 64; i++) {
    password[i] = EEPROM.read(i + 64);
  }
  //Room
  for (int i = 0; i < 32; i++) {
    room[i] = EEPROM.read(i + 128);
  }
  //Username
  for (int i = 0; i < 32; i++) {
    username[i] = EEPROM.read(i + 160);
  }
  //ID
  //EEPROM.write(193, 0); EEPROM.commit();
  id = EEPROM.read(193);

  //Debug printing
  Serial.println(ssid);
  Serial.println(password);
  Serial.println(room);
  Serial.println(username);
  Serial.println(id);

  LittleFS.begin();

  //Pin all
  pinMode(WiFiOverride, INPUT_PULLUP);

  //Allow for overriding with the pin, forcing board to ask for SSID/Password
  if (digitalRead(WiFiOverride) != LOW) {
    //Go into client mode and try to connect
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    //Wait until we're connected, and if it takes too long (>10 seconds), give up
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(250);
      Serial.print(".");
      attempts++;
      if (attempts > 100) {
        break;
      }
    }
    //Note whether the above was successful
    if (WiFi.status() == WL_CONNECTED) {
      isConnected = true;
    }
  }
  //If we're connected, connect to the server and register event handlers
  if (isConnected) {
    Serial.println("Connected!");

    webSocket.on("connect", handleConnected);
    webSocket.on("disconnect", handleDisconnected);
    webSocket.on("shock", handleShockOn);
    webSocket.on("deshock", handleShockOff);
    webSocket.begin("ec2-13-58-213-225.us-east-2.compute.amazonaws.com", 4098);

    server.on("/", hostRoot);
    server.begin();
  //If disconnected, give this thing its own web server and register handlers
  } else {
    Serial.println("\nNot connected...");
    WiFi.mode(WIFI_AP);
    IPAddress ip(192, 168, 0, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    Serial.println("Electric Boogie " + String(id));
    WiFi.softAP("Electric Boogie " + String(id));
    server.on("/", clientRoot);
    server.on("/submit", submit);
    server.begin();
    Serial.println("Server has been made");
  }

  Serial.println("Setup done");
}

void loop() {
  // put your main code here, to run repeatedly:
  if (isConnected) {
    clientLoop();
  } else {
    serverLoop();
  }
}

void serverLoop() {
  //WiFi.printDiag(Serial);
  server.handleClient();
}

void clientLoop() {
  webSocket.loop();
  server.handleClient();
  //Do GPS stuff and potentially shock
}

void submit() {
  String ssidString = server.arg("ssid");
  String passString = server.arg("password");
  String roomString = server.arg("room");
  String usernameString = server.arg("username");

  Serial.println(ssidString);
  Serial.println(passString);
  Serial.println(roomString);
  Serial.println(usernameString);

  if (ssidString != "") {
    for (int i = 0; i < 64; i++) {
      EEPROM.write(i, ssidString[i]);
    }
  }
  if (passString != "") {
    for (int i = 0; i < 64; i++) {
      EEPROM.write(i + 64, passString[i]);
    }
  }
  if (roomString != "") {
    for (int i = 0; i < 32; i++) {
      EEPROM.write(i + 128, roomString[i]);
    }
  }
  if (usernameString != "") {
    for (int i = 0; i < 32; i++) {
      EEPROM.write(i + 160, usernameString[i]);
    }
  }

  EEPROM.commit();
  server.send(200, "text/plain", "Success!");
  ESP.restart();
}

void clientRoot() {
  File file = LittleFS.open("setup.html", "r");
  server.streamFile(file, "text/html");
  file.close();
}

void hostRoot() {
  File file = LittleFS.open("setup.html", "r");
  server.streamFile(file, "tet/html");
  file.close();
}

void handleConnected(const char* data, size_t length) {
  Serial.println("Connected to ws");
  webSocket.emit("register", (String("\"") + room + "," + id + "," + username + "\"").c_str());
}

void handleDisconnected(const char* data, size_t length) {
  Serial.println("Disconnected from ws");
  isConnected = false;
  setup();
}

void handleShockOff(const char* data, size_t length) {
  Serial.println("I'm too weak...");
}

void handleShockOn(const char* data, size_t length) {
  Serial.println("UNLIMITED POWER!!!");
}
