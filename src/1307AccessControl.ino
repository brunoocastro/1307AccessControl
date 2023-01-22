#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <EEPROM.h>
#include <MFRC522.h>
#include <SPI.h>
#include <string.h>

#define SS_PIN 21  // Pin for MFRC522 SS
#define RST_PIN 22 // Pin for MFRC522 reset
#define GREEN_PIN 22
#define RED_PIN 13

MFRC522 mfrc522(SS_PIN, RST_PIN);
String AuthorizedID = "";
// String CardID = ""; // Create MFRC522 instance
byte CardUID[4];

bool detected = false;

const char *accessPointSSID = "Sala1307";
const char *accessPointPassword = "Senhasala1307";

IPAddress local_IP(192, 168, 13, 07);
IPAddress gateway(192, 168, 1, 5);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer webServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);

bool IS_DOOR_OPEN = false;
bool IS_PROGRAMMING_MODE = false;

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
  initAccessControl();
};

void loop()
{
  webSocket.loop();
  ManageDoorMode(IS_PROGRAMMING_MODE);
  ManageLightMode(IS_DOOR_OPEN, IS_PROGRAMMING_MODE);
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
      IS_PROGRAMMING_MODE = !IS_PROGRAMMING_MODE;
      Serial.print("New door open status");
      Serial.println(IS_PROGRAMMING_MODE);
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

void initAccessControl()
{
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  SPI.begin();
  mfrc522.PCD_Init(); // Initialize MFRC522
  Serial.println("Access Control ready!");
}

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
  String currentStatusParsed = String("{\"type\":\"getData\",\"isDoorOpen\":") + IS_DOOR_OPEN + ",\"isRegistrationMode\":" + IS_PROGRAMMING_MODE + "}";

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

    IS_PROGRAMMING_MODE = newStatus;
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

void setOpenLight()
{
  digitalWrite(GREEN_PIN, HIGH);
  digitalWrite(RED_PIN, LOW);
}

void setClosedLight()
{
  digitalWrite(RED_PIN, HIGH);
  digitalWrite(GREEN_PIN, LOW);
}

void setProgrammingLight()
{
  digitalWrite(RED_PIN, HIGH);
  digitalWrite(GREEN_PIN, LOW);
  delay(500);
  digitalWrite(GREEN_PIN, HIGH);
  digitalWrite(RED_PIN, LOW);
  delay(500);
}

void blinkCloseLight()
{
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(RED_PIN, HIGH);
  delay(500);
  digitalWrite(RED_PIN, LOW);
  delay(500);
  digitalWrite(RED_PIN, HIGH);
  delay(500);
  digitalWrite(RED_PIN, LOW);
  delay(500);
  digitalWrite(RED_PIN, HIGH);
}

void blinkOpenLight()
{
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, HIGH);
  delay(500);
  digitalWrite(GREEN_PIN, LOW);
  delay(500);
  digitalWrite(GREEN_PIN, HIGH);
  delay(500);
  digitalWrite(GREEN_PIN, LOW);
  delay(500);
  digitalWrite(GREEN_PIN, HIGH);
}

boolean checkCardInEEPROM()
{
  EEPROM.begin(32); // Start EEPROM access
  for (int i = 0; i < 4; i++)
  {
    if (EEPROM.read(i) != CardUID[i])
    {
      Serial.println("Not in EEPROM");
      EEPROM.end(); // End EEPROM access
      return false; // Return failure
    }
  }
  EEPROM.end(); // End EEPROM access
  Serial.println("UID in EEPROM");
  return true; // Return success
}

String readCard()
{
  String CardID = "";
  // Check if a card is present
  if (mfrc522.PICC_IsNewCardPresent())
  {
    // Select the card
    if (mfrc522.PICC_ReadCardSerial())
    {
      // Print the UID of the card
      Serial.print("Card UID: ");
      for (byte i = 0; i < mfrc522.uid.size; i++)
      {
        CardUID[i] = mfrc522.uid.uidByte[i];
        CardID += String(CardUID[i], HEX);
      }
      Serial.println(CardID);
      mfrc522.PICC_HaltA(); // halt PICC
      mfrc522.PCD_StopCrypto1();
      return CardID;
      // Return success
    }
  }
  return ""; // Return failure
}

