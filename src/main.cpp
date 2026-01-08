// for SL2

#define USE_PN532 // with "RFID Reader Unit (PN532)"
#define USE_MIFARE // use MIFARE with "RFID Reader Unit (PN532) 
//#define WITHOUT_WIFI // without WiFi, read ID from SD(id.txt), and record log to SD(log.txt)

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#ifdef USE_PN532
#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <PN532_debug.h>
#else
#include "MFRC522_I2C.h"
#endif
#include <FastLED.h>
#include "SD.h"

//#define UNLOCK_TEST

#define LED_INTENSITY 70

#define PIN_SCL 15 // Grove on board, SL2
#define PIN_SDA 13 // Grove on board, SL2
#define PIN_SOL 1 // SL2
#define PIN_SW  3 // SL2
#define PIN_LED 43 // LED on board, SL2
//#define PIN_LED 21 // LED on StampS3

#define PWM_STRONG_ON 255
#define PWM_WEAK_ON   25
#define PWM_OFF			  0

char GAS_URL[128];
char WIFI_SSID[32];
char WIFI_PASSWORD[64];
char WIFI_ID[64];
#ifdef USE_PN532
PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);
#else
MFRC522 mfrc522(0x28);
#endif

StaticJsonDocument<1024> json_doc;
String IDlist = "";
File file;
#define NUM_LEDS 1
CRGB leds[NUM_LEDS];

void showLED(uint8_t r, uint8_t g, uint8_t b) {
//  pixels.setPixelColor(0, pixels.Color(r, g, b));
//  pixels.show();
	// PL9823=RGB / WS2812=GRB
  leds[0] = CRGB(g, r, b);
  FastLED.show();
}

uint16_t tmKeepUnlock = 0;
bool fUnlock = false;

void setUnlock(bool f=true)
{
	if (f == 1){
		printf("Unlock\n");
//		showLED(30, 0, 0); // red
//		delay(10);
		analogWrite(PIN_SOL, PWM_STRONG_ON);
		delay(300);
		analogWrite(PIN_SOL, PWM_WEAK_ON);
//		delay(10);
//		showLED(0, 0, 0); 
		fUnlock = true;
	}
	else {
		digitalWrite(PIN_SOL, LOW);
		printf("Lock\n");
		fUnlock = false;
	}
}

#define LOCK_STATUS_LOCKED true
#define LOCK_STATUS_UNLOCKED false

