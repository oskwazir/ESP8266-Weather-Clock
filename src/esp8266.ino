#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <FS.h>
#include <Arduino.h>
#include <ArduinoJson.h> 

#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <WiFiUdp.h>

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <ESP8266HTTPClient.h>

#define USE_SERIAL Serial
WiFiUDP UDP;                     // Create an instance of the WiFiUDP class to send and receive
IPAddress timeServerIP;          // time.nist.gov NTP server address
const char* NTPServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48;  // NTP time stamp is in the first 48 bytes of the message

byte NTPBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing packets

unsigned long intervalNTP = 60000; // Request NTP time every minute
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();
uint32_t timeUNIX = 0;

unsigned long prevActualTime = 0;

#define OLED_RESET 0
Adafruit_SSD1306 display(OLED_RESET);

#if (SSD1306_LCDHEIGHT != 32)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

char openweathermap_key[35];
String openweathermap_uri = "http://api.openweathermap.org/data/2.5/weather?units=metric&id=6167865&appid=";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

uint32_t getTime() {
  if (UDP.parsePacket() == 0) { // If there's no response (yet)
    return 0;
  }
  UDP.read(NTPBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (NTPBuffer[40] << 24) | (NTPBuffer[41] << 16) | (NTPBuffer[42] << 8) | NTPBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  // subtract seventy years:
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime;
}

void sendNTPpacket(IPAddress& address) {
  memset(NTPBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  NTPBuffer[0] = 0b11100011;   // LI, Version, Mode
  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(NTPBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}

inline int getSeconds(uint32_t UNIXTime) {
  return UNIXTime % 60;
}

inline int getMinutes(uint32_t UNIXTime) {
  return UNIXTime / 60 % 60;
}

inline int getHours(uint32_t UNIXTime) {
  return UNIXTime / 3600 % 24;
}

void startUDP() {
  Serial.println("Starting UDP");
  UDP.begin(123);                          // Start listening for UDP messages on port 123
  Serial.print("Local port:\t");
  Serial.println(UDP.localPort());
  Serial.println();
}


void setup() {

  pinMode(LED_BUILTIN, OUTPUT);    
  USE_SERIAL.begin(115200);

    //read configuration from FS json
  USE_SERIAL.println("mounting file system...");

  if (SPIFFS.begin()) {
      USE_SERIAL.println("mounted file system");
      if (SPIFFS.exists("/config.json")) {
        //file exists, reading and loading
        USE_SERIAL.println("reading config file");
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
          USE_SERIAL.println("opened config file");
          size_t size = configFile.size();
          // Allocate a buffer to store contents of the file.
          std::unique_ptr<char[]> buf(new char[size]);
  
          configFile.readBytes(buf.get(), size);
          DynamicJsonBuffer jsonBuffer;
          JsonObject& json = jsonBuffer.parseObject(buf.get());
          json.printTo(Serial);
          if (json.success()) {
            USE_SERIAL.println("\nparsed json");
  
            strcpy(openweathermap_key, json["openweathermap_key"]);
          } else {
            USE_SERIAL.println("failed to load json config");
          }
        }
      }
    } else {
      USE_SERIAL.println("failed to mount FS");
    }

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_openweathermap_api_key("weatherapikey", "openweathermap api key", openweathermap_key, 35);
  
  //Locally initialized, then it's gone because we don't need it anymore.
  WiFiManager wifiManager;
  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  wifiManager.addParameter(&custom_openweathermap_api_key);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("ESP8266-Omer", "easypassword")) {
    USE_SERIAL.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //Blocking until WiFi AP configuration is complete
  USE_SERIAL.println("Wifi AP configuration successful...");

  startUDP();

    if(!WiFi.hostByName(NTPServerName, timeServerIP)) { // Get the IP address of the NTP server
    Serial.println("DNS lookup failed. Rebooting.");
    Serial.flush();
    ESP.reset();
  }
  Serial.print("Time server IP:\t");
  Serial.println(timeServerIP);
  
  Serial.println("\r\nSending NTP request ...");
  sendNTPpacket(timeServerIP); 

  //read custom parameters
      USE_SERIAL.print("openweathermap_key before strcpy=> ");
      USE_SERIAL.println(strlen(openweathermap_key));
      USE_SERIAL.println(openweathermap_key);
      strcpy(openweathermap_key, custom_openweathermap_api_key.getValue());
      USE_SERIAL.println("openweathermap_key after strcpy=>");
      USE_SERIAL.println(openweathermap_key);
      USE_SERIAL.println(strlen(openweathermap_key));

    if(strlen(openweathermap_key) != 32){
      wifiManager.resetSettings();
      shouldSaveConfig = false;
      USE_SERIAL.println(openweathermap_key);
      USE_SERIAL.println(strlen(openweathermap_key));
    }

    //save the custom parameters to FS
    if (shouldSaveConfig) {
      Serial.println("saving config");
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["openweathermap_key"] = openweathermap_key;
  
      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        Serial.println("failed to open config file for writing");
      }
  
      json.printTo(Serial);
      json.printTo(configFile);
      configFile.close();
      //end save
    }

    USE_SERIAL.println("local ip ");
    USE_SERIAL.println(WiFi.localIP());

    openweathermap_uri = openweathermap_uri + openweathermap_key;

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
      // draw a white circle, 10 pixel radius
    display.fillCircle(display.width()/2, display.height()/2, 10, WHITE);
    display.display();
    delay(2000);
    display.clearDisplay();
}

void loop() {
  unsigned long currentMillis = millis();
  display.clearDisplay();
  if (currentMillis - prevNTP > intervalNTP) { // If a minute has passed since last NTP request
    prevNTP = currentMillis;
    Serial.println("\r\nSending NTP request ...");
    sendNTPpacket(timeServerIP);               // Send an NTP request

        HTTPClient http;
    
    digitalWrite(LED_BUILTIN, HIGH); 
    USE_SERIAL.println("[HTTP] begin...");
    // configure traged server and url
    http.begin(openweathermap_uri); //HTTP

    USE_SERIAL.println("[HTTP] GET...");
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      USE_SERIAL.printf("[HTTP] GET... code: %d", httpCode);
      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        DynamicJsonBuffer jsonBuffer;
        digitalWrite(LED_BUILTIN, LOW);
        String response = http.getString();
        
        const char* json = response.c_str();
//      response.toCharArray(json);
        JsonObject& root = jsonBuffer.parseObject(json);
        
         if (!root.success()) {
          Serial.println("parseObject() failed");
          return;
        } else {
          
          JsonObject& weather0 = root["weather"][0];
          const char* weather0_main = weather0["main"];
          const char* weather0_description = weather0["description"];
          USE_SERIAL.println(weather0_main);
          
          JsonObject& main = root["main"];
          const char* main_temp = main["temp"];
          const char* main_temp_min = main["temp_min"]; // 4
          const char* main_temp_max = main["temp_max"];
          USE_SERIAL.println(main_temp);

          display.clearDisplay();
          display.setTextSize(1);
          display.setTextColor(WHITE);
          display.setCursor(0,0);
          display.printf("Current %sC\n", main_temp);
          display.printf("High %sC - Low %sC\n", main_temp_max, main_temp_min);                    
          display.printf("%s\n", weather0_main);
          display.printf("%s\n", weather0_description);
          display.display();
  
        }
        digitalWrite(LED_BUILTIN, HIGH); 
      }
    } else {
      USE_SERIAL.printf("[HTTP] GET... failed, error: %s", http.errorToString(httpCode).c_str());
    }

    
    http.end();
  }

  uint32_t time = getTime();                   // Check if an NTP response has arrived and get the (UNIX) time
  if (time) {                                  // If a new timestamp has been received
    timeUNIX = time;
    Serial.print("NTP response:\t");
    Serial.println(timeUNIX);
    lastNTPResponse = currentMillis;
  } else if ((currentMillis - lastNTPResponse) > 3600000) {
    Serial.println("More than 1 hour since last NTP response. Rebooting.");
    Serial.flush();
    ESP.reset();
  }

  uint32_t actualTime = timeUNIX + (currentMillis - lastNTPResponse)/1000;
  if (actualTime != prevActualTime && timeUNIX != 0) { // If a second has passed since last print
    prevActualTime = actualTime;
    Serial.printf("\rUTC time:\t%d:%d:%d   ", getHours(actualTime), getMinutes(actualTime), getSeconds(actualTime));
  }  
  
}
