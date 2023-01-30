#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <SPI.h>       //Biblioteca de comunicação SPI, para comunição do módulo RFID
#include <MFRC522.h>   //Biblioteca do módulo RFID
#include <EEPROM.h>    //Biblioteca da memória EEPROM
#include <MemoryLib.h> //Biblioteca para gerenciar a EEPROM com variáveis

#define SDA_RFID 5  // SDA do RFID no pino 10
#define RST_RFID 2  // Reset do RFID no pino 9
#define ledGreen 22 // Led verde no pino 2
#define ledRed 13   // Led vermelho no pino 3
#define relePin 12

MFRC522 mfrc522(SDA_RFID, RST_RFID);   // Inicializa o módulo RFID
MemoryLib memory(1, 2);                // Inicializa a biblioteca MemoryLib. Parametros: memorySize=1 (1Kb) / type=2 (LONG)
int maxCards = memory.lastAddress / 2; // Cada cartão ocupa duas posições na memória. Para 1Kb será permitido o cadastro de 101 cartões
String cards[101] = {};                // Array com os cartões cadastrados

String UID = "";

const char *accessPointSSID = "Sala1307";
const char *accessPointPassword = "Senhasala1307";

IPAddress local_IP(192, 168, 13, 07);
IPAddress gateway(192, 168, 1, 5);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer webServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);

bool IS_DOOR_OPEN = true;
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
  initRFID();
};

