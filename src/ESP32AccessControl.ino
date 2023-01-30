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
  } else if (Event == "getData") {
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

void UpdateLocalCardsFromMemory()
{
  int memPos = 0;
  for (int iter = 0; iter < maxCards; iter++)
  { 
    Serial.println(memory.read(memPos));
    Serial.println(memory.read(memPos + 1));
    String readUID = String(memory.read(memPos)) + String(memory.read(memPos + 1));
    Serial.println("[UPDATING HERE MEM] UID: " + readUID + " | POS: " + iter);
    cards[iter] = readUID;
    memPos += 2;
  }
}

void ReadCardUID()
{
  UID = "";
  // Verifica a presença de um novo cartao e efetua a leitura
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
  {
    // Retorna o UID para a variavel UIDcard
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
  sendCurrentStatus();
  delay(350);
}

void setReadingState()
{
  Serial.println("[PROGRAMMING] Changed to Reading state!");
  IS_REGISTER_MODE = false;
  sendCurrentStatus();
  delay(350);
}

void setDoorOpen()
{
  Serial.println("[UNLOCK] Door is OPEN now!");
  digitalWrite(relePin, HIGH);
  IS_DOOR_OPEN = true;

  sendCurrentStatus();
  delay(350);
}

void setDoorClosed()
{
  Serial.println("[UNLOCK] Door is CLOSED now!");
  digitalWrite(relePin, LOW);
  IS_DOOR_OPEN = false;

  sendCurrentStatus();
  delay(350);
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
  // Serial.println("Tentando LER");
  // Variável UID recebe o valor do cartão lido
  ReadCardUID();

  if (UID != "")
  {
    boolean canAccess = false;
    Serial.println("[ACCESS] Trying to change door state with card UID: " + String(UID));
    // Efetua a leitura de todas as posições do array
    for (int c = 0; c < maxCards; c++)
    {
      if (cards[c] != 00)
        Serial.println("Card " + cards[c] + " | " + String(UID) + " | " + String(cards[c]));
      if (UID == cards[c])
      {
        Serial.println("ID da memória:" + cards[c]);
        // Se a posição do array for igual ao cartão lido, seta a varíavel como verdadeira e finaliza o for
        canAccess = true;
        break;
      }
    }
    // Variável verdadeira, efetua o acesso. Caso contrário, pisca o led vermelho
    if (canAccess)
    {
      Unlock();
      canAccess = false;
    }
    else
    {
      for (int i = 0; i < 6; i++)
      {
        digitalWrite(ledGreen, LOW);
        digitalWrite(ledRed, HIGH);
        delay(150);
        digitalWrite(ledRed, LOW);
        delay(150);
        digitalWrite(ledRed, HIGH);
        delay(150);
        digitalWrite(ledRed, LOW);
        delay(150);
      }
      delay(500);
    }
  }
}

void Unlock()
{
  Serial.println("[UNLOCK] Alterando porta para o estado " + String(!IS_DOOR_OPEN));
  if (IS_DOOR_OPEN)
  {
    return setDoorClosed();
  }

  return setDoorOpen();
}

void handleProgrammingMode()
{
  UID = "";
  boolean isDeleteCard = false;

  // Efetua a leitura do cartao para cadastro
  // Aprox 5 segundos para cadastrar o cartao
  Serial.println("[REGISTER] Tentando ler um cartão para adicionar ou remover");
  for (int t = 0; t < 150; t++)
  {
    delay(10);

    digitalWrite(ledGreen, HIGH);
    digitalWrite(ledRed, HIGH);

    ReadCardUID();
    if (UID != "")
    {
      // Se leu um cartão, mantém somente o led verde acesso enquanto segue executando o procedimento
      digitalWrite(ledRed, LOW);
      Serial.println("[REGISTER] Cartão encontrado com ID: " + UID);
      // Verifica se o cartao ja esta cadastrado
      for (int iter = 0; iter < maxCards; iter++)
      {
        // Se já estiver cadastrado, exclui o cartão do array e também da memória
        if (cards[iter] == UID)
        {
          digitalWrite(ledGreen, LOW);
          digitalWrite(ledRed, HIGH);
          delay(150);
          digitalWrite(ledRed, LOW);
          delay(150);
          digitalWrite(ledRed, HIGH);
          cards[iter] = "0";
          isDeleteCard = true;
        }
      }
      break;
    }
  }

  if (UID == "")
  {
    Serial.println("[REGISTER] Nenhum cartão aproximado para o cadastro.\nRetornando ao modo leitura!");
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledRed, LOW);
    setReadingState();
    return;
  }

  if (isDeleteCard == false)
  {
    // Se for inclusão de novo cartão, verifica se ainda existe espaco para novos cadastros
    // Se a última posição da memória for diferente de zero, pisca o led vermelho sinalizando
    // que não existe mais espaço para novos cartões, e finaliza o procedimento
    if (hasNoMemorySpace())
      return;

    Serial.println("[REGISTER] Adding new access to local register");
    for (int cleanMemPosition = 0; cleanMemPosition < maxCards; cleanMemPosition++)
    {
      if (cards[cleanMemPosition].toInt() == 0)
      { // Posicao livre
        Serial.println("[EEPROM] Saving new card of UID " + String(UID) + " in memory position " + cleanMemPosition);
        cards[cleanMemPosition] = UID;

        break; // finaliza o for
      }
    }
    printAllCards();
  }

  // Grava na memória os cartões do array
  // Cada cartão ocupa duas posições da memória
  for (int iter = 0; iter <= memory.lastAddress; iter++)
  { // Limpa os valores da memória
    memory.write(iter, 0);
  }

  Serial.println("[EEPROM] Memory cleaned!");

  int a = 0;
  for (int c = 0; c < maxCards; c++)
  {
    if (cards[c].toInt() != 0)
    {
      Serial.println("Card " + UID + " | space " + c);
      Serial.println("POS1 " + String(cards[c].substring(0, 6).toInt()));
      Serial.println("POS2 " + String(cards[c].substring(6, cards[c].length()).toInt()) + "\n");
      memory.write(a, cards[c].substring(0, 6).toInt());
      memory.write(a + 1, cards[c].substring(6, cards[c].length()).toInt());
      a += 2;
    }
  }

  // Retorna os valores da memória para o array, para ajustar as posições do cartão no array como está na memória
  // UpdateLocalCardsFromMemory();

  if (isDeleteCard == false)
  {
    digitalWrite(ledGreen, LOW);
    delay(150);
    digitalWrite(ledGreen, HIGH);
    delay(150);
    digitalWrite(ledGreen, LOW);
    delay(150);
    digitalWrite(ledGreen, HIGH);
    delay(150);
    digitalWrite(ledGreen, LOW);
  }
  else
  {
    // Apaga os leds verde e vermelho
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledRed, LOW);
  }
}

bool hasNoMemorySpace()
{
  if (cards[maxCards - 1].toInt() != 0)
  {
    Serial.println("[REGISTER] Memória CHEIA");
    digitalWrite(ledGreen, LOW);
    for (int i = 0; i < 10; i++)
    {
      digitalWrite(ledRed, HIGH);
      delay(100);
      digitalWrite(ledRed, LOW);
      delay(100);
      digitalWrite(ledRed, HIGH);
      delay(100);
      digitalWrite(ledRed, LOW);
      delay(100);
      digitalWrite(ledRed, HIGH);
      delay(100);
      digitalWrite(ledRed, LOW);
      delay(100);
    }
    return true;
  }
  return false;
}

void printAllCards()
{
  int founded = 0;
  for (int memPosition = 0; memPosition < maxCards; memPosition++)
  {
    if (cards[memPosition].toInt() != 0)
    {
      founded += 1;
      Serial.println("[LOCALMEM] Card founded in position" + String(memPosition) + " with ID " + cards[memPosition]);
    }
  }
  Serial.println("[LOCALMEM] Founded Cards: " + founded);
}