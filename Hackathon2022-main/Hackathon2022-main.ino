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
//We do some C-string manipulation here
#include <string.h>

//Pin allows to force board to take input
#define WiFiOverride 14
//Pin which controls the TENS unit
#define zapPin D0

//Initializing the webserver and the SocketIO client, respectively
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

//The threshold for distance. Later gets set to -50 or something
int scanThreshold = 0;

//Simple circular queue implementation
int scanIndex = 0;
int lastScans[] = {0, 0, 0, 0, 0};

//Keeps track of when we last left the beacon, as well as when we last emitted an unzap request to the websocket
unsigned long lastLeft;
unsigned long lastEmission;
//Keeps track of whether we need to broadcast a zap request to the websocket
bool shouldZap = false;

//Room data, including user IDs, names, and sending status (whether they're being zapped)
#define roomSize 16
int ids[roomSize];
char names[roomSize][64];
bool sending[roomSize];

//Called at the start
void setup() {
  // Begin serial
  Serial.begin(115200);
  Serial.println("Starting");

  //Initialize the TENS pin
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

  //Initialize LittleFS
  LittleFS.begin();

  //Pin for manually forcing the program to enter setup mode
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
    //Logging for debug purposes
    Serial.print("Connected! IP is: ");
    Serial.println(WiFi.localIP());

    //Event handlers for server, plus connection
    server.on("/", hostRoot);
    server.on("/getName", hostName);
    server.on("/connections", hostConnections);
    server.on("/threshold", setThreshold);
    server.begin();

    //Event handlers for socket, plus connection
    webSocket.on("connect", handleConnected);
    webSocket.on("disconnect", handleDisconnected);
    webSocket.on("room", handleRoom);
    webSocket.begin("ec2-13-58-213-225.us-east-2.compute.amazonaws.com", 4098);

    //Do an automatic scan if the threshold seems too high
    if (scanThreshold > -30) {
      WiFi.scanNetworksAsync(initialScan, true);
    }

    //Start keeping time
    lastLeft = millis();
    lastEmission = millis();

    //We should NOT send a zap request right now
    shouldZap = false;
  //If disconnected, give this thing its own web server and register handlers
  } else {
    Serial.println("\nNot connected...");
    //WiFi config
    WiFi.mode(WIFI_AP);
    IPAddress ip(192, 168, 0, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    Serial.println("Electric Boogie " + String(id));
    WiFi.softAP("Electric Boogie " + String(id));
    //Server event handlers
    server.on("/", clientRoot);
    server.on("/submit", submit);
    server.begin();
    Serial.println("Server has been made");
  }

  Serial.println("Setup done");
}

//Runs repeatedly
void loop() {
  //Route to proper loop based on connections
  if (isConnected) {
    clientLoop();
  } else {
    serverLoop();
  }
}

void serverLoop() {
  //WiFi.printDiag(Serial);
  //Make the web server work
  server.handleClient();
}

void clientLoop() {
  //Make client and server work
  webSocket.loop();
  server.handleClient();
  //When available, refresh the distance to the beacon
  if (WiFi.scanComplete() == -2) {
    WiFi.scanNetworksAsync(onScan, true);
  }
  //If we just hit time for the first time, yell at the server
  if (millis() - lastLeft > 30000 && !shouldZap) {
    webSocket.emit("zap", (String("\"") + room + "," + id + "\"").c_str());
    shouldZap = true;
  }
  //If we hit time a bit ago and still haven't zapped the user, yell at the server again
  if (millis() - lastLeft > 40000 && digitalRead(zapPin) == LOW) {
    webSocket.emit("zap", (String("\"") + room + "," + id + "\"").c_str());
  }
  //If we haven't hit time
  if (millis() - lastLeft < 30000) {
    //Every ten seconds while we zap, cry out in pain for the server to stop (in case it missed our unzap request)
    if (digitalRead(zapPin) == HIGH && millis() - lastEmission > 10000) {
      webSocket.emit("unzap", (String("\"") + room + "," + id + "\"").c_str());
      lastEmission = millis();
    }
    //We should no longer try to zap the user
    shouldZap = false;
  }
}

//API call to set the decibel threshold for what's considered moving
void setThreshold() {
  //Get user input  
  String numberString = server.arg("threshold");
  //Make it an int and save it
  scanThreshold = numberString.toInt();
  //Write it to memory and commit
  EEPROM.write(194, -1 * scanThreshold);
  EEPROM.commit();
  //Tell the client that it worked
  server.send(200, "text/plain", "Success!");
}

//Function which allows the user to submit the router stuff
void submit() {
  //All the args
  String ssidString = server.arg("ssid");
  String passString = server.arg("password");
  String roomString = server.arg("room");
  String usernameString = server.arg("username");

  //Debug logging
  Serial.println(ssidString);
  Serial.println(passString);
  Serial.println(roomString);
  Serial.println(usernameString);

  //Write SSID to memory
  if (ssidString != "") {
    for (int i = 0; i < 64; i++) {
      EEPROM.write(i, ssidString[i]);
    }
  }
  //Write password to memory
  if (passString != "") {
    for (int i = 0; i < 64; i++) {
      EEPROM.write(i + 64, passString[i]);
    }
  }
  //Write room code to memory
  if (roomString != "") {
    for (int i = 0; i < 32; i++) {
      EEPROM.write(i + 128, roomString[i]);
    }
  }
  //Write username to memory
  if (usernameString != "") {
    for (int i = 0; i < 32; i++) {
      EEPROM.write(i + 160, usernameString[i]);
    }
  }

  //Finish up
  EEPROM.commit();
  //Tell the client we're good
  server.send(200, "text/plain", "Success!");
  //Restart
  ESP.restart();
}

//Send the setup page on a web server call
void clientRoot() {
  File file = LittleFS.open("setup.html", "r");
  server.streamFile(file, "text/html");
  file.close();
}

//Send the status page on a web server call
void hostRoot() {
  File file = LittleFS.open("status.html", "r");
  server.streamFile(file, "text/html");
  file.close();
}

//API call to get the current room code
void hostName() {
  //Serial.println(room);
  server.send(200, "text/plain", room);
}

//API call to get the room information
void hostConnections() {
  //Construct a string which contains all of id, name, and sending for everyone in the room
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
  //Send that to the client
  server.send(200, "text/plain", toSend.c_str());
}

//On connection, log and then register with the server
void handleConnected(const char* data, size_t lengthh) {
  Serial.println("Connected to ws");
  webSocket.emit("register", (String("\"") + room + "," + id + "," + username + "\"").c_str());
}

//On disconnection, log and then try to reconnect
void handleDisconnected(const char* data, size_t lengthh) {
  Serial.println("Disconnected from ws");
  isConnected = false;
  setup();
}

//Handle when the server broadcasts the room status
void handleRoom(const char* data, size_t lengthh) {
  //Log the data
  Serial.println("Handling room");
  Serial.println(data);
  //Buffers to hold the data
  char newData[lengthh + 4];
  char* stringBuffer[roomSize];

  //Copy data to newData (essentially removing the const)
  strcpy(newData, data);

  //Split newData into an array based on the | delimiter
  stringBuffer[0] = strtok(newData, "|");
  for (int i = 1; i < roomSize; i++) {
    stringBuffer[i] = strtok(NULL, "|");
  }

  //For each row in stringBuffer
  for (int i = 0; i < roomSize; i++) {
    //Skip empty rows
    if (stringBuffer[i] == NULL) {
      continue;
    }
    //Parse values from stringBuffer and save
    ids[i] = atoi(strtok(stringBuffer[i], "`"));
    strcpy(names[i], strtok(NULL, "`"));
    sending[i] = atoi(strtok(NULL, "`")) == 1;
  }

  //Check if anyone is saying to zap
  bool found = false;
  for (int i = 0; i < roomSize; i++) {
    if (sending[i]) {
      //If we found them, zap the user
      Serial.println("ZAP ZAP ZAP ZAP");
      digitalWrite(zapPin, HIGH);
      found = true;
    }
  }

  //If nobody is saying to zap, don't zap the user
  if (!found) {
    Serial.println("No zap :(");
    digitalWrite(zapPin, LOW);
  }

  //Debug printing
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

//Sets the WiFi threshold automatically based on a scan
void initialScan(int n) {
  Serial.println(n);
  //For each WiFI signal we see
  for (int i = 0; i < n; i++) {
    //If this is the paired beacon
    if (WiFi.SSID(i) == "Electric Boogie Beacon " + String(id)) {
      Serial.println("Found beacon! Strength is as follows:");
      //Set the threshold, with a maximum value
      scanThreshold = WiFi.RSSI(i) - 15;
      if (scanThreshold > -50) {
        scanThreshold = -50;
      }
      //Save to memory
      Serial.println(scanThreshold);
      EEPROM.write(194, -1 * scanThreshold);
      EEPROM.commit();
    }
  }
  Serial.println("Scan finished for the first time");
  //Clear scan
  WiFi.scanDelete();
}

//When a scan finishes
void onScan(int n) {
  //Keep track of if the source is out of range
  bool found = false;
  //For each network
  for (int i = 0; i < n; i++) {
    //If this is it
    if (WiFi.SSID(i) == "Electric Boogie Beacon " + String(id)) {
      Serial.print("Found beacon! Strength is: ");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" compared to the threshold of ");
      Serial.println(scanThreshold);
      //Save to our circular queue
      lastScans[scanIndex] = WiFi.RSSI(i);
      //Update queue index
      scanIndex++;
      scanIndex = scanIndex % 5;
      found = true;
    }
  }
  //If not found, pretend we found it just below the threshold
  if (!found) {
    Serial.println("Adding threshold due to not finding");
    lastScans[scanIndex] = scanThreshold - 1;
    scanIndex++;
    scanIndex = scanIndex % 5;
  }

  //Sum the signal strengths
  int sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += lastScans[i];
  }

  Serial.print("Sum is now: ");
  Serial.print(sum);
  Serial.print(" and threshold is ");
  Serial.println(scanThreshold * 5);

  //If we're out of range, reset the lastLeft timer
  if (sum <= scanThreshold * 5) {
    Serial.println("Resetting due to leaving");
    lastLeft = millis();
  }
  //Clear the scan
  WiFi.scanDelete();
}
