//
// #include "TockMQTT.h"
// #include "otaUpdatePubSub.h"
#define LED_BUILTIN 2 //GPIO 2
#define TRIGGER_PORTAL 3

#define fileConfig "/configs.json"
#define fileStates "/states.json"

#include "utils.h"
#include "awsOTA.h"
#include "TockWiFiManager.h"
#include "awsMQTT.h"

void setup(){
  Serial.begin(9600);
  Serial.println("Setup Started!");
  pinMode(LED_BUILTIN, OUTPUT);
  flipper.attach(0.1, flip); // pisca rapido enquanto tenta se conectar ao wifi


  setupWM();
  setupMQTT();
  
}

void loop(){

  loopWM();
  loopMQTT();

}

