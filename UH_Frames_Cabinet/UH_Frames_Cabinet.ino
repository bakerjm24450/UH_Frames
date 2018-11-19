// For the cabinet with the hidden doors -- system includes ESP8266 and two relays to
// open the hidden doors. The ESP8266 runs an MQTT broker and processes MQTT messages
// from the picture frames. The frames send the RFID tags that they detect, and
// when we determine that the right frames are hanging over the right tags, we
// open the cabinet doors.
//
// Used libraries from https://github.com/martin-ger/uMQTTBroker
// Revision history:
//  1.0 (8/7/2018) -- initial version
//  2.0 (9/1/2018) --  revised to allow configuration -- which frame and which tag
//  2.1 (9/11/2018) -- replaced cabinet locks with fail-safe holding magnets

#include <ESP8266WiFi.h>
#include <uMQTTBroker.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>   // Include the WebServer library
#include <ArduinoOTA.h>
#include <EEPROM.h>

// Relay controls
const int OFF = 1;             // relays are active low
const int ON = 0;

const int LED = 0;            // pin for the onboard LED

// Pins controlling the cabinet doors
const int DOOR1 = 12;
const int DOOR2 = 13;

// Soft AP config
const char ssid[] = "";
const char passwd[] = "";
const int ap_channel = 2;

// OTA config
const char OTAName[] = "UH_Jefferson_Cabinet";         // A name and a password for the OTA service
const char OTAPassword[] = "";

// standard MQTT setup
const unsigned int mqttPort = 1883;
const unsigned int maxSubscriptions = 30;
const unsigned int maxRetainedTopics = 30;

// MDNS name
const char localName[] = "uh_jefferson_cabinet";      // name will be uh_jefferson_cabinet.local

// web server on port 80
ESP8266WebServer server(80);

// RFID tags we're looking for
uint32_t tags[2] = {0xced29ab2, 0xbd97730f};

const int numFrames = 3;          // number of frames, including spares
const int requiredMatches = 2;    // number of matching frames to open doors

// State of a frame
typedef enum {NO_TAG, TAG_PRESENT, NOT_RESPONDING} FrameStatus;

// data structure to represent a frame
typedef struct {
  FrameStatus status;         // is tag present, missing, or is frame not responding?
  uint32_t    detectedTag;    // tag this frame sees (0 if not tag present)
  uint32_t    desiredTag;     // tag this frame is looking for (0 if frame is not used)
  uint32_t    lastUpdateTime;  // last time we heard from this frame (in seconds)
} Frame;

// Array of frames in system
Frame frames[numFrames];

// status of the doors
bool doorsOpen = false;

// system status
bool apStatus = false;
bool mdnsStatus = false;
bool webServerStatus = false;
bool otaStatus = false;
bool mqttStatus = false;

//*************************************************************************
// Function prototypes
//*************************************************************************
bool setupAP();                           // wifi access point setup
bool setupMdns();                         // MDNS server
bool setupMqttBroker();                   // server for MQTT traffic
bool setupWebServer();
bool setupOTA();                          // Over The Air software updates
bool setupFrames();                       // initialize the frame data structures

void openDoor();                          // open the doors
void lockDoor();                          // lock the doors

// handle MQTT message
void frameCallback(uint32_t *client, const char* topic, uint32_t topicLen,
                   const char *data, uint32_t dataLen);

// HTTP handlers
void handleStatusRequest();
void handleDoorCmd();
void handleConfigRequest();
void handleConfig();
void handleRestartRequest();
void handleNotFound();


//********************************************************************************
// Initialize the system. Set up all the servers and initialize the relays
// controlling the doors.
//********************************************************************************
void setup()
{
  pinMode(LED, OUTPUT);

  // digital outs for the doors
  pinMode(DOOR1, OUTPUT);
  pinMode(DOOR2, OUTPUT);

  // open the doors initially
  openDoor();

  // set up serial port for diagnostics
  Serial.begin(115200);
  Serial.println();

  // set up the wifi network
  apStatus = setupAP();

  // set up MDNS server
  mdnsStatus = setupMdns();

  // set up MQTT broker
  mqttStatus = setupMqttBroker();

  // set up the web server
  webServerStatus = setupWebServer();

  // set up the OTA server for updates
  otaStatus = setupOTA();

  // initialize the frames
  setupFrames();
}