bool getLockStatus(){
	if (digitalRead(PIN_SW) == LOW) {
		return LOCK_STATUS_LOCKED; // locked
	} else {
		return LOCK_STATUS_UNLOCKED; // unlocked
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

void ShowError(uint16_t n)
{
	for (uint16_t i = 0; i < n; i++) {
		showLED(LED_INTENSITY, 0, 0); delay(100);
		showLED(0, 0, 0);	delay(100);
	}
}

bool readIDlist(){

	// IDlist:
	// [{"id":"CardID","active":"enable=1"},{"id":"d98e4e51c","active":0},{"id":"d995d243","active":1},{"id":"884986d79","active":1},{"id":"884f19ce1","active":1},{"id":"881d87a8ba","active":1}]

	#ifdef WITHOUT_WIFI
	// read ID list from SD
	IDlist = "[{\"id\":\"CardID\",\"active\":\"enable=1\"}";
	file = SD.open("/id.csv", "r");
	if (file) {
		while(file.available()){
			String line = file.readStringUntil('\n');
			line.trim();
			int s = line.indexOf(',');
			if (s > 0){
				String id = line.substring(0, s);
				String val = line.substring(s + 1);
				if (line.length() > 0){
					IDlist += ",{\"id\":\""+id+"\",\"active\":"+val+"}";
				}
			}
		}
		IDlist += "]";
		file.close();
	} else {
		printf("failed to open id.csv.r\n");
		ShowError(1000);
		return false;
	}
#else
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
	
	IDlist = msg;
//	deserializeJson(json_doc, msg);
//	printf("%s\n", json_doc.as<String>().c_str());
#endif
	printf("ID list: %s\n", IDlist.c_str());
	return true;
}

bool recordLog(String id){
#ifdef WITHOUT_WIFI
	// record log to SD card
	file = SD.open("/log.csv", FILE_APPEND);
	if (file) {
		String logline = "";
		logline += id;
		logline += "\n";
		file.print(logline);
		file.close();
		printf("Log recorded to SD: %s\n", logline.c_str());
	} else {
		printf("log.csv open error\n");
		ShowError(1000);
		return false;
	}
#else
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
#endif
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
#ifdef USE_PN532
 #ifdef USE_MIFARE
 	uint8_t ret;
  uint8_t idm[8];
	//printf("Waiting for a Mifare card...\n");
	uint8_t uidLength; // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
  // 'uid' will be populated with the UID, and uidLength will indicate
  // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
  ret = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, idm, &uidLength, 22); // timeout:1000=28s / 22=1s
  if (ret) {
	  // Display some basic information about the card
   	printf("Found an ISO14443A card, uid=%x (len=%d)\n", idm, uidLength);
	  if (ret == 1){
			for (byte i = 0; i < 8; i++) {
				id += String(idm[i], HEX);
			}
		}
		printf("card ID: %s (%d)\n", id.c_str(), id.length());
	  if (uidLength == 4){
	    // We probably have a Mifare Classic card ... 
//    	printf("Seems to be a Mifare Classic card (4 byte UID)\n");
	    // Now we need to try to authenticate it for read/write access
     	// Try with the factory default KeyA: 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF
//     	printf("Trying to authenticate block 4 with default KEYA value\n");
     	uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	  	// Start with block 4 (the first block of sector 1) since sector 0
	  	// contains the manufacturer data and it's probably better just
	  	// to leave it alone unless you know what you're doing
     	ret = nfc.mifareclassic_AuthenticateBlock(idm, uidLength, 4, 0, keya);
     	if (ret){
// 	     	printf("Sector 1 (Blocks 4..7) has been authenticated\n");
       	uint8_t data[16];
       	// If you want to write something to block 4 to test with, uncomment
				// the following line and this text should be read back in a minute
       	// data = { 'a', 'd', 'a', 'f', 'r', 'u', 'i', 't', '.', 'c', 'o', 'm', 0, 0, 0, 0};
      	 	// success = nfc.mifareclassic_WriteDataBlock (4, data);
       	// Try to read the contents of block 4
       	ret = nfc.mifareclassic_ReadDataBlock(4, data);
       	if (ret){
         	// Data seems to have been read ... spit it out
//         	printf("Reading Block 4: %x\n", data);
         	// Wait a bit before reading the card again
//         	delay(1000);
       	}
       	else{
//         	printf("Ooops ... unable to read the requested block.  Try another key?\n");
       	}
     	}
     	else{
//       	printf("Ooops ... authentication failed: Try another key？\n");
     	}
     }    
     if (uidLength == 7){
      // We probably have a Mifare Ultralight card ...
//      printf("Seems to be a Mifare Ultralight tag (7 byte UID)\n");
      // Try to read the first general-purpose user page (#4)
//      printf("Reading page 4: ");
      uint8_t data[32];
      ret = nfc.mifareultralight_ReadPage (4, data);
      if (ret){
        // Data seems to have been read ... spit it out
//        printf("%x\n", data);
        // Wait a bit before reading the card again
//        delay(1000);
      }
      else{
//        printf("Ooops ... unable to read the requested page!?\n");
      }
    }
	}
 #else
	uint8_t success;
	uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
	uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
	
	// Check for a new card
	success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50);
	if (success) {
		// UID found
		for (uint8_t i = 0; i < uidLength; i++) {
			id += String(uid[i], HEX);
		}
	}
	uint8_t ret;
  uint16_t systemCode = 0xFFFF;
  uint8_t requestCode = 0x01;       // System Code request
  uint8_t idm[8];
  uint8_t pmm[8];
  uint16_t systemCodeResponse;
  ret = nfc.felica_Polling(systemCode, requestCode, idm, pmm, &systemCodeResponse, 30); // timeout=100 -> about 3sec
  if (ret == 1){
		for (byte i = 0; i < 8; i++) {
			id += String(idm[i], HEX);
		}
	}
 #endif
#else
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
#endif
//	printf("card ID: %s (%d)\n", id.c_str(), id.length());
	return(id);
}