void loop()
{
  webSocket.loop();

  ManageDoorMode(IS_REGISTER_MODE);
  ManageLightMode(IS_DOOR_OPEN, IS_REGISTER_MODE);

  if (Serial.available())
  {
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

void initRFID()
{
  // Configura os pinos
  pinMode(ledGreen, OUTPUT);
  pinMode(ledRed, OUTPUT);
  pinMode(relePin, OUTPUT);

  digitalWrite(ledGreen, LOW);
  digitalWrite(ledRed, LOW);
  digitalWrite(relePin, LOW);

  // Inicia SPI
  SPI.begin();

  // Inicia o modulo RFID MFRC522
  mfrc522.PCD_Init();

  // Retorna os cartões armazenados na memória EEPROM para o array
  UpdateLocalCardsFromMemory();

  //! DEBUG -> Deletar as linhas abaixo: Elas forçam a adição das tags no array
  // memory.write(0, long(14534186121));
  // cards[0] = String(14534186121);
  // Serial.println("CARD:" + cards[0]);

  // Pisca os leds sinalizando a inicialização do circuito
  for (int i = 0; i < 10; i++)
  {
    digitalWrite(ledRed, HIGH);
    digitalWrite(ledGreen, HIGH);
    delay(50);
    digitalWrite(ledRed, LOW);
    digitalWrite(ledGreen, LOW);
    delay(50);
  }
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
  return (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial());
}

void ReadCardUID()
{
  UID = "";
  if (hasCardToRead())
  {
    for (byte i = 0; i < mfrc522.uid.size; i++)
    {
      UID += String(mfrc522.uid.uidByte[i]);
    }
    Serial.println("[READ] Card Readed with UID: " + String(UID));
  }
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
  digitalWrite(relePin, HIGH);
  IS_DOOR_OPEN = true;

  delay(350);
  sendCurrentStatus();
}

void setDoorClosed()
{
  Serial.println("[DOOR] Door is CLOSED now!");
  digitalWrite(relePin, LOW);
  IS_DOOR_OPEN = false;

  delay(350);
  sendCurrentStatus();
}

void setOpenLight()
{
  digitalWrite(ledGreen, HIGH);
  digitalWrite(ledRed, LOW);
}

void setClosedLight()
{
  digitalWrite(ledRed, HIGH);
  digitalWrite(ledGreen, LOW);
}

void setProgrammingLight()
{
  digitalWrite(ledRed, HIGH);
  digitalWrite(ledGreen, LOW);
  delay(500);
  digitalWrite(ledGreen, HIGH);
  digitalWrite(ledRed, LOW);
  delay(500);
}

void setUnauthorizedLight()
{
  digitalWrite(ledGreen, LOW);
  digitalWrite(ledRed, LOW);
  for (int i = 0; i < 6; i++)
  {
    digitalWrite(ledRed, HIGH);
    delay(150);
    digitalWrite(ledRed, LOW);
    delay(150);
  }
  delay(500);
}

void setAuthorizedLight()
{
  digitalWrite(ledGreen, LOW);
  digitalWrite(ledRed, LOW);
  for (int i = 0; i < 6; i++)
  {
    digitalWrite(ledGreen, HIGH);
    delay(150);
    digitalWrite(ledGreen, LOW);
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
  ReadCardUID();

  if (UID != "")
  {
    Serial.println("[ACCESS] Trying to access with card UID: " + String(UID));

    boolean canAccess = hasCardInLocalMemory(UID);

    if (canAccess)
    {
      Serial.println("[AUTHORIZED] Access GRANTED with UID: " + String(UID));
      toggleDoorStatus();
      return setAuthorizedLight();
    }

    Serial.println("[UNAUTHORIZED] Access DENIED with UID: " + String(UID));
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
  UID = "";

  Serial.println("[REGISTER] Waiting read a card to add/remove");

  for (int times = 0; times < 100; times++)
  {
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledRed, HIGH);
    delay(10);
    digitalWrite(ledGreen, HIGH);
    digitalWrite(ledRed, LOW);

    ReadCardUID();
    if (UID == "")
      continue;

    break;
  }

  if (UID == "")
  {
    Serial.println("[REGISTER] No card read!");
  }
  else if (hasCardInLocalMemory(UID))
  {
    deleteCardFromMemory(UID);
  }
  else if (hasNoMemorySpace())
  {
    Serial.println("[REGISTER] FULL Memory. Card not added!");
  }
  else
  {
    addCardToMemory(UID);
  }

  digitalWrite(ledGreen, LOW);
  digitalWrite(ledRed, LOW);

  Serial.println("[REGISTER] Finish. Returning to Reading state!");
  return setReadingState();
}

bool hasCardInLocalMemory(String cardID)
{
  boolean hasCard = false;
  for (int c = 0; c < maxCards; c++)
  {
    if (UID == cards[c])
    {
      hasCard = true;
      break;
    }
  }

  if (hasCard)
    return true;
  return false;
}

void deleteCardFromMemory(String cardID)
{
  for (int memPOS = 0; memPOS < maxCards; memPOS++)
  {
    if (cards[memPOS] == UID)
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
    if (cards[cleanMemPosition].toInt() == 0)
    {
      Serial.println("[MEMORY] Saving new card of UID " + String(UID) + " in memory position " + cleanMemPosition);
      cards[cleanMemPosition] = UID;

      break;
    }
  }

  printAllCards();

  // TODO -> SALVAR NA EEPROM AQUI

  // TODO -> Após salvar na EEPROM, atualizar os cards da memória com a EEPROM para ficar SYNCADO
  // UpdateLocalCardsFromMemory()

  return setAuthorizedLight();
}

bool hasNoMemorySpace()
{
  if (cards[maxCards - 1].toInt() != 0)
  {
    digitalWrite(ledGreen, LOW);
    for (int i = 0; i < 15; i++)
    {
      digitalWrite(ledRed, HIGH);
      delay(100);
      digitalWrite(ledRed, LOW);
      delay(100);
    }
    return true;
  }
  return false;
}

void UpdateLocalCardsFromMemory()
{
  int memPos = 0;
  for (int iter = 0; iter < maxCards; iter++)
  {
    Serial.println(memory.read(memPos));
    Serial.println(memory.read(memPos + 1));
    String readUID = String(memory.read(memPos)) + String(memory.read(memPos + 1));
    Serial.println("[MEMORY] Inserting card with UID [" + readUID + "] in local memory at pos :" + iter);
    cards[iter] = readUID;
    memPos += 2;
  }
}

void printAllCards()
{
  int founded = 0;
  for (int memPosition = 0; memPosition < maxCards; memPosition++)
  {
    if (cards[memPosition].toInt() != 0)
    {
      founded += 1;
      Serial.println("[MEMORY] Card founded in position" + String(memPosition) + " with ID " + cards[memPosition]);
    }
  }
  Serial.println("[MEMORY] Founded Cards: " + founded);
}
