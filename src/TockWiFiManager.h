#ifndef TOCKWM
#define TOCKWM

#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#ifdef ESP32
  #include <SPIFFS.h>
#endif


#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <Update.h>



//WiFiManager
WiFiManager wifiManager;
std::vector<WiFiManagerParameter*> wifiParams;
WiFiManagerParameter ota_enabled ("ota", "ota", "0", 2, "<br> OTA Enabled <hr>",0);
WiFiManagerParameter aws_file ("aws_file", "aws_file", "/mod-water-awsmqtt-v1.0.bin", 20, "<br> AWS File Update<hr>",0);

String awsFileMQtt;

void checkConfigButton();
void saveConfigCallback();
void firmwareUpdate(String awsFile);


void setupWM() {
  Serial.begin(9600);
  Serial.println();

  // // clean FS, for testing
  // SPIFFS.format();
  // wifiManager.resetSettings(); 

  // initializing Parameters
  //const char* custom_radio_str = "<br/><label for='customfieldid'>Input/Output</label> <br> <input type='radio' name='customfieldid' value='1' checked> Input <br> <input type='radio' name='customfieldid' value='2'> Output <br>";
  // wifiParams.push_back(new WiFiManagerParameter(custom_radio_str);
  wifiParams.push_back(&ota_enabled); // nao colocar 1 emlength
  wifiParams.push_back(&aws_file); // nao colocar 1 emlength
  wifiParams.push_back(new WiFiManagerParameter("high", "Altura (cm)", "20", 4, "<br> Altura (cm) <hr>",0));
  wifiParams.push_back(new WiFiManagerParameter("offset", "Recuo (cm)", "130", 4, "<br> Recuo (cm) <hr>",0));
  wifiParams.push_back(new WiFiManagerParameter("trigger", "Trigger", "4", 2, "<br> Trigger <hr>",0));
  wifiParams.push_back(new WiFiManagerParameter("echo", "Echo", "5", 2, "<br> Echo <hr>",0));
  
  

  //read configuration from FS json
  Serial.println("mounting FS...");
  if (SPIFFS.begin()){
    Serial.println("mounted file system");
    if (SPIFFS.exists(fileConfig)) {
      Serial.println("reading config file");
      DynamicJsonDocument jsonDoc(512);
      File configFile = SPIFFS.open(fileConfig, "r");
      if (configFile) {
        // Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);

        auto deserializeError = deserializeJson(jsonDoc, buf.get());
        // serializeJson(jsonDoc, Serial);
        if ( ! deserializeError ) {
          JsonObject fileJsonObj  = jsonDoc.as<JsonObject>();

          if(jsonDoc.containsKey("aws_file_mqtt")){
            awsFileMQtt = fileJsonObj["aws_file_mqtt"].as<String>();
          }
        
        // update wifiParams
        for (size_t i = 0; i < wifiParams.size(); i++)
        { 
         
          // custom
          if(fileJsonObj.containsKey(wifiParams[i]->getLabel())){
            auto value = fileJsonObj[wifiParams[i]->getLabel()];
            wifiParams[i]->setValue(value,wifiParams[i]->getValueLength());
          }

        
        }
        
        Serial.println("\nfileJsonObj retrieved form memory: ");
        serializeJsonPretty(fileJsonObj,Serial); //debug
       
        fileJsonObj.clear();

        } else {
          Serial.println("failed to load json config");
        }

        configFile.close();
        jsonDoc.clear();

      }
    }
  } else Serial.println("failed to mount FS");


  // config pins after try get on memory

  for (size_t i = 0; i < wifiParams.size(); i++){
    wifiManager.addParameter(wifiParams[i]);
  }

  // configs
  wifiManager.setSaveConnectTimeout(10);
  wifiManager.setConfigPortalBlocking(true);
  wifiManager.setConfigPortalTimeout(60);
  wifiManager.setConnectTimeout(10);
  wifiManager.setAPClientCheck(true);
  wifiManager.setPreSaveConfigCallback(saveConfigCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback); 


  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(getClientID().c_str(), "tock1234")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.print("Connected to wifi. My IP: ");
  Serial.println(WiFi.localIP());
  flipper.attach(0.3, flip); // pisca rapido enquanto tenta se conectar ao wifi

  
  // check if update from webserver config
  // firmwareUpdate(String(aws_file.getValue()));
  // if(String(ota_enabled.getValue()).equals("1")){
  //   firmwareUpdate(String(aws_file.getValue()));
  // }
  // check if there are config to update on memory
   if(!awsFileMQtt.isEmpty()){
    firmwareUpdate(awsFileMQtt);
  }



}

void loopWM() {
  // checkConfigButton();
}


void checkConfigButton()
{
    if (digitalRead(TRIGGER_PORTAL))
    {
        delay(1000);
        if (digitalRead(TRIGGER_PORTAL))
        {
            // flipper.detach(); // pisca lento se conectado ao wifi
            digitalWrite(LED_BUILTIN, 1);
            wifiManager.resetSettings(); // reset settings?
      
            delay(1000);
            ESP.restart();
        }
    }
}

void saveConfigCallback () {
  // Open json object
  Serial.println("saving config");
  DynamicJsonDocument json(512);
  
  // saving to json
  for (size_t i = 0; i < wifiParams.size(); i++){
    json[wifiParams[i]->getLabel()] = wifiParams[i]->getValue();

    
  }

  File configFile = SPIFFS.open(fileConfig, "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

  // save to file
  Serial.println("fileJsonObj saved on memory: ");
  #if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
      serializeJson(json, Serial);
      serializeJson(json, configFile);
  #else
      json.printTo(Serial);
      json.printTo(configFile);
  #endif
      configFile.close();  

  json.clear();    



}

void firmwareUpdate(String awsFile){
    Serial.println("Formating ...");
    SPIFFS.format();
    // wifiManager.resetSettings(); 
    delay(3000);

    Serial.println("updating from aws s3 file: " + awsFile);
    auto ota_res = execOTA(awsFile);
    while (!ota_res)
    {
      ota_res = execOTA(awsFile);
    }
    
}

#endif