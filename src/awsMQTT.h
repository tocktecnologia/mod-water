
#ifdef ESP32
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error Platform not supported
#endif
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson (use v6.xx)
#include <time.h>
#define emptyString String()

//Follow instructions from https://github.com/debsahu/ESP-MQTT-AWS-IoT-Core/blob/master/doc/README.md
//Enter values in secrets.h ▼
#include "secrets.h"
#include <Update.h>
#include <HCSR04.h>


#if !(ARDUINOJSON_VERSION_MAJOR == 6 and ARDUINOJSON_VERSION_MINOR >= 7)
#error "Install ArduinoJson v6.7.0-beta or higher"
#endif

String MQTT_TOPIC_UPDATE;
String MQTT_TOPIC_SUB;
String MQTT_TOPIC_PUB;


#ifdef USE_SUMMER_TIME_DST
uint8_t DST = 1;
#else
uint8_t DST = 0;
#endif

WiFiClientSecure client;

#ifdef ESP8266
BearSSL::X509List cert(cacert);
BearSSL::X509List client_crt(client_cert);
BearSSL::PrivateKey key(privkey);
#endif

PubSubClient mqttClient(client);


time_t now;
time_t nowish = 1510592825;
long lastReconnectAttempt = 0;
int intervalRetryMqtt = 4000;
int countWiFiDisconnection = 0;

Ticker timerSendMqtt;
HCSR04 *hc; 
int maxSamples=100;
int countTimesSensor=0;
float countValuesSensor=0.0;
float filteringFactor = 2;

void writeAwsFile(String awsFile)
{   
    // reading
    DynamicJsonDocument jsonDoc(512);
    Serial.println("reading config file: ");
    File configFile = SPIFFS.open(fileConfig, "r");
    if (configFile) {
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);

        auto deserializeError = deserializeJson(jsonDoc, buf.get());
        if ( ! deserializeError ) {
            jsonDoc["aws_file_mqtt"]= awsFile;
            serializeJson(jsonDoc, Serial);
        }
        configFile.close();  
    } 

    // writing  
    configFile = SPIFFS.open(fileConfig, "w");
    if (!configFile) {
        Serial.println("failed to open config file for writing");
    }
    Serial.println("writing config file: ");
    serializeJson(jsonDoc, Serial);
    serializeJson(jsonDoc, configFile);
    configFile.close();  
    jsonDoc.clear();    

}

void NTPConnect(void){
  Serial.print("Setting time using SNTP");
  configTime(TIME_ZONE * 3600, DST * 3600, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < nowish)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

void messageReceived(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  String message;
  for (int i = 0; i < length; i++)
  { 
    message += String((char)payload[i]);
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (String(topic).equals(MQTT_TOPIC_UPDATE) == 1) {
    Serial.println("write name of file on memory: "+ message);
    writeAwsFile(message);
    delay(3000);
    ESP.restart();
  }    

}

void pubSubErr(int8_t MQTTErr)
{
  if (MQTTErr == MQTT_CONNECTION_TIMEOUT)
    Serial.print("Connection tiemout");
  else if (MQTTErr == MQTT_CONNECTION_LOST)
    Serial.print("Connection lost");
  else if (MQTTErr == MQTT_CONNECT_FAILED)
    Serial.print("Connect failed");
  else if (MQTTErr == MQTT_DISCONNECTED)
    Serial.print("Disconnected");
  else if (MQTTErr == MQTT_CONNECTED)
    Serial.print("Connected");
  else if (MQTTErr == MQTT_CONNECT_BAD_PROTOCOL)
    Serial.print("Connect bad protocol");
  else if (MQTTErr == MQTT_CONNECT_BAD_CLIENT_ID)
    Serial.print("Connect bad Client-ID");
  else if (MQTTErr == MQTT_CONNECT_UNAVAILABLE)
    Serial.print("Connect unavailable");
  else if (MQTTErr == MQTT_CONNECT_BAD_CREDENTIALS)
    Serial.print("Connect bad credentials");
  else if (MQTTErr == MQTT_CONNECT_UNAUTHORIZED)
    Serial.print("Connect unauthorized");
}

void sendDistSensor(){
  if(countTimesSensor>=maxSamples){

    auto distAverage = countValuesSensor/maxSamples;  
    auto message = "{\"module\":\"water\",\"disAverage\":" + String(distAverage) + ",\"samples\":"+ String(maxSamples) +"}";
    Serial.println("sending mqtt messag: " + String(message));
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), message.c_str());

    countTimesSensor=0; 
    countValuesSensor=0;

  }
  else {
    auto distCurrent = hc->dist() - String(offsetParam.getValue()).toFloat();
    auto distCurrentAverage = countValuesSensor/countTimesSensor;
    
    // filtering by average distance
    if(distCurrent < filteringFactor * distCurrentAverage){
      countValuesSensor += countTimesSensor++;
      countTimesSensor++;
    }
    
  }

}

boolean reconnectMqtt()
{
    if (WiFi.status() != WL_CONNECTED)
    {
      countWiFiDisconnection++;
      if (countWiFiDisconnection >= 3)
        ESP.restart();
    }
  

    String thingName = esp8266ID();
    String clientId = "TOCK-" + thingName + "-";
    clientId += String(random(0xffff), HEX);

    Serial.print("Try connecting to ");
    Serial.print(AWS_MQTT_ENDPOINT);
    Serial.println(" ...");
    flipper.attach(0.3, flip);


    if (mqttClient.connect(clientId.c_str()))
    {
        Serial.println("connected to mqtt. Broker: " + String(AWS_MQTT_ENDPOINT) + "!");
        mqttClient.publish(MQTT_TOPIC_PUB.c_str(), "{\"msg\":\"connected\"}");
        mqttClient.subscribe(MQTT_TOPIC_UPDATE.c_str());
        mqttClient.subscribe(MQTT_TOPIC_SUB.c_str());      
   
        flipper.attach(1, flip);     
    }

    return mqttClient.connected();
}

void setupMQTT(){

NTPConnect();

#ifdef ESP32
  client.setCACert(AWS_CERT_CA);
  client.setCertificate(AWS_CERT_CRT);
  client.setPrivateKey(AWS_CERT_PRIVATE);
#else
  client.setTrustAnchors(&cert);
  client.setClientRSACert(&client_crt, &key);
#endif

  mqttClient.setServer(AWS_MQTT_ENDPOINT, AWS_MQTT_PORT);
  mqttClient.setCallback(messageReceived);
  lastReconnectAttempt = 0;

  // subscribers
  MQTT_TOPIC_UPDATE =  "tock/" + esp8266ID() + "/update";
  MQTT_TOPIC_SUB = "tock/" + esp8266ID() + "/sub";
  MQTT_TOPIC_PUB = "tock/" + esp8266ID() + "/pub";


  
  hc = new HCSR04(2,22);
  timerSendMqtt.attach(4,sendDistSensor);
  

}

void loopMQTT()
{
 
 if (!mqttClient.connected())
    {
        long now = millis();
        if (now - lastReconnectAttempt > intervalRetryMqtt)
        {
            lastReconnectAttempt = now;
            // Attempt to reconnect
            if (reconnectMqtt())
            {
                lastReconnectAttempt = 0;
            }
        }
    }
    else
    { 
        mqttClient.loop();
    }



}

