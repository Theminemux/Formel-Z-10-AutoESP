#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include <WebServer.h>

#include <ESP32Servo.h>

// Pins
#define RST_PIN 19
#define SDA_PIN 16
#define SCK_PIN 17
#define MOSI_PIN 5
#define MISO_PIN 18

// Servo-Pin (anpassen, falls du einen anderen nutzt)
#define SERVO_PIN 4

char* ssid = "rescuerobotcar";
char* password = "mint2025";
char* carIp = "";

String lastCardData = ""; 

MFRC522 mfrc522(SDA_PIN, RST_PIN);
WebServer server(80);
Servo gateServo;

void handleServoUp()
{
  Serial.println("HTTP: servo_up called");
  gateServo.writeMicroseconds(2600); // Tor auf
  server.send(200, "text/plain", "servo_up");
}

void handleServoDown()
{
  Serial.println("HTTP: servo_down called");
  gateServo.write(0); // Tor zu
  server.send(200, "text/plain", "servo_down");
}

void printCardInfo() {
  Serial.println("--- Tag detected ---");
  Serial.print("UID: ");
  Serial.println();
  Serial.print("UID size: "); Serial.println(mfrc522.uid.size);
  Serial.print("SAK: 0x"); Serial.println(mfrc522.uid.sak, HEX);
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.print("PICC Type: "); Serial.println(mfrc522.PICC_GetTypeName(piccType));
}

String GetCardData(){
  String data = "";
  // Read blocks 4-6 (sector 1 data blocks) and convert to UTF-8 string
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  byte trailerBlock = 7;
  
  if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid)) == MFRC522::STATUS_OK) {
    const int maxBlocks = 3;
    uint8_t allbuf[16 * maxBlocks];
    memset(allbuf, 0, sizeof(allbuf));
    int pos = 0;
    
    for (int b = 0; b < maxBlocks; ++b) {
      byte blockAddr = 4 + b;
      byte buffer[18];
      byte size = sizeof(buffer);
      MFRC522::StatusCode status = mfrc522.MIFARE_Read(blockAddr, buffer, &size);
      
      if (status == MFRC522::STATUS_OK) {
        for (int i = 0; i < 16; ++i) {
          allbuf[pos++] = buffer[i];
        }
      }
    }
    
    // Convert to String, trim trailing zeros
    int len = pos;
    while (len > 0 && allbuf[len-1] == 0) len--;
    
    if (len > 0) {
      data = String((const char*)allbuf).substring(0, len);
    }
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  return data;
}

void NewCardDetected(String cardData)
{
  String json = "{\"rfid_reader\":\"new_card\",\"data\":\"" + cardData + "\"}";
  // JSON: {"rfid_reader":"new_card","data":"CardDataHere"}

  Serial.println("New card detected with data: " + cardData);

  HTTPClient http;
  http.begin("http://" + String(carIp) + ":5000/sensors/rfidupdate");

  http.addHeader("Content-Type", "application/json");   // WICHTIG für ASP.NET

  int httpCode = http.POST(json);

  if (httpCode == HTTP_CODE_OK)
  {
    Serial.println("Data sent to car successfully");
  }
  else
  {
    Serial.print("Failed to send data to car, HTTP code: ");
    Serial.println(httpCode);
  }

  http.end();
}

void setup() {
  Serial.begin(9600);
  delay(100);
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SDA_PIN);
  mfrc522.PCD_Init();
  Serial.println("MFRC522 initialized");

  // login to wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to WiFi. IP address: ");
  Serial.println(WiFi.localIP());

  // Servo initialisieren
  gateServo.setPeriodHertz(50);       // Standard-Servo: 50 Hz
  gateServo.attach(SERVO_PIN);        // Servo-Pin
  gateServo.write(0);                 // Startposition: zu

  server.on("/servo_up", HTTP_GET, handleServoUp);
  server.on("/servo_down", HTTP_GET, handleServoDown);
  server.begin();
  Serial.println("HTTP server started");

  /*
  // Ask server for orangepi ip address
  HTTPClient http;
  http.begin("http://5.175.245.160:8300/text");
  int httpCode = http.GET();

  // Log the HTTP response code and handle errors
  Serial.print("HTTP Response code: ");
  Serial.println(httpCode);

  if (httpCode <= 0) {
    Serial.println("Failed to get orangepi IP, restarting...");
    ESP.restart();
  }
  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Failed to get orangepi IP. body empty, restarting...");
    ESP.restart();
  }
  char* orangepiIp = strdup(http.getString().c_str());
  Serial.print("Orangepi IP: ");
  Serial.println(orangepiIp);
  http.end();

  // Ask orangepi for car ip address
  http.begin("http://" + String(orangepiIp) + "/api/getip/?device=rescuecar");
  httpCode = http.GET();

  Serial.print("HTTP Response code: ");
  Serial.println(httpCode);

  if (httpCode <= 0) {
    Serial.println("Failed to get car IP from orangepi, restarting...");
    ESP.restart();
  }
  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Failed to get car IP from orangepi. body empty, restarting...");
    ESP.restart();
  }
  String carResponse = http.getString();
  Serial.print("Car response: ");
  Serial.println(carResponse);

  carResponse.trim();
  carIp = strdup(carResponse.c_str());

  Serial.print("Car IP: ");
  Serial.println(carIp);
  http.end();
  */
  carIp = "192.168.137.25";

  Serial.println("Setup complete, waiting for RFID cards...");
}

void loop() {
  server.handleClient();
  // Read RFID cards continuously
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    printCardInfo();
    String cardData = GetCardData();
    if (cardData.length() > 0) {
      Serial.print("Card Data: ");
      Serial.println(cardData); 
      if (cardData != lastCardData) {
        Serial.println("New card detected with different data!");

        NewCardDetected(cardData);

        lastCardData = cardData;
      } else {
        Serial.println("Same card detected again.");
      }
    } else {
      Serial.println("No data read from card.");
    }
    delay(100); // Wait 100 milliseconds before next read
  }
}