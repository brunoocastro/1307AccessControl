#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <SPIFFS.h>
#include <SPI.h>
#include <MFRC522.h>
#include <FS.h>

#define SDAPin 5
#define RSTPin 2
#define RelayPin 12

#define GreenLedPin 22
#define RedLedPin 13

File cardsFile;
int maxCards = 100;
String cards[100] = {};

MFRC522 CardReader(SDAPin, RSTPin);

const char *accessPointSSID = "Sala1307";
const char *accessPointPassword = "Senhasala1307";

IPAddress local_IP(192, 168, 13, 07);
IPAddress gateway(192, 168, 1, 5);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer webServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);

bool IS_DOOR_OPEN = true;
bool IS_REGISTER_MODE = false;

void setup()
{
  Serial.begin(115200);
  
  delay(500);

  initSPIFFS();
  initAccessPoint();
  initWebServer();

  delay(500);

  initWebSocket();
  initRFID();

  delay(500);

  setDoorOpen();
};

void loop()
{
  webSocket.loop();

  ManageDoorMode(IS_REGISTER_MODE);
  ManageLightMode(IS_DOOR_OPEN, IS_REGISTER_MODE);
};

void initRFID()
{
  pinMode(GreenLedPin, OUTPUT);
  pinMode(RedLedPin, OUTPUT);
  pinMode(RelayPin, OUTPUT);

  digitalWrite(GreenLedPin, LOW);
  digitalWrite(RedLedPin, LOW);
  digitalWrite(RelayPin, LOW);

  SPI.begin();

  CardReader.PCD_Init();

  UpdateLocalCardsFromMemory();

  for (int i = 0; i < 10; i++)
  {
    digitalWrite(RedLedPin, HIGH);
    digitalWrite(GreenLedPin, HIGH);
    delay(50);
    digitalWrite(RedLedPin, LOW);
    digitalWrite(GreenLedPin, LOW);
    delay(50);
  }
}

void initSPIFFS()
{
  if (!SPIFFS.begin())
  {
    Serial.println("Cannot mount SPIFFS volume... Restarting in 5 seconds!");
    delay(5000);
    return ESP.restart();
  }

  cardsFile = SPIFFS.open("/savedCards.txt", "r");
  delay(300);

  if (!cardsFile)
  {
    Serial.println("[FILE] Error opening file.");
    return ESP.restart();
  }

  Serial.println("[FILE] - Successfully opened cards file.");
};

void initAccessPoint()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  delay(500);
  WiFi.mode(WIFI_AP);

  Serial.print("Starting Access Point ... ");
  Serial.println(WiFi.softAP(accessPointSSID, accessPointPassword) ? "Ready" : "Failed!");
  delay(3000);

  Serial.print("Setting up Access Point ... ");
  Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Ready" : "Failed!");

  Serial.print("IP address = ");
  Serial.println(WiFi.softAPIP());
};

void initWebServer()
{
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send(SPIFFS, "/webpage.html", "text/html"); });

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
  Serial.println("Handling with event of type: " + Event + " with value: " + newStatus);

  if (Event == "isRegistrationMode")
  {
    if (newStatus == true)
    {
      setProgrammingState();
    }
    else
    {
      setReadingState();
    }
  }
  else if (Event == "isDoorOpen")
  {
    if (newStatus == true)
    {
      setDoorOpen();
    }
    else
    {
      setDoorClosed();
    }
  }
  else if (Event == "getData")
  {
    sendCurrentStatus();
  }
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

bool hasCardToRead()
{
  return (CardReader.PICC_IsNewCardPresent() == true && CardReader.PICC_ReadCardSerial() == true);
}

String ReadCardUID()
{
  String foundCard = "";
  if (hasCardToRead())
  {
    for (byte i = 0; i < CardReader.uid.size; i++)
    {
      foundCard += String(CardReader.uid.uidByte[i]);
    }
    Serial.println("[READ] Found card with UID: " + String(foundCard));
    delay(200);
  }
  return foundCard;
}

void setProgrammingState()
{
  Serial.println("[PROGRAMMING] Changed to Programming state!");
  IS_REGISTER_MODE = true;
  delay(350);
  sendCurrentStatus();
}

void setReadingState()
{
  Serial.println("[PROGRAMMING] Changed to Reading state!");
  IS_REGISTER_MODE = false;
  delay(350);
  sendCurrentStatus();
}

void setDoorOpen()
{
  Serial.println("[DOOR] Door is OPEN now!");
  digitalWrite(RelayPin, HIGH);
  IS_DOOR_OPEN = true;

  delay(350);
  sendCurrentStatus();
}

void setDoorClosed()
{
  Serial.println("[DOOR] Door is CLOSED now!");
  digitalWrite(RelayPin, LOW);
  IS_DOOR_OPEN = false;

  delay(350);
  sendCurrentStatus();
}

void setOpenLight()
{
  digitalWrite(GreenLedPin, HIGH);
  digitalWrite(RedLedPin, LOW);
}

void setClosedLight()
{
  digitalWrite(RedLedPin, HIGH);
  digitalWrite(GreenLedPin, LOW);
}

void setProgrammingLight()
{
  digitalWrite(RedLedPin, HIGH);
  digitalWrite(GreenLedPin, LOW);
  delay(500);
  digitalWrite(GreenLedPin, HIGH);
  digitalWrite(RedLedPin, LOW);
  delay(500);
}

void setUnauthorizedLight()
{
  digitalWrite(GreenLedPin, LOW);
  digitalWrite(RedLedPin, LOW);
  for (int i = 0; i < 6; i++)
  {
    digitalWrite(RedLedPin, HIGH);
    delay(150);
    digitalWrite(RedLedPin, LOW);
    delay(150);
  }
  delay(500);
}