void setup() {
	M5.begin();
	//M5.Ex_I2C.begin(); // need for ATOMS3's Grove port
	Wire.end();
  Wire.begin(PIN_SDA, PIN_SCL); // Grove on board
//	pixels.begin();

	FastLED.addLeds<NEOPIXEL, PIN_LED>(leds, NUM_LEDS);

	analogWrite(PIN_SOL, PWM_OFF);
	pinMode(PIN_SW, INPUT_PULLUP);

	showLED(LED_INTENSITY, 0, 0);
#ifdef USE_PN532
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion(); // 32010607
  nfc.setPassiveActivationRetries(0xFF);
  nfc.SAMConfig();
#else
	mfrc522.PCD_Init(); // Init MFRC522
#endif
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

#ifdef WITHOUT_WIFI
#else
	// read WiFi config from SD
	strcpy(WIFI_SSID, ""); strcpy(WIFI_PASSWORD, ""); strcpy(WIFI_ID, "");
	file = SD.open("/wifi.txt", "r");
	while(file.available()){
		String line = file.readStringUntil('\n');
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
	file.close();
	printf("%s / %s / %s / %s\n", WIFI_SSID, WIFI_PASSWORD, WIFI_ID, GAS_URL);
	connectWiFi(); // connect WiFi at startup
	readIDlist();
#endif
	printf("Reading ID list at boot...\n");
	showLED(LED_INTENSITY, 0, LED_INTENSITY);
	readIDlist();
	printf("ID list: %s\n", IDlist.c_str());
	showLED(0, 0, 0);
}

#define KEEP_FORCE_UNLOCK_AFTER_UNLOCK 10  // [s]
#define KEEP_UNLOCK_AFTER_UNLOCK       60  // [s]

String cardID = "", lastCardID = "";

void loop() {
	M5.update();
	if (M5.BtnA.wasClicked()){
#ifdef UNLOCK_TEST
		setUnlock(1);
#else
	#ifdef WITHOUT_WIFI
	#else
		connectWiFi();
	#endif
		printf("Reading ID list...\n");
		showLED(LED_INTENSITY, 0, LED_INTENSITY);
		readIDlist();
		printf("ID list: %s\n", IDlist.c_str());
		showLED(0, 0, 0);
#endif
	}

/*
（用語の定義）
弱通電＝ソレノイドに弱通電し金具をはめてもロックされない状態
強通電＝ソレノイドに強通電し金具を吐き出す状態
非通電＝ソレノイドに通電しておらず金具をはめるとロックされる状態
解錠＝金具が吐き出された（扉が開いている）状態
施錠＝金具がはまっている（扉は閉じている）状態

1.初期状態: 非通電、施錠
2.解錠動作: 強通電(300ms)→弱通電
2-1. 解錠になった場合: 非通電→4.へ
2-2. 施錠のままの場合: 弱通電を継続→3.へ

3. 解錠動作を行ったが施錠のままの場合: 弱通電を継続
3-1. 解錠になった場合: 非通電→扉を閉じて施錠になったら1.へ戻る
3-2. 施錠のまま60秒経過: 解錠動作をあきらめ、非通電→1.へ戻る

4. 解錠された状態: 扉を閉じて施錠になったら1.へ戻る

*/
	printf("%d %d %c\n", tmKeepUnlock, fUnlock, (getLockStatus() == LOCK_STATUS_LOCKED)?'L':'U');
	if (fUnlock == true){
		// unlock operated
		if (tmKeepUnlock < KEEP_FORCE_UNLOCK_AFTER_UNLOCK){
			printf("Keeping unlock... %d\n", tmKeepUnlock);
			tmKeepUnlock++;
			if (getLockStatus() == LOCK_STATUS_UNLOCKED){ // actually unlocked
				printf("Actually unlocked(0)\n");
				showLED(0, 0, LED_INTENSITY); // blue when unlocked
			}
		}
		else{
			printf("Checking lock status...");
			if (getLockStatus() == LOCK_STATUS_UNLOCKED){ // actually unlocked
				printf("Actually unlocked(1)\n");
				showLED(0, 0, LED_INTENSITY); // blue when unlocked
				//delay(100);
				tmKeepUnlock = 0;
		 		analogWrite(PIN_SOL, PWM_OFF); // turn off when actually unlocked
				//delay(100);
				fUnlock = false;
				// record log when actually unlocked	
				showLED(LED_INTENSITY, LED_INTENSITY, LED_INTENSITY); // white
				printf("Record log for card %s\n", lastCardID.c_str());
				bool res = recordLog(lastCardID);
				if (res == false){
					printf("Failed to record log for card %s\n", lastCardID.c_str());
					for (uint8_t i = 0; i < 10; i++){
						showLED(LED_INTENSITY, 0, 0); delay(100);
						showLED(0, 0, 0);	delay(100);
					}
				}
				delay(1000);
				showLED(0, 0, 0);
			}
			else{ // still actually locked
				printf("Still locked\n");
				showLED(0, LED_INTENSITY, 0); // green when locked
				tmKeepUnlock++;
//#define RETAIN_UNLOCK_TIME 600 // [x100ms], 60sec
				if (tmKeepUnlock >= KEEP_UNLOCK_AFTER_UNLOCK){
					// if still locked after RETAIN_UNLOCK_TIME, give up unlock
					analogWrite(PIN_SOL, PWM_OFF); // turn off
					delay(100);
					for (uint8_t i = 0; i < 3; i++){
						// flash green when giving up unlock
						showLED(0, LED_INTENSITY, 0); delay(100);
						showLED(0, 0, 0);	delay(100);
					}
					fUnlock = false;
					tmKeepUnlock = 0;
				}
			}
		}
	}
	else{
		if (getLockStatus() == LOCK_STATUS_UNLOCKED){ // actually unlocked
			printf("Actually unlocked(2)\n");
			showLED(0, 0, LED_INTENSITY); // blue when unlocked
			delay(100);
		}
		else{ // locked
		  printf("Locked\n");
		  showLED(0, LED_INTENSITY, 0); // green when locked
			delay(100);
		}
	}
	cardID = getCardID();
	if (cardID.length() > 0) {
		printf("Card ID: %s [%d]\n", cardID.c_str(), cardID.length());
		lastCardID = cardID;
#ifdef UNLOCK_TEST
//   赤: 未登録カード
//   紫: 登録済みカード(disbaled)
//   黄: 登録済みカード(enabled)→解錠動作後消灯
#else
		int cardStatus = checkIDstatus(cardID);
		if (cardStatus == 1) {
			printf("Card %s is enabled\n", cardID.c_str());
			showLED(LED_INTENSITY+20, LED_INTENSITY, 0); // yellow
			setUnlock(1);
			tmKeepUnlock = 0;
/*
			showLED(LED_INTENSITY, LED_INTENSITY, LED_INTENSITY); // white
			bool res = recordLog(cardID);
			if (res == false){
				printf("Failed to record log for card %s\n", cardID.c_str());
				for (uint8_t i = 0; i < 10; i++){
					showLED(LED_INTENSITY, 0, 0); delay(100);
					showLED(0, 0, 0);	delay(100);
				}
			}
			delay(1000);
			showLED(0, 0, 0);
*/
		} else if (cardStatus == 0) {
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
	delay(10);
}