//************************************************************************
// loop -- Check to see if we should open the doors (if we have the right
// number of matching frames). Also, handle web requests and OTA requests.
// Note that MQTT requests are handled elsewhere by the callback functions.
//************************************************************************
void loop(void) {
  int numMatches = 0;           // number of matching frames we've found
  uint32_t currentTime = millis() / 1000;   // time in seconds

  // check status of each frome
  for (int i = 0; i < numFrames; i++) {
    // is this frame dead?
    if (currentTime - frames[i].lastUpdateTime > 120) {
      // haven't heard from him in 2 minutes, must be dead
      frames[i].status = NOT_RESPONDING;
      frames[i].detectedTag = 0;    // no false matches
    }

    // did he find the right tag?
    else if ( (frames[i].desiredTag != 0)
              && (frames[i].desiredTag == frames[i].detectedTag) ) {
      // he found it!
      frames[i].status = TAG_PRESENT;
      numMatches++;
    }

    // else, either no match or frame is a spare
    else {
      frames[i].status = NO_TAG;
    }
  }

  // should we open the doors?
  if (numMatches >= requiredMatches) {
    // open sesame!
    openDoor();
  }
 
  // handle web requests
  server.handleClient();

  // check for OTA updates
  ArduinoOTA.handle();

  // wait a little bit
  delay(10);
}


//*************************************************************************
// Set up the Wifi access point (AP)
// Returns true (success) or false (failure)
//************************************************************************
bool setupAP()
{
  Serial.print("Setting up AP ... ");

  WiFi.mode(WIFI_AP);

  // set up AP as a hidden network
  if (!WiFi.softAP(ssid, passwd, ap_channel, true)) {
    Serial.println("Failed to initialize soft AP");
    return false;
  }

  Serial.println("Done");

  return true;
}

//***************************************************************************
// Set up the MDNS server to handle local name requests.
// Returns true if successful
//***************************************************************************
bool setupMdns()
{
  // start up the server
  if (MDNS.begin(localName)) {
    Serial.println("mDNS responder started");

    // respond to http queries
    MDNS.addService("http", "tcp", 80);

    return true;
  }
  else {
    Serial.println("Error setting up MDNS responder!");
    return false;
  }
}


//****************************************************************************
// Initialize the MQTT broker to handle pub/sub traffic. Note we also subscribe
// to messages from the picture frames.
// Returns true if successful
//****************************************************************************
bool setupMqttBroker()
{
  // register the callback to handle received messages
  MQTT_server_onData(frameStatusCallback);

  // set up MQTT broker
  Serial.print("Starting MQTT broker ... ");
  if (!MQTT_server_start(mqttPort, maxSubscriptions, maxRetainedTopics)) {
    Serial.println("Failed to initialize MQTT broker");
    return false;
  }

  Serial.println("Done");

  // subscribe to the frames
  for (int i = 1; i <= numFrames; i++) {
    String topic("/Frame");
    topic += i;
    topic += "/tag";

    Serial.print("Subscribing to topic ");
    Serial.println(topic);

    MQTT_local_subscribe((uint8_t *) topic.c_str(), 0);
  }

  Serial.println("Waiting for messages...");

  return true;
}


//************************************************************************
// Initialize the web server to serve up status info.
// Returns true if successful
//************************************************************************
bool setupWebServer()
{
  // callbacks for status, opening/locking the door, and reset
  server.on("/", handleStatusRequest);
  server.on("/DoorCmd", handleDoorCmd);
  server.on("/ConfigRequest", handleConfigRequest);
  server.on("/Config", handleConfig);
  server.on("/Restart", handleRestartRequest);
  server.onNotFound(handleNotFound);

  server.begin();                           // Actually start the server
  Serial.println("HTTP server started");

  return true;
}

//*************************************************************************
// Set up the Over The Air service to receive software updates.
// Returns true if successful
//*************************************************************************
bool setupOTA()
{ // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  // Callbacks
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\r\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready\r\n");

  return true;
}