void setAuthorizedLight()
{
  digitalWrite(GreenLedPin, LOW);
  digitalWrite(RedLedPin, LOW);
  for (int i = 0; i < 6; i++)
  {
    digitalWrite(GreenLedPin, HIGH);
    delay(150);
    digitalWrite(GreenLedPin, LOW);
    delay(150);
  }
  delay(500);
}

void ManageLightMode(bool isDoorOpen, bool isProgrammingMode)
{
  if (isProgrammingMode)
  {
    return setProgrammingLight();
  }

  if (!isDoorOpen)
  {
    return setClosedLight();
  }

  setOpenLight();
}

void ManageDoorMode(bool isProgrammingMode)
{
  if (isProgrammingMode)
    return handleProgrammingMode();

  return handleReadingMode();
}

void handleReadingMode()
{
  String currentReadCard = ReadCardUID();

  if (currentReadCard != "")
  {
    Serial.println("[ACCESS] Trying to access with card UID: " + String(currentReadCard));

    boolean canAccess = hasCardInLocalMemory(currentReadCard);

    if (canAccess)
    {
      Serial.println("[AUTHORIZED] Access GRANTED with UID: " + String(currentReadCard));
      toggleDoorStatus();
      return setAuthorizedLight();
    }

    Serial.println("[UNAUTHORIZED] Access DENIED with UID: " + String(currentReadCard));
    return setUnauthorizedLight();
  }
}

void toggleDoorStatus()
{
  if (IS_DOOR_OPEN)
  {
    return setDoorClosed();
  }

  return setDoorOpen();
}

void handleProgrammingMode()
{
  String ReadCard = "";

  Serial.println("[REGISTER] Waiting read a card to add/remove");

  for (int times = 0; times < 100; times++)
  {
    digitalWrite(GreenLedPin, LOW);
    digitalWrite(RedLedPin, HIGH);
    delay(10);
    digitalWrite(GreenLedPin, HIGH);
    digitalWrite(RedLedPin, LOW);

    ReadCard = ReadCardUID();
    if (ReadCard == "")
      continue;

    break;
  }

  if (ReadCard == "")
  {
    Serial.println("[REGISTER] No card read!");
  }
  else if (hasCardInLocalMemory(ReadCard))
  {
    deleteCardFromMemory(ReadCard);
  }
  else if (hasNoMemorySpace())
  {
    Serial.println("[REGISTER] FULL Memory. Card not added!");
  }
  else
  {
    addCardToMemory(ReadCard);
  }

  digitalWrite(GreenLedPin, LOW);
  digitalWrite(RedLedPin, LOW);

  Serial.println("[REGISTER] Finish. Returning to Reading state!");
  return setReadingState();
}

bool hasCardInLocalMemory(String cardID)
{
  boolean hasCard = false;
  for (int c = 0; c < maxCards; c++)
  {
    if (cardID == cards[c])
    {
      hasCard = true;
      break;
    }
  }

  return hasCard == true;
}

void deleteCardFromMemory(String cardID)
{
  for (int memPOS = 0; memPOS < maxCards; memPOS++)
  {
    if (cards[memPOS] == cardID)
    {
      cards[memPOS] = "";
      setUnauthorizedLight();
    }
  }
}

void addCardToMemory(String cardID)
{
  Serial.println("[REGISTER] Adding new access to local memory!");
  for (int cleanMemPosition = 0; cleanMemPosition < maxCards; cleanMemPosition++)
  {
    if (cards[cleanMemPosition] == "")
    {
      Serial.println("[MEMORY] Saving new card of UID " + String(cardID) + " in memory position " + cleanMemPosition);
      cards[cleanMemPosition] = cardID;

      break;
    }
  }

  upCardsToFile();

  delay(500);

  UpdateLocalCardsFromMemory();

  return setAuthorizedLight();
}

void upCardsToFile()
{
  cardsFile.close();
  delay(300);
  File saveFile = SPIFFS.open("/savedCards.txt", "w");
  delay(300);

  for (int cardPos = 0; cardPos <= maxCards; cardPos++)
  {
    if (cards[cardPos] != "")
    {
      Serial.println("Card to save: " + cards[cardPos]);
      cardsFile.println(String(cards[cardPos]));
    }
  }
  saveFile.close();
  delay(300);
  cardsFile = SPIFFS.open("/savedCards.txt", "r");
  delay(300);
}

bool hasNoMemorySpace()
{
  if (cards[maxCards - 1] != "")
  {
    digitalWrite(GreenLedPin, LOW);
    for (int i = 0; i < 30; i++)
    {
      digitalWrite(RedLedPin, HIGH);
      delay(100);
      digitalWrite(RedLedPin, LOW);
      delay(100);
    }
    return true;
  }
  return false;
}

void UpdateLocalCardsFromMemory()
{
  int pos = 0;
  Serial.println("[FILE] Trying to read file if is available " + String(cardsFile.available()));

  while (cardsFile.available() && pos <= maxCards)
  {
    String cardUIDFromFile = "";

    cardUIDFromFile = cardsFile.readStringUntil('\n');
    cardUIDFromFile = cardUIDFromFile.substring(0, cardUIDFromFile.length() - 1);

    if (cardUIDFromFile != "")
    {
      Serial.println("[FILE] Card found in register file: " + cardUIDFromFile + ". Saving in local memory at position: " + String(pos));
    }

    cards[pos] = String(cardUIDFromFile);
    pos += 1;
    delay(15);
  }
}