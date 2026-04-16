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

const String ssid = "rescuerobotcar";
const String password = "mint2025";
const String deviceName = "rescuecar-esp32";
const String carName = "rescuecar";
const String serverUrl = "http://5.175.245.160:8300/text";
String orangepiIp = "";

String carIp = "";
String lastCardData = ""; 

MFRC522 mfrc522(SDA_PIN, RST_PIN);
WebServer server(80);
Servo gateServo;

void handleConnectionCheck() 
{
  Serial.println("HTTP: Connection check called");
  server.send(200);
}

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

bool SendJsonPost(const String& targetName, const String& url, const String& json)
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("[HTTP] WiFi not connected, skip send to ");
    Serial.println(targetName);
    return false;
  }

  for (int attempt = 1; attempt <= 2; attempt++) {
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    http.setReuse(false);

    Serial.print("[HTTP] POST ");
    Serial.print(targetName);
    Serial.print(" attempt ");
    Serial.print(attempt);
    Serial.print(" -> ");
    Serial.println(url);

    if (!http.begin(url)) {
      Serial.print("[HTTP] begin() failed for ");
      Serial.println(targetName);
      return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");

    int httpCode = http.POST(json);
    if (httpCode == HTTP_CODE_OK) {
      Serial.print("Data sent to ");
      Serial.print(targetName);
      Serial.println(" successfully");
      http.end();
      return true;
    }

    if (httpCode <= 0) {
      Serial.print("Failed to send data to ");
      Serial.print(targetName);
      Serial.print(", HTTP code: ");
      Serial.println(httpCode);
      Serial.print("[HTTP] Transport error: ");
      Serial.println(http.errorToString(httpCode));
    } else {
      Serial.print("Failed to send data to ");
      Serial.print(targetName);
      Serial.print(", HTTP status: ");
      Serial.println(httpCode);
      String responseBody = http.getString();
      if (responseBody.length() > 0) {
        Serial.print("[HTTP] Response body: ");
        Serial.println(responseBody);
      }
    }

    http.end();
    delay(150);
  }

  return false;
}

void NewCardDetected(String cardData)
{
  String json = "{\"rfid_reader\":\"new_card\",\"data\":\"" + cardData + "\"}";
  // JSON: {"rfid_reader":"new_card","data":"CardDataHere"}

  Serial.println("New card detected with data: " + cardData);

  //SendJsonPost("macbook", "http://192.168.137.252:8005/api/rfidscan", json);

  if (carIp.length() > 0) {
    String carUrl = "http://" + carIp + "/sensors/rfidupdate";
    SendJsonPost("car", carUrl, json);
  } else {
    Serial.println("[HTTP] carIp is empty, skip send to car");
  }

  if (orangepiIp.length() > 0) {
    String orangepiBase = orangepiIp;
    if (!orangepiBase.startsWith("http://") && !orangepiBase.startsWith("https://")) {
      orangepiBase = "http://" + orangepiBase;
    }
    String orangepiUrl = orangepiBase + "/api/rfidscan";
    SendJsonPost("orangepi", "http://" + orangepiIp + "/api/rfidscan", json);
  } else {
    Serial.println("[HTTP] orangepiIp is empty, skip send to orangepi");
  }
}