//************************************************************************
// Initialize the frame data structures.
//************************************************************************
bool setupFrames()
{
  for (int i = 0; i < numFrames; i++) {
    frames[i].status = NO_TAG;
    frames[i].desiredTag = 0;
    frames[i].detectedTag = 0;
    frames[i].lastUpdateTime = 0;
  }

  // allocate enough EEPROM space to hold left/right frame numbers
  EEPROM.begin(16);

  // get values stored in EEPROM
  int leftId;
  int rightId;

  EEPROM.get(0, leftId);
  EEPROM.get(4, rightId);

  Serial.print("Setup: leftId = "); Serial.println(leftId);
  Serial.print("Setup: rightId = "); Serial.println(rightId);

  // make sure they're sane
  if ((leftId >= 0) && (leftId < numFrames)
      && (rightId >= 0) && (rightId < numFrames)
      && (leftId != rightId)) {
    frames[leftId].desiredTag = tags[0];
    frames[rightId].desiredTag = tags[1];
  }

  return true;
}

//************************************************************************
// Open the door locks
//************************************************************************
void openDoor()
{
  // turn the magnets off to open the doors
  digitalWrite(DOOR1, OFF);
  digitalWrite(DOOR2, OFF);

  digitalWrite(LED, OFF);        // turn LED off too for diagnostics

  doorsOpen = true;
}

//************************************************************************
// Lock the doors
//************************************************************************
void lockDoor()
{
  // turn the magnets on to lock the doors
  digitalWrite(DOOR1, ON);
  digitalWrite(DOOR2, ON);

  digitalWrite(LED, ON);        // turn LED on too for diagnostics

  doorsOpen = false;
}

//************************************************************************
// Process received messages from the frames and set the frame's status. We
// expect the topics to be "/Frame1/tag", "/Frame2/tag", and so on.
// The data will be the tag that is detected, as an 8-digit hex value (0 if
// no tag is present).
// Note that we also record the time that the msg was received, so that we can
// figure out if a frame has stopped responding (i.e., the battery is dead).
// Input:
//    client = MQTT client
//    topic = MQTT topic that was received
//    topicLen = length of topic string
//    data = content of MQTT topic msg
//    dataLen = length of data string
//************************************************************************
void frameStatusCallback(uint32_t *client, const char* topic, uint32_t topicLen,
                         const char *data, uint32_t dataLen)
{
  // sanitize the input -- no overflows. Then, convert to a String
  char topicArray[32];
  if (topicLen > 31) topicLen = 31;
  memcpy(topicArray, topic, topicLen);
  topicArray[topicLen] = '\0';
  String topicStr(topicArray);

  char dataArray[32];
  if (dataLen > 31) dataLen = 31;
  memcpy(dataArray, data, dataLen);
  dataArray[dataLen] = '\0';
  String dataStr(dataArray);

  Serial.print("Received topic: "); Serial.print(topicStr);
  Serial.print(", data: "); Serial.println(dataStr);

  // process the message (sanitizing the input along the way)
  if (topicStr.startsWith("/Frame") && topicStr.endsWith("/tag")) {
    // looks OK, so figure out frame number
    int frameNumber = topicStr.substring(6, 7).toInt() - 1;

    // make sure frame number is OK
    if ((frameNumber >= 0) && (frameNumber < numFrames)) {
      // get detected tag ID -- note: Ardunio String.toInt() doesn't seem
      // to work correctly for big numbers. So, we'll do this by hand
      dataStr.trim();     // trim any whitespace
      uint32_t detectedTag = 0;
      for (int i = 0; i < dataStr.length() && isdigit(dataStr.charAt(i)); i++) {
        detectedTag = 10 * detectedTag + (dataStr.charAt(i) - '0');
      }

      frames[frameNumber].detectedTag = detectedTag;

      // update time for this frame
      frames[frameNumber].lastUpdateTime = millis() / 1000;
    }
  }
}



