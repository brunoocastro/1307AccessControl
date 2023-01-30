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

MFRC522 mfrc522(SDA_RFID, RST_RFID); // Inicializa o módulo RFID
MemoryLib memory(1, 2);              // Inicializa a biblioteca MemoryLib. Parametros: memorySize=1 (1Kb) / type=2 (LONG)
int maxCards = 101;                  // Cada cartão ocupa duas posições na memória. Para 1Kb será permitido o cadastro de 101 cartões
String cards[101] = {};              // Array com os cartões cadastrados

String UID = "";

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

  // Inicia SPI
  SPI.begin();

  // Inicia o modulo RFID MFRC522
  mfrc522.PCD_Init();

  // Retorna os cartões armazenados na memória EEPROM para o array
  ReadMemory();

  //! DEBUG -> Deletar as linhas abaixo: Elas forçam a adição das tags no array
  memory.write(0, long(14534186121));
  cards[0] = String(14534186121);
  Serial.println("CARD:" + cards[0]);

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

  delay(1000);
  Serial.println("Mode: " + WiFi.getMode());

  delay(500);
  WiFi.mode(WIFI_AP);

  delay(1000);
  Serial.println("Mode: " + WiFi.getMode());

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
  Serial.println("Handling with event of type: " + Event + " | value: " + newStatus);
  Serial.println(Event == "isDoorOpen");

  if (Event == "isRegistrationMode")
  {
    Serial.print("Set Registration Mode");
    Serial.println(newStatus);

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
    Serial.println("Set Door Status to :" + String(newStatus) + " | " + (newStatus == 1) + " | " + String(newStatus == true));
    Serial.println(newStatus == 1, newStatus);

    if (newStatus == true)
    {
      setDoorOpen();
    }
    else
    {
      setDoorClosed();
    }
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

void ReadMemory()
{
  int a = 0;
  for (int c = 0; c < maxCards; c++)
  {
    cards[c] = String(memory.read(a)) + String(memory.read(a + 1));
    a += 2;
  }
}

void ReadCard()
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
  }
}

void setProgrammingState()
{
  Serial.println("[PROGRAMMING] Changed to Programming state!");
  IS_REGISTER_MODE = true;
}

void setReadingState()
{
  Serial.println("[PROGRAMMING] Changed to Reading state!");
  IS_REGISTER_MODE = false;
}

void setDoorOpen()
{
  Serial.println("[UNLOCK] Door is OPEN now!");
  digitalWrite(relePin, HIGH);
  IS_DOOR_OPEN = true;
  delay(1000);
}

void setDoorClosed()
{
  Serial.println("[UNLOCK] Door is CLOSED now!");
  digitalWrite(relePin, LOW);
  IS_DOOR_OPEN = false;
  delay(1000);
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
  // Variável UID recebe o valor do cartão lido
  ReadCard();

  if (UID != "")
  {
    boolean canAccess = false;
    // Efetua a leitura de todas as posições do array
    for (int c = 0; c < maxCards; c++)
    {
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
    }
    else
    {
      for (int i = 0; i < 6; i++)
      {
        digitalWrite(ledRed, HIGH);
        delay(50);
        digitalWrite(ledRed, LOW);
        delay(50);
      }
      delay(500);
    }

    canAccess = false;
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
  String UID = "";
  boolean deleteCard = false;

  // Efetua a leitura do cartao para cadastro
  // Aprox 5 segundos para cadastrar o cartao
  Serial.println("[REGISTER] Tentando ler um cartão para adicionar ou remover");
  for (int t = 0; t < 150; t++)
  {
    delay(10);
    // Acende os leds verde e vermelho
    digitalWrite(ledGreen, HIGH);
    digitalWrite(ledRed, HIGH);
    // Faz a leitura o cartao
    ReadCard();
    if (UID != "")
    {
      // Se leu um cartão, mantém somente o led verde acesso enquanto segue executando o procedimento
      digitalWrite(ledRed, LOW);
      Serial.println("[REGISTER] Cartão encontrado com ID: " + UID);
      // Verifica se o cartao ja esta cadastrado
      for (int c = 0; c < maxCards; c++)
      {
        // Se já estiver cadastrado, exclui o cartão do array e também da memória
        if (cards[c] == UID)
        {
          digitalWrite(ledRed, HIGH);
          digitalWrite(ledGreen, LOW);
          cards[c] = "0";
          deleteCard = true;
        }
      }
      break; // finaliza o for
    }
  }

  // Cancela o cadastro se nenhum cartao foi lido
  if (UID == "")
  {
    // Apaga os leds verde e vermelho
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledRed, LOW);
    return;
  }

  if (deleteCard == false)
  {
    // Se for inclusão de novo cartão, verifica se ainda existe espaco para novos cadastros
    // Se a última posição da memória for diferente de zero, pisca o led vermelho sinalizando
    // que não existe mais espaço para novos cartões, e finaliza o procedimento
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
      }
      return;
    }
  }

  // Adiciona o cartão no array, somente se for inclusão de novo cartão
  if (deleteCard == false)
  {
    Serial.println("[REGISTER] Adicionando cartão a memória");
    for (int c = 0; c < maxCards; c++)
    {
      if (cards[c].toInt() == 0)
      { // Posicao livre
        cards[c] = UID;
        break; // finaliza o for
      }
    }
  }

  // Grava na memória os cartões do array
  // Cada cartão ocupa duas posições da memória
  for (int e = 0; e <= memory.lastAddress; e++)
  { // Limpa os valores da memória
    memory.write(e, 0);
  }
  int a = 0;
  for (int c = 0; c < maxCards; c++)
  {
    if (cards[c].toInt() != 0)
    {
      memory.write(a, cards[c].substring(0, 6).toInt());
      memory.write(a + 1, cards[c].substring(6, cards[c].length()).toInt());
      a += 2;
    }
  }

  // Retorna os valores da memória para o array, para ajustar as posições do cartão no array como está na memória
  ReadMemory();

  if (deleteCard == false)
  {
    Unlock();
  }
  else
  {
    // Apaga os leds verde e vermelho
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledRed, LOW);
  }
}