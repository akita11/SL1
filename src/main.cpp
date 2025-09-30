// for SL2
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "MFRC522_I2C.h"
#include <Adafruit_NeoPixel.h>
#include "SD.h"

//#define UNLOCK_TEST

MFRC522 mfrc522(0x28);

#define LED_INTENSITY 70

#define PIN_SCL 15 // Grove on board, SL2
#define PIN_SDA 13 // Grove on board, SL2
#define PIN_SOL 1 // SL2
#define PIN_SW  3 // SL2
#define PIN_LED 43 // LED on board, SL2
//#define PIN_LED 21 // LED on StampS3

char GAS_URL[128];
char WIFI_SSID[32];
char WIFI_PASSWORD[64];
char WIFI_ID[64];

StaticJsonDocument<1024> json_doc;
String IDlist = "";
File configFile;
#define NUM_LEDS 1
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_LEDS, PIN_LED, NEO_GRB + NEO_KHZ800);

void showLED(uint8_t r, uint8_t g, uint8_t b) {
	pixels.setPixelColor(0, r, g, b);
	pixels.show();
}

void setUnlock(bool f=true)
{
	if (f == 1){
		printf("Unlock\n");
		digitalWrite(PIN_SOL, HIGH);
		showLED(30, 0, 0);
		delay(1000);
		digitalWrite(PIN_SOL, LOW);
		showLED(0, 0, 0);
	}
	else {
		digitalWrite(PIN_SOL, LOW);
		printf("Lock\n");
	}
}

bool getLockStatus(){
	if (digitalRead(PIN_SW) == LOW) {
		return true; // locked
	} else {
		return false; // unlocked
	}	
}

// LED
// 起動時赤点灯: RFID Unit初期化失敗
// ふだん：
//   緑: ロック状態
//   青: アンロック状態
// ボタン押したとき：
//   青点滅(ゆっくり): WiFi接続中
//   赤点滅(高速10回): WiFi接続失敗
//   紫点灯: IDリスト取得中
// IDカードタッチ時：
//   赤: 未登録カード
//   紫: 登録済みカード(disbaled)
//   橙: 登録済みカード(enabled)→白(記録中)→その後消灯（記録エラー=赤点滅(高速10回)）

bool connectWiFi(){
	uint16_t nTrial = 0;
	uint8_t f = 0;
	printf("WiFi connecting to %s...\n", WIFI_SSID);
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
	while (WiFi.status() != WL_CONNECTED && nTrial < 30	) {
		if (f == 0) showLED(0, 0, LED_INTENSITY); else showLED(0, 0, 0);
		f = 1 - f;
		printf(".");
		delay(500);
	}
	if (WiFi.status() != WL_CONNECTED) {
		printf("WiFi connection failed\n");
		for (uint8_t i = 0; i < 10; i++) {
			showLED(LED_INTENSITY, 0, 0); delay(100);
			showLED(0, 0, 0);	delay(100);
		}
		return false;
	} else {
		printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
		showLED(0, 0, 0);
		return true;
	}
}

bool readIDlist(){
	// get ID list from GAS
	// GET -> parse ID

	WiFiClientSecure client;
	client.setInsecure();
	if(!client.connect("script.google.com", 443)) {
		printf("failed to connect GAS server\n");
		return false;
	}
	String request = String("")
							+ "GET " + GAS_URL + " HTTP/1.1\r\n"
							+ "Host: script.google.com\r\n"
							+ "Content-type: plain/text\r\n"
							+ "Content-Length: 0" + "\r\n"
							+ "Connection: close\r\n\r\n";
	client.print(request);

	String msg0 = "";
	while (client.connected()) {
		String line = client.readStringUntil('\n');
		msg0 += line + "\n";
	}
	client.stop();

	// ToDo: chunk decode for response body

	//	printf("Received message: %s\n", msg.c_str());
	String MagicWord = "shEEt";

	int s = msg0.indexOf(MagicWord);
	msg0 = msg0.substring(s + MagicWord.length());
	int e = msg0.indexOf(MagicWord);
	msg0 = msg0.substring(0, e);
	String msg = "";
	for (int i = 0; i < msg0.length(); i++) {
		if (msg0.substring(i, i+2) == "\\x"){
			String hex = msg0.substring(i + 2, i + 4);
			char c = (char)strtol(hex.c_str(), NULL, 16);
			msg += c;
			i += 3; // skip next two characters
		} else if (msg0.substring(i, i+2) == "\\\\") {
			//msg += '\\'; // escape backslash
			i += 1; // skip next character
		} else {
			msg += msg0[i];			
		}
	}
	
	// [{"id":"CardID","active":"enable=1"},{"id":"d98e4e51c","active":0},{"id":"d995d243","active":1},{"id":"884986d79","active":1},{"id":"884f19ce1","active":1},{"id":"881d87a8ba","active":1}]
	// printf("msg: %s\n", msg.c_str());

	IDlist = msg;
//	deserializeJson(json_doc, msg);
//	printf("%s\n", json_doc.as<String>().c_str());
	return true;
}

bool recordLog(String id){
	// POST JSON: {"action": "log", "id": "<ID>", [option:"time": "<timestamp>"]}
	WiFiClientSecure client;
	client.setInsecure();
	if(!client.connect("script.google.com", 443)) {
		printf("connect error!\n");
		return false;
	}
	String json_request;
	json_doc["action"] = "log";
	json_doc["id"] = id;

	serializeJson(json_doc, json_request);
//  printf("JSON string: %s\n", json_request.c_str()); // for debug

	String request = String("")
							+ "POST " + GAS_URL + " HTTP/1.1\r\n"
							+ "Host: script.google.com\r\n"
							+ "Content-type: application/json\r\n"
							+ "Content-Length: " + String(json_request.length()) + "\r\n"
							+ "Connection: close\r\n\r\n"
							+ String(json_request) + "\r\n";
//  printf("request: %s\n", request.c_str());
	client.print(request);

	while (client.connected()) {
		String line = client.readStringUntil('\n');
//    printf("Response: %s\n", line.c_str());
	}
	client.stop();

	return true;
}

// returns 1=enabled, 0=disabled, -1=not found
int checkIDstatus(String id){
	// [{id:CardID,active:enable=1},{id:d98e4e51c,active:0},{id:d995d243,active:1},{"id":"884986d79","active":1},{"id":"884f19ce1","active":1},{"id":"881d87a8ba","active":1}]
	int res = -1;
	for (int i = 0; i < IDlist.length(); i++) {
		if (IDlist.substring(i, i + id.length()) == id) {
			if (IDlist.substring(i + id.length() + 11, i + id.length() + 12) == "1") res = 1;
			else res = 0;
		}
	}
	return(res);
}

String getCardID(){
	String id = "";
	if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
		//printf("no card\n");
	}
	else{
		for (byte i = 0; i < mfrc522.uid.size; i++) {
			id += String(mfrc522.uid.uidByte[i], HEX);
			//printf("%02x ", mfrc522.uid.uidByte[i]);
		}
		//printf("\n");
	}
	//printf("card ID: %s\n", id.c_str());
	return(id);
}

void setup() {
	M5.begin();
	//M5.Ex_I2C.begin(); // need for ATOMS3's Grove port
  Wire.begin(PIN_SDA, PIN_SCL); // Grove on board
	pinMode(PIN_LED, OUTPUT);
	pixels.begin();

	/*
	while(1){
		printf("hoge\n");
		showLED(80, 0, 0); delay(100);
		showLED(0, 0, 0); delay(100);
	}
	*/
	pinMode(PIN_SOL, OUTPUT); digitalWrite(PIN_SOL, LOW);
	pinMode(PIN_SW, INPUT_PULLUP);

	showLED(LED_INTENSITY, 0, 0);
	mfrc522.PCD_Init(); // Init MFRC522
	showLED(0, 0, 0);

	// for SD card, SL2
	SPI.begin(5, 7, 9); // SCK, MISO, MOSI
	bool fSD = SD.begin(44, SPI, 25000000); // SS pin, SPI bus, frequency
	if (fSD == false){
		while(1){
			printf("SD init error\n");
			showLED(LED_INTENSITY, 0, 0); delay(100);
			showLED(0, 0, 0); delay(100);
		}
	}

	//for (uint8_t i = 0; i < 10; i++){ printf("ready\n"); delay(500); }

	// read config from SD
	strcpy(WIFI_SSID, ""); strcpy(WIFI_PASSWORD, ""); strcpy(WIFI_ID, "");
	configFile = SD.open("/wifi.txt", "r");
	while(configFile.available()){
		String line = configFile.readStringUntil('\n');
		line.trim();
		printf("Read line: %s\n", line.c_str());
		int s = line.indexOf(' ');
		if (s > 0){
			String key = line.substring(0, s);
			String val = line.substring(s + 1);
			if (key == "SSID") strcpy(WIFI_SSID, val.c_str());
			else if (key == "PASSWORD") strcpy(WIFI_PASSWORD, val.c_str());
			else if (key == "ID") strcpy(WIFI_ID, val.c_str());
			else if (key == "GAS_URL") strcpy(GAS_URL, val.c_str());
			printf("Read from SD: %s=%s\n", key.c_str(), val.c_str());
		}
	}
	configFile.close();
	printf("%s / %s / %s / %s\n", WIFI_SSID, WIFI_PASSWORD, WIFI_ID, GAS_URL);
	connectWiFi(); // connect WiFi at startup
}

void loop() {
	M5.update();
	if (M5.BtnA.wasClicked()){
#ifdef UNLOCK_TEST
		setUnlock(1);
#else
		connectWiFi();
		printf("Reading ID list...\n");
		showLED(LED_INTENSITY, 0, LED_INTENSITY);
		readIDlist();
		printf("ID list: %s\n", IDlist.c_str());
		showLED(0, 0, 0);
#endif
	}
	if (getLockStatus() == 1) showLED(0, 0, 30); else showLED(0, 30, 0);

	String cardID = getCardID();
	if (cardID.length() > 0) {
		printf("Card ID: %s\n", cardID.c_str());
#ifdef UNLOCK_TEST
//   赤: 未登録カード
//   紫: 登録済みカード(disbaled)
//   黄: 登録済みカード(enabled)→解錠動作後消灯
#else
		if (checkIDstatus(cardID) == 1) {
			printf("Card %s is enabled\n", cardID.c_str());
			showLED(LED_INTENSITY+20, LED_INTENSITY, 0); // yellow
			setUnlock(1);
			showLED(LED_INTENSITY, LED_INTENSITY, LED_INTENSITY);
			bool res = recordLog(cardID);
			if (res == false){
				printf("Failed to record log for card %s\n", cardID.c_str());
				for (uint8_t i = 0; i < 10; i++){
					showLED(LED_INTENSITY, 0, 0); delay(100);
					showLED(0, 0, 0);	delay(100);
				}
			}
			showLED(0, 0, 0);
		} else if (checkIDstatus(cardID) == 0) {
			printf("Card %s is disabled\n", cardID.c_str());
			showLED(LED_INTENSITY, 0, LED_INTENSITY+30); // purple
			delay(1000);
			setUnlock(0);
		} else {
			printf("Card %s not found in ID list\n", cardID.c_str()); // red
			showLED(LED_INTENSITY, 0, 0);
			setUnlock(0);
			delay(1000);
		}
#endif
	}
	delay(100);
}
