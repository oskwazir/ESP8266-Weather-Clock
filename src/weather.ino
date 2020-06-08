#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <FS.h>
#include <Arduino.h>
#include <ArduinoJson.h> 

#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <ESP8266HTTPClient.h>

#define USE_SERIAL Serial

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

  delay(30000);
}