//*******************************************************************************
// Generate the web page for the system status.
//*******************************************************************************
void handleStatusRequest() {
  String htmlCode;
  unsigned long currentTime = millis();

  htmlCode += "<!DOCTYPE html>";
  htmlCode += "<html>";
  htmlCode += "<head>";
  htmlCode += "<meta http-equiv=\"refresh\" content=\"20\">";
  htmlCode += "<title>Cabinet Status</title>";
  htmlCode += "</head>";

  htmlCode += "<body>";

  // output system status
  htmlCode += "<h3>Cabinet Status</h3>";

  // are the doors open or closed?
  htmlCode += "<p><b>Doors:</b> ";
  htmlCode += (doorsOpen) ? "open" : "locked";
  htmlCode += "</p>";

  // output frame status
  for (int i = 0; i < numFrames; i++) {
    htmlCode += "<p><b>Frame ";
    htmlCode += String(i + 1);
    htmlCode += ":</b> ";
    if (frames[i].desiredTag == 0) htmlCode += "not in use";
    else {
      if (frames[i].desiredTag == tags[0]) htmlCode += "Left -- ";
      else if (frames[i].desiredTag == tags[1]) htmlCode += "Right -- ";

      if (frames[i].status == TAG_PRESENT) htmlCode += "in place";
      else if (frames[i].status == NO_TAG) htmlCode += "not in place";
      else if (frames[i].status == NOT_RESPONDING) htmlCode += "NOT RESPONDING";
      else htmlCode += "unknown status";
    }
    htmlCode += "</p>";
  }

  // add buttons to lock and open cabinet doors
  htmlCode += "<hr>";
  htmlCode += "<form>";
  htmlCode += "<button type=\"submit\" formmethod=\"post\" formaction=\"/DoorCmd\" name = \"doorCmd\" value=\"lock\">Lock Doors</button>";
  htmlCode += "<button type=\"submit\" formmethod=\"post\" formaction=\"/DoorCmd\" name = \"doorCmd\" value=\"open\">Open Doors</button>";
  htmlCode += "</form>";
  
  // add button to configure frames and tags
  htmlCode += "<hr>";
  htmlCode += "<form action=\"/ConfigRequest\" method=\"POST\"><input type=\"submit\" value=\"Configure Frames\"></form>";

  // add button to restart system
  htmlCode += "<hr>";
  htmlCode += "<form action=\"/Restart\" method=\"POST\"><input type=\"submit\" value=\"Restart System\"></form>";

  htmlCode += "</body>";
  htmlCode += "</html>";

  server.send(200, "text/html", htmlCode);
}

//***********************************************************************************
// Handle web request to open or lock the cabinet doors
//***********************************************************************************
void handleDoorCmd() {
  if (server.hasArg("doorCmd")) {
    String cmd = server.arg("doorCmd");

    if (cmd == "open") openDoor();
    else if (cmd == "lock") lockDoor();
  }

  // send them back to the status web page
  server.sendHeader("Location", "/");
  server.send(303);
}


