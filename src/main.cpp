#include <Arduino.h>
#include <M5Unified.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "MFRC522_I2C.h"

MFRC522 mfrc522(0x28);

// with ATOMS3
// SCL: G8
// SDA: G7
// SOL: G5
// SW : G6
// NeoPix: G39

#define PIN_SCL 8
#define PIN_SDA 7
#define PIN_SOL 5
#define PIN_SW  6
#define PIN_LED 39

// GAS URL (to be stored in SD):
const char* GAS_URL = "https://script.google.com/macros/s/AKfycbwh1XZGEyKaU5-AY-gR72gtN6R-61t-ldEwJBB1Eoed3_pafeoFOxRQwDIWoaY3r-IT/exec";

// WiFi Settings (to be stored in SD):
const char* WIFI_SSID = "ifdl";
const char* WIFI_PASSWORD = "hogeupip5";
const char* WIFI_ID = ""; // ID for IEEE802.X, null for non-IEEE802.X

StaticJsonDocument<200> json_doc;

void setUnlock(bool f=true)
{
	if (f == 1){
		printf("Unlock\n");
		digitalWrite(PIN_SOL, HIGH);
		delay(1000);
		digitalWrite(PIN_SOL, LOW);
	} else {
		digitalWrite(PIN_SOL, LOW);
		printf("Lock\n");
	}

}

bool readIDlist(){
	// get ID list from GAS
	// GET -> parse ID
/*
target = target[target.index(MagicWord) + len(MagicWord):]
target = target[:target.index(MagicWord)]
str_w = ''
while len(target) > 0:
  if target[:2] == '\\x':
    str_w += chr(int(target[2:4], 16))
    target = target[4:]
  elif target[:2] == '\\\\':
    target = target[2:]
  else:
    str_w += target[0]
    target = target[1:]
*/
}

bool recordLog(String id){
	// POST JSON: {"action": "log", "id": "<ID>", [option:"time": "<timestamp>"]}
  WiFiClientSecure client;
  client.setInsecure();
  if(!client.connect("script.google.com", 443)) {
    M5.Display.printf("connect error!\n");
    return false;
  }
  String json_request;
  json_doc["action"] = "log";
  json_doc["id"] = id;

  serializeJson(json_doc, json_request);
  // M5.Display.printf("JSON string: %s\n", json_request.c_str()); // for debug

  String request = String("")
              + "POST " + GAS_URL + " HTTP/1.1\r\n"
              + "Host: script.google.com\r\n"
              + "Content-type: application/json\r\n"
              + "Content-Length: " + String(json_request.length()) + "\r\n"
              + "Connection: close\r\n\r\n"
              + String(json_request) + "\r\n";
  printf("request: %s\n", request.c_str());
  client.print(request);

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    printf("%s\n", line.c_str());
  }
  client.stop();

  return true;
}

bool checkIDstatus(String id){
	// true=enabled, false=disabled
}

void setup() {
	M5.begin();
	pinMode(PIN_SOL, OUTPUT); digitalWrite(PIN_SOL, LOW);
	pinMode(PIN_SW, INPUT_PULLUP);
	mfrc522.PCD_Init();  // Init MFRC522

  M5.Display.printf("WiFi connecting...\n");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Display.print(".");
  }
  M5.Lcd.printf("ready\n");
}

void loop() {
	M5.update();
	if (M5.BtnA.wasClicked()){
		readIDlist();
	}
	if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
		delay(200);
		return;
	}
	for (byte i = 0; i < mfrc522.uid.size; i++) {  // Output the stored UID data
//        M5.Lcd.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
//        M5.Lcd.print(mfrc522.uid.uidByte[i], HEX);
	}
}
