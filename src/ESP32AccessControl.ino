#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

const char *accessPointSSID = "Sala1307";
const char *accessPointPassword = "Senhasala1307";

IPAddress local_IP(192, 168, 13, 07);
IPAddress gateway(192, 168, 1, 5);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer webServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);

bool IS_DOOR_OPEN = false;
bool IS_REGISTER_MODE = false;

String COMMAND;

void setup()
{
  Serial.begin(115200);
  delay(500);

  initSPIFFS();
  initAccessPoint();
  initWebServer();
  delay(500);
  initWebSocket();
};

void loop()
{
  webSocket.loop();
  if (Serial.available())
  {
    Serial.println("Available");
    COMMAND = Serial.readStringUntil('\n');

    if (COMMAND.equals("door"))
    {
      IS_DOOR_OPEN = !IS_DOOR_OPEN;
      Serial.print("New door open status");
      Serial.println(IS_DOOR_OPEN);
    }
    else if (COMMAND.equals("register"))
    {
      IS_REGISTER_MODE = !IS_REGISTER_MODE;
      Serial.print("New door open status");
      Serial.println(IS_REGISTER_MODE);
    }
    else if (COMMAND.equals("send"))
    {
      Serial.println("Sending info:");
      sendCurrentStatus();
    }
    else
    {
      Serial.println("Invalid command");
    }
  }
};

void initSPIFFS()
{
  if (!SPIFFS.begin())
  {
    Serial.println("Cannot mount SPIFFS volume... Restarting in 5 seconds!");
    delay(5000);
    ESP.restart();
  }
};

void initAccessPoint()
{
  Serial.print("Setting up Access Point ... ");
  Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Ready" : "Failed!");

  Serial.print("Starting Access Point ... ");
  Serial.println(WiFi.softAP(accessPointSSID, accessPointPassword) ? "Ready" : "Failed!");

  Serial.print("IP address = ");
  Serial.println(WiFi.softAPIP());
};

void initWebServer()
{
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { // define here wat the webserver needs to do
    request->send(SPIFFS, "/webpage.html", "text/html");
  });

  webServer.onNotFound([](AsyncWebServerRequest *request)
                       { request->send(404, "text/plain", "Not found. Just '/' endpoint exists!"); });

  webServer.serveStatic("/", SPIFFS, "/");
};

void initWebSocket()
{
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  webServer.begin();
};

void sendCurrentStatus()
{
  String currentStatusParsed = String("{\"type\":\"getData\",\"isDoorOpen\":") + IS_DOOR_OPEN + ",\"isRegistrationMode\":" + IS_REGISTER_MODE + "}";

  Serial.println("Send Current Status to WEB -> " + currentStatusParsed);

  webSocket.broadcastTXT(currentStatusParsed);
};

void handleValidEvents(String Event, bool newStatus)
{
  Serial.println("Handling with event of type: " + Event + " | value: " + newStatus);
  Serial.println(Event == "isDoorOpen");

  if (Event == "isRegistrationMode")
  {
    Serial.print("Set Registration Mode");
    Serial.println(newStatus);

    IS_REGISTER_MODE = newStatus;
  }
  else if (Event == "isDoorOpen")
  {
    Serial.println("Set Door Status to :" + String(newStatus) + " | " + (newStatus == 1) + " | " + String(newStatus == true));
    Serial.println(newStatus == 1, newStatus);

    IS_DOOR_OPEN = newStatus;
  }

  sendCurrentStatus();
};

void onWebSocketEvent(byte clientId, WStype_t eventType, uint8_t *payload, size_t length)
{
  switch (eventType)
  {
  case WStype_DISCONNECTED:
    Serial.println("Client with ID" + String(clientId) + " disconnected now!");
    break;
  case WStype_CONNECTED:
    Serial.println("Client with ID " + String(clientId) + " connected now!");
    break;
  case WStype_TEXT:
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      Serial.print(F("Error on deserialize Json content: "));
      Serial.println(error.f_str());
      return;
    }

    const char *event_type = doc["type"];
    bool event_value = doc["value"];

    handleValidEvents(String(event_type), event_value);
  };
};