void setup() {
  Serial.begin(9600);
  delay(100);
  Serial.println("[SETUP] Booting ESP32...");
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SDA_PIN);
  mfrc522.PCD_Init();
  Serial.println("[SETUP] MFRC522 initialized");

  // login to wifi
  Serial.print("[WIFI] Connecting to SSID: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("[WIFI] Connected. IP address: ");
  Serial.println(WiFi.localIP());

  // Servo initialisieren
  gateServo.setPeriodHertz(50);       // Standard-Servo: 50 Hz
  gateServo.attach(SERVO_PIN);        // Servo-Pin
  gateServo.write(0);                 // Startposition: zu

  server.on("/servo/servo_up", HTTP_GET, handleServoUp);
  server.on("/servo/servo_down", HTTP_GET, handleServoDown);
  server.on("/api/checkconnection", HTTP_GET, handleConnectionCheck);
  server.begin();
  Serial.println("[SETUP] HTTP server started");

  // Ask server for orangepi ip address
  HTTPClient http;
  Serial.print("[HTTP] Request OrangePi host from: ");
  Serial.println(serverUrl);
  http.begin(serverUrl);
  int httpCode = http.GET();

  Serial.print("[HTTP] Response code (orangepi host): ");
  Serial.println(httpCode);
  String orangepiResponse = "";
  if (httpCode <= 0) {
    Serial.print("[HTTP] Transport error: ");
    Serial.println(http.errorToString(httpCode));
  } else {
    orangepiResponse = http.getString();
    Serial.print("[HTTP] Response body (orangepi host): ");
    Serial.println(orangepiResponse);
  }

  if (httpCode <= 0) {
    Serial.println("[HTTP] Failed to get orangepi IP, restarting...");
    http.end();
    ESP.restart();
  }
  if (httpCode != HTTP_CODE_OK) {
    Serial.println("[HTTP] Failed to get orangepi IP. body empty, restarting...");
    http.end();
    ESP.restart();
  }
  orangepiIp = orangepiResponse;
  orangepiIp.trim();
  if (orangepiIp.length() == 0) {
    Serial.println("[HTTP] OrangePi IP is empty after trim, restarting...");
    http.end();
    ESP.restart();
  }
  Serial.print("[HTTP] Orangepi IP: ");
  Serial.println(orangepiIp);
  http.end();

  // Log in to orangepi
  String registerUrl = "http://" + orangepiIp + "/api/register/?device=" + deviceName;
  Serial.print("[HTTP] Register request: ");
  Serial.println(registerUrl);
  http.begin(registerUrl);
  httpCode = http.GET();

  Serial.print("[HTTP] Response code (register): ");
  Serial.println(httpCode);
  if (httpCode <= 0) {
    Serial.print("[HTTP] Register transport error: ");
    Serial.println(http.errorToString(httpCode));
  } else {
    Serial.print("[HTTP] Register response body: ");
    Serial.println(http.getString());
  }

  if (httpCode <= 0) {
    Serial.println("[HTTP] Registration failed (transport), restarting...");
    http.end();
    ESP.restart();
  }
  if (httpCode != HTTP_CODE_OK) {
    Serial.println("[HTTP] Registration failed (status != 200), restarting...");
    http.end();
    ESP.restart();
  }
  http.end();

  // Ask orangepi for car ip address
  String carIpUrl = "http://" + orangepiIp + "/api/getip/?device=" + carName;
  Serial.print("[HTTP] Request car IP from: ");
  Serial.println(carIpUrl);
  http.begin(carIpUrl);
  httpCode = http.GET();

  Serial.print("[HTTP] Response code (car IP): ");
  Serial.println(httpCode);
  if (httpCode <= 0) {
    Serial.print("[HTTP] Car IP transport error: ");
    Serial.println(http.errorToString(httpCode));
  }

  if (httpCode <= 0) {
    Serial.println("[HTTP] Failed to get car IP from orangepi, restarting...");
    http.end();
    ESP.restart();
  }
  if (httpCode != HTTP_CODE_OK) {
    Serial.println("[HTTP] Failed to get car IP from orangepi. body empty, restarting...");
    http.end();
    ESP.restart();
  }
  String carResponse = http.getString();
  Serial.print("[HTTP] Car response: ");
  Serial.println(carResponse);

  carResponse.trim();
  carIp = carResponse;

  Serial.print("[SETUP] Car IP: ");
  Serial.println(carIp);
  http.end();
  //carIp = "192.168.137.25";

  Serial.println("[SETUP] Setup complete, waiting for RFID cards...");
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