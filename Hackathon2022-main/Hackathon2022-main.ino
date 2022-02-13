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

#include <string.h>

//Pin allows to force board to take input
#define WiFiOverride 14

#define zapPin D0

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

//The threshold for this 
int scanThreshold = 0;

int scanIndex = 0;
int lastScans[] = {0, 0, 0, 0, 0};

unsigned long lastLeft;
unsigned long lastEmission;
bool shouldZap = false;

//Room data
#define roomSize 16
int ids[roomSize];
char names[roomSize][64];
bool sending[roomSize];

void setup() {
  // Begin serial
  Serial.begin(115200);
  Serial.println("Starting");

  pinMode(zapPin, OUTPUT);
  digitalWrite(zapPin, LOW);

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
  //EEPROM.write(193, 255); EEPROM.commit();
  id = EEPROM.read(193);
  //EEPROM.write(194, 63); EEPROM.commit();
  scanThreshold = -1 * EEPROM.read(194);

  //Debug printing
  Serial.println(ssid);
  Serial.println(password);
  Serial.println(room);
  Serial.println(username);
  Serial.println(id);
  Serial.println(scanThreshold);

  LittleFS.begin();

  //Pin all
  pinMode(WiFiOverride, INPUT_PULLUP);

  //Allow for overriding with the pin, forcing board to ask for SSID/Password
  if (digitalRead(WiFiOverride) != LOW) {
    //Go into client mode and try to connect
    WiFi.mode(WIFI_AP_STA);
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
    Serial.print("Connected! IP is: ");
    Serial.println(WiFi.localIP());

    /*IPAddress ip(192, 168, 0, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    Serial.println("Electric Boogie " + String(id));
    WiFi.softAP("Electric Boogie " + String(id));*/

    server.on("/", hostRoot);
    server.on("/getName", hostName);
    server.on("/connections", hostConnections);
    server.on("/threshold", setThreshold);
    server.begin();

    webSocket.on("connect", handleConnected);
    webSocket.on("disconnect", handleDisconnected);
    webSocket.on("room", handleRoom);
    webSocket.begin("ec2-13-58-213-225.us-east-2.compute.amazonaws.com", 4098);

    if (scanThreshold > -30) {
      WiFi.scanNetworksAsync(initialScan, true);
    }
    lastLeft = millis();
    lastEmission = millis();
    shouldZap = false;
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
  //Do WiFi stuff and potentially shock
  if (WiFi.scanComplete() == -2) {
    WiFi.scanNetworksAsync(onScan, true);
  }
  if (millis() - lastLeft > 30000 && !shouldZap) {
    webSocket.emit("zap", (String("\"") + room + "," + id + "\"").c_str());
    shouldZap = true;
  }
  if (millis() - lastLeft > 40000 && digitalRead(zapPin) == LOW) {
    webSocket.emit("zap", (String("\"") + room + "," + id + "\"").c_str());
  }
  if (millis() - lastLeft < 30000) {
    if (digitalRead(zapPin) == HIGH && millis() - lastEmission > 10000) {
      webSocket.emit("unzap", (String("\"") + room + "," + id + "\"").c_str());
      lastEmission = millis();
    }
    shouldZap = false;
  }
}

void setThreshold() {
  String numberString = server.arg("threshold");
  scanThreshold = numberString.toInt();
  EEPROM.write(194, -1 * scanThreshold);
  EEPROM.commit();
  server.send(200, "text/plain", "Success!");
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
  File file = LittleFS.open("status.html", "r");
  server.streamFile(file, "text/html");
  file.close();
}

void hostName() {
  //Serial.println(room);
  server.send(200, "text/plain", room);
}

void hostConnections() {
  String toSend = "";
  for (int i = 0; i < roomSize; i++) {
    toSend.concat(ids[i]);
    toSend.concat("`");
    toSend.concat(names[i]);
    toSend.concat("`");
    toSend.concat(sending[i] ? 1 : 0);
    toSend.concat("\n");
  }
  //Serial.println(toSend);
  server.send(200, "text/plain", toSend.c_str());
}

void handleConnected(const char* data, size_t lengthh) {
  Serial.println("Connected to ws");
  webSocket.emit("register", (String("\"") + room + "," + id + "," + username + "\"").c_str());
}

void handleDisconnected(const char* data, size_t lengthh) {
  Serial.println("Disconnected from ws");
  isConnected = false;
  setup();
}

void handleRoom(const char* data, size_t lengthh) {
  Serial.println("Handling room");
  Serial.println(data);
  char newData[lengthh + 4];
  char* stringBuffer[roomSize];

  strcpy(newData, data);
  
  stringBuffer[0] = strtok(newData, "|");
  for (int i = 1; i < roomSize; i++) {
    stringBuffer[i] = strtok(NULL, "|");
  }
  
  for (int i = 0; i < roomSize; i++) {
    if (stringBuffer[i] == NULL) {
      continue;
    }
    ids[i] = atoi(strtok(stringBuffer[i], "`"));
    strcpy(names[i], strtok(NULL, "`"));
    sending[i] = atoi(strtok(NULL, "`")) == 1;
  }

  bool found = false;
  for (int i = 0; i < roomSize; i++) {
    if (sending[i]) {
      Serial.println("ZAP ZAP ZAP ZAP");
      digitalWrite(zapPin, HIGH);
      found = true;
    }
  }
  
  if (!found) {
    Serial.println("No zap :(");
    digitalWrite(zapPin, LOW);
  }

  Serial.println("Got room data:");
  for (int i = 0; i < roomSize; i++) {
    if (names[i] == NULL || strcmp(names[i], "") == 0) continue;
    Serial.print(ids[i]);
    Serial.print(" ");
    Serial.print(names[i]);
    Serial.print(" ");
    Serial.println(sending[i]);
  }
}

void initialScan(int n) {
  Serial.println(n);
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == "Electric Boogie Beacon " + String(id)) {
      Serial.println("Found beacon! Strength is as follows:");
      scanThreshold = WiFi.RSSI(i) - 15;
      if (scanThreshold > -50) {
        scanThreshold = -50;
      }
      Serial.println(scanThreshold);
      EEPROM.write(194, -1 * scanThreshold);
      EEPROM.commit();
    }
  }
  Serial.println("Scan finished for the first time");
  WiFi.scanDelete();
}

void onScan(int n) {
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == "Electric Boogie Beacon " + String(id)) {
      Serial.print("Found beacon! Strength is: ");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" compared to the threshold of ");
      Serial.println(scanThreshold);
      lastScans[scanIndex] = WiFi.RSSI(i);
      scanIndex++;
      scanIndex = scanIndex % 5;
      found = true;
    }
  }
  if (!found) {
    Serial.println("Adding threshold due to not finding");
    lastScans[scanIndex] = scanThreshold - 1;
    scanIndex++;
    scanIndex = scanIndex % 5;
  }
  
  int sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += lastScans[i];
  }

  Serial.print("Sum is now: ");
  Serial.print(sum);
  Serial.print(" and threshold is ");
  Serial.println(scanThreshold * 5);
  
  if (sum <= scanThreshold * 5) {
    Serial.println("Resetting due to leaving");
    lastLeft = millis();
  }
  WiFi.scanDelete();
}