//**********************************************************************************
// Handle web request to configure the frames and tags
//**********************************************************************************
void handleConfigRequest() {
  String htmlCode;

  htmlCode += "<!DOCTYPE html>\n";
  htmlCode += "<html>\n";
  htmlCode += "<head>\n";
  htmlCode += "<style>\n";
  htmlCode += "table {\n";
  htmlCode += "   font-family: arial, sans-serif;\n";
  htmlCode += "   border-collapse: collapse;\n";
  htmlCode += "   width: 100%;\n";
  htmlCode += "}\n";

  htmlCode += "td, th {\n";
  htmlCode += "   border: 1px solid #dddddd;\n";
  htmlCode += "   text-align: left;\n";
  htmlCode += "   padding: 8px;\n";
  htmlCode += "}\n";

  htmlCode += "tr:nth-child(even) {\n";
  htmlCode += "    background-color: #dddddd;\n";
  htmlCode += "}\n";
  htmlCode += "</style>\n";

  htmlCode += "<title>Reconfigure Frames</title>\n";

  htmlCode += "</head>\n";

  htmlCode += "<body>\n";

  htmlCode += "<h2>Select frame for each location:</h2>\n";

  htmlCode += "<form action=\"/Config\">\n";

  htmlCode += "<table>\n";
  htmlCode += "  <tr>\n";
  htmlCode += "     <th>Left</th>\n";
  htmlCode += "     <th>Right</th>\n";
  htmlCode += "  </tr>\n";

  htmlCode += "  <tr>\n";
  htmlCode += (frames[0].desiredTag == tags[0]) ?
              "     <td><input type=\"radio\" name=\"left\" value=\"1\" checked> Frame 1</td>\n"
              : "     <td><input type=\"radio\" name=\"left\" value=\"1\"> Frame 1</td>\n";
  htmlCode += (frames[0].desiredTag == tags[1]) ?
              "     <td><input type=\"radio\" name=\"right\" value=\"1\" checked> Frame 1</td>\n"
              : "     <td><input type=\"radio\" name=\"right\" value=\"1\"> Frame 1</td>\n";
  htmlCode += "  </tr>\n";

  htmlCode += "  <tr>\n";
  htmlCode += (frames[1].desiredTag == tags[0]) ?
              "     <td><input type=\"radio\" name=\"left\" value=\"2\" checked> Frame 2</td>\n"
              : "     <td><input type=\"radio\" name=\"left\" value=\"2\"> Frame 2</td>\n";
  htmlCode += (frames[1].desiredTag == tags[1]) ?
              "     <td><input type=\"radio\" name=\"right\" value=\"2\" checked> Frame 2</td>\n"
              : "     <td><input type=\"radio\" name=\"right\" value=\"2\"> Frame 2</td>\n";
  htmlCode += "  </tr>\n";

  htmlCode += "  <tr>\n";
  htmlCode += (frames[2].desiredTag == tags[0]) ?
              "     <td><input type=\"radio\" name=\"left\" value=\"3\" checked> Frame 3</td>\n"
              : "     <td><input type=\"radio\" name=\"left\" value=\"3\"> Frame 3</td>\n";
  htmlCode += (frames[2].desiredTag == tags[1]) ?
              "     <td><input type=\"radio\" name=\"right\" value=\"3\" checked> Frame 3</td>\n"
              : "     <td><input type=\"radio\" name=\"right\" value=\"3\"> Frame 3</td>\n";
  htmlCode += "  </tr>\n";

  htmlCode += "</table>\n";

  htmlCode += "<br>\n";
  htmlCode += "<input type=\"submit\">\n";
  htmlCode += "</form>\n";

  htmlCode += "</body>\n";
  htmlCode += "</html>\n";

  server.send(200, "text/html", htmlCode);
}


//**********************************************************************************
// Handles request to configure the frames -- specify which frame goes on the left
// and which goes on the right.
//**********************************************************************************
void handleConfig()
{
  int leftId = -1, rightId = -1;

  // get ID of left and right frames. The -1 is because received values are 1-numFrames,
  // but array index needs to be 0 - numFrames-1.
  if (server.hasArg("left")) {
    leftId = server.arg("left").toInt() - 1;
  }

  if (server.hasArg("right")) {
    rightId = server.arg("right").toInt() - 1;
  }

  // make sure they're valid
  if ( (leftId >= 0) && (leftId < numFrames)
       && (rightId >= 0) && (rightId < numFrames)
       && (leftId != rightId) ) {
    // clear the desired tags
    for (int i = 0; i < numFrames; i++) {
      frames[i].desiredTag = 0;
    }

    // set up the correct desired tag now
    frames[leftId].desiredTag = tags[0];
    frames[rightId].desiredTag = tags[1];

    // store config info in eeprom
    EEPROM.put(0, leftId);
    EEPROM.put(4, rightId);
    EEPROM.commit();

    // send them back to main status page
    server.sendHeader("Location", "/");
    server.send(303);

  }

  else {
    // form data is bad -- either missing entry or selected same frame twice
    // Send them back to the configure frame page
    server.sendHeader("Location", "/ConfigRequest");
    server.send(303);


  }
}

//**********************************************************************************
// Handle web request to restart the system
//**********************************************************************************
void handleRestartRequest() {
  // send them back to status page
  server.sendHeader("Location", "/");
  server.send(303);

  // wait for web redirect to finish
  delay(1000);

  // restart the system
  ESP.restart();
}

//************************************************************************************
// Handle any invalid web request.
//************************************************************************************
void handleNotFound() {
  server.send(404, "text/plain", "404: Not found");
}