void saveCardToEEPROM(String cardUID)
{
  // Save the UID to EEPROM
  // Get the current number of stored cards
  int numCards = EEPROM.read(0);

  // Calculate the next available address for the new card
  int nextAddress = numCards * 4 + 1;

  // Convert the string to an array of bytes
  uint8_t cardBytes[4];
  for (int i = 0; i < 4; i++)
  {
    cardBytes[i] = cardUID.charAt(i);
  }

  // Write the card UID to EEPROM
  for (int i = 0; i < 4; i++)
  {
    EEPROM.write(nextAddress + i, cardBytes[i]);
  }

  // Increment the number of stored cards
  numCards++;
  EEPROM.write(0, numCards);

  // Commit the changes to EEPROM
  EEPROM.commit();
  Serial.println("Cartao salvo no endereço: " + nextAddress);
}

void deleteCardFromEEPROM(String cardUID)
{
  // Check if a card is present
  // Convert the string to an array of bytes
  uint8_t cardBytes[4];
  for (int i = 0; i < cardUID.length(); i++)
  {
    cardBytes[i] = cardUID.charAt(i);
  }

  // Get the number of stored cards
  int numCards = EEPROM.read(0);

  // Search for the card in EEPROM
  for (int i = 0; i < numCards; i++)
  {
    int address = i * 4 + 1;
    bool match = true;
    for (int j = 0; j < cardUID.length(); j++)
    {
      if (EEPROM.read(address + j) != cardBytes[j])
      {
        match = false;
        break;
      }
    }
    if (match)
    {
      // Shift all the cards after the matched card to the left
      for (int j = i + 1; j < numCards; j++)
      {
        int nextAddress = j * 4 + 1;
        for (int k = 0; k < 4; k++)
        {
          EEPROM.write(address + k, EEPROM.read(nextAddress + k));
        }
        address = nextAddress;
      }
      // Decrement the number of stored cards
      numCards--;
      EEPROM.write(0, numCards);
      // Commit the changes to EEPROM
      EEPROM.commit();
      break;
    }
  }
}

void handleProgrammingMode()
{
  // Serial.println("PROGRAMMING MODE");
  String Card = "";
  while (Card == "")
  {
    Card = readCard();
  }
  Serial.println("Saiu do while");
  if ((Card != "") && checkCardInEEPROM())
  {
    deleteCardFromEEPROM(Card);
    Serial.println("deveria apagar");
    return;
  }
  else if ((Card = !"") && !checkCardInEEPROM())
  {
    Serial.println("DEVERIA SALVAR AGORA");
    saveCardToEEPROM(Card);
    return;
  }
}

void setDoorOpen()
{
  IS_DOOR_OPEN = true;
}
void setDoorClosed()
{
  IS_DOOR_OPEN = false;
}

void handleReadingMode()
{
  while (!IS_DOOR_OPEN)
  {
    String Card = readCard();
    if ((Card != "") && checkCardInEEPROM())
    {
      blinkOpenLight();
      AuthorizedID = Card;
      Serial.println(Card);
      Serial.println(AuthorizedID);
      Serial.println(IS_DOOR_OPEN);
      setDoorOpen();
    }
  }
  while (IS_DOOR_OPEN)
  {
    String Card = readCard();
    // Serial.println("TA ABERTA");
    if (Card != "" && (Card == AuthorizedID))
    {
      setDoorClosed();
    }
  }
  delay(500);
}

void ManageDoorMode(bool isProgrammingMode)
{
  if (!isProgrammingMode)
    return handleReadingMode();

  handleProgrammingMode();
}

void ManageLightMode(bool isDoorOpen, bool isProgrammingMode)
{
  if (isProgrammingMode)
  {
    return setProgrammingLight();

    Serial.println("RETURN MAS CONTINUOU");
  }
  Serial.print("doorStatus:");
  Serial.println(isDoorOpen);
  if (!isDoorOpen)
  {
    return setClosedLight();
    Serial.println("RETURN MAS CONTINUOU");
  }
  Serial.println("Setando luzopen");
  setOpenLight();
}
