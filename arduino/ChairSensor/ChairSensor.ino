/* 
  MQTT Binary Sensor - Occupancy sensor for Home-Assistant - NodeMCU (ESP8266)  
  
  Libraries :
    - ESP8266 core for Arduino :  https://github.com/esp8266/Arduino
    - PubSubClient:               https://github.com/knolleary/pubsubclient
    - WiFiManager:                https://github.com/tzapu/WiFiManager
*/

#include <ESP8266WiFi.h>    // https://github.com/esp8266/Arduino
#include <WiFiManager.h>    // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>   // https://github.com/knolleary/pubsubclient/releases/tag/v2.6
#include <Ticker.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>

#define               DEBUG                       // enable debugging
#define               STRUCT_CHAR_ARRAY_SIZE 24   // size of the arrays for MQTT username, password, etc.

// macros for debugging
#ifdef DEBUG
  #define             DEBUG_PRINT(x)    Serial.print(x)
  #define             DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define             DEBUG_PRINT(x)
  #define             DEBUG_PRINTLN(x)
#endif

// Pins used by the occupancy sensor and the push button
const PROGMEM uint8_t OCCUPANCY_SENSOR_PIN = D5;
const PROGMEM uint8_t BUILTINLED_PIN  = BUILTIN_LED;

// MQTT ID and topics
char                  MQTT_CLIENT_ID[7]                                           = {0};
char                  MQTT_BINARY_SENSOR_OCCUPANCY_STATE_TOPIC[STRUCT_CHAR_ARRAY_SIZE] = {0};
const char*           MQTT_ON_PAYLOAD                                             = "OFF";
const char*           MQTT_OFF_PAYLOAD                                            = "ON";

// MQTT settings
typedef struct {
  char                mqttUser[STRUCT_CHAR_ARRAY_SIZE]      = {0};
  char                mqttPassword[STRUCT_CHAR_ARRAY_SIZE]  = {0};
  char                mqttServer[STRUCT_CHAR_ARRAY_SIZE]    = {0};
  char                mqttPort[5]                           = {0};
} Settings;

uint8_t               occupancyState             = LOW;
uint8_t               currentOccupancyState      = occupancyState;

enum CMD {
  CMD_NOT_DEFINED,
  CMD_OCCUPANCY_STATE_CHANGED
};
volatile uint8_t cmd = CMD_NOT_DEFINED;

Settings      settings;
Ticker        ticker;
WiFiClient    wifiClient;
PubSubClient  mqttClient(wifiClient);


///////////////////////////////////////////////////////////////////////////
//   MQTT
///////////////////////////////////////////////////////////////////////////

/*
  Function called to publish the state of the occupancy sensor
*/
void publishOccupancyState() {
  if (occupancyState == HIGH) {
    if (mqttClient.publish(MQTT_BINARY_SENSOR_OCCUPANCY_STATE_TOPIC, MQTT_ON_PAYLOAD, true)) {
      DEBUG_PRINT(F("INFO: MQTT message publish succeeded. Topic: "));
      DEBUG_PRINT(MQTT_BINARY_SENSOR_OCCUPANCY_STATE_TOPIC);
      DEBUG_PRINT(F(". Payload: "));
      DEBUG_PRINTLN(MQTT_ON_PAYLOAD);
    } else {
      DEBUG_PRINTLN(F("ERROR: MQTT message publish failed, either connection lost, or message too large"));
    }
  } else {
    if (mqttClient.publish(MQTT_BINARY_SENSOR_OCCUPANCY_STATE_TOPIC, MQTT_OFF_PAYLOAD, true)) {
      DEBUG_PRINT(F("INFO: MQTT message publish succeeded. Topic: "));
      DEBUG_PRINT(MQTT_BINARY_SENSOR_OCCUPANCY_STATE_TOPIC);
      DEBUG_PRINT(F(". Payload: "));
      DEBUG_PRINTLN(MQTT_OFF_PAYLOAD);
    } else {
      DEBUG_PRINTLN(F("ERROR: MQTT message publish failed, either connection lost, or message too large"));
    }
  }
}

/*
  Function called to connect/reconnect to the MQTT broker
*/
void reconnect() {
  uint8_t i = 0;
  while (!mqttClient.connected()) {
    if (mqttClient.connect(MQTT_CLIENT_ID, settings.mqttUser, settings.mqttPassword)) {
      DEBUG_PRINTLN(F("INFO: The client is successfully connected to the MQTT broker"));
    } else {
      DEBUG_PRINTLN(F("ERROR: The connection to the MQTT broker failed"));
      DEBUG_PRINT(F("Username: "));
      DEBUG_PRINTLN(settings.mqttUser);
      DEBUG_PRINT(F("Password: "));
      DEBUG_PRINTLN(settings.mqttPassword);
      DEBUG_PRINT(F("Broker: "));
      DEBUG_PRINTLN(settings.mqttServer);
      delay(1000);
      if (i == 3) {
        reset();
      }
      i++;
    }
  }
}

///////////////////////////////////////////////////////////////////////////
//   WiFiManager
///////////////////////////////////////////////////////////////////////////
/*
  Function called to toggle the state of the LED
*/
void tick() {
  digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
}

// flag for saving data
bool shouldSaveConfig = false;

// callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}

void configModeCallback (WiFiManager *myWiFiManager) {
  ticker.attach(0.2, tick);
}

///////////////////////////////////////////////////////////////////////////
//   ISR
///////////////////////////////////////////////////////////////////////////
/*
  Function called when the occupancy is opened/closed
*/
void occupancyStateChangedISR() {
  cmd = CMD_OCCUPANCY_STATE_CHANGED;
}

///////////////////////////////////////////////////////////////////////////
//   ESP
///////////////////////////////////////////////////////////////////////////
/*
  Function called to restart the switch
*/
void restart() {
  DEBUG_PRINTLN(F("INFO: Restart..."));
  ESP.reset();
  delay(1000);
}

/*
  Function called to reset the configuration of the switch
*/
void reset() {
  DEBUG_PRINTLN(F("INFO: Reset..."));
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
  delay(1000);
}

///////////////////////////////////////////////////////////////////////////
//   Setup() and loop()
///////////////////////////////////////////////////////////////////////////
void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif
  pinMode(OCCUPANCY_SENSOR_PIN,  INPUT_PULLUP);
  pinMode(BUILTIN_LED,      OUTPUT);

  attachInterrupt(digitalPinToInterrupt(OCCUPANCY_SENSOR_PIN), occupancyStateChangedISR,    CHANGE);  

  ticker.attach(0.6, tick);

  sprintf(MQTT_CLIENT_ID, "%06X", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT client ID/Hostname: "));
  DEBUG_PRINTLN(MQTT_CLIENT_ID);

  sprintf(MQTT_BINARY_SENSOR_OCCUPANCY_STATE_TOPIC, "%06X/binary_sensor/occupancy/state", ESP.getChipId());
  DEBUG_PRINT(F("INFO: MQTT command topic: "));
  DEBUG_PRINTLN(MQTT_BINARY_SENSOR_OCCUPANCY_STATE_TOPIC);

  // load custom params
  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

  WiFiManagerParameter custom_mqtt_user("mqtt-user", "MQTT User", settings.mqttUser, STRUCT_CHAR_ARRAY_SIZE);
  WiFiManagerParameter custom_mqtt_password("mqtt-password", "MQTT Password", settings.mqttPassword, STRUCT_CHAR_ARRAY_SIZE, "type = \"password\"");
  WiFiManagerParameter custom_mqtt_server("mqtt-server", "MQTT Broker IP", settings.mqttServer, STRUCT_CHAR_ARRAY_SIZE);
  WiFiManagerParameter custom_mqtt_port("mqtt-port", "MQTT Broker Port", settings.mqttPort, 5);

  WiFiManager wifiManager;
  
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(180);
  // set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect(MQTT_CLIENT_ID)) {
    ESP.reset();
    delay(1000);
  }

  if (shouldSaveConfig) {
    strcpy(settings.mqttServer,   custom_mqtt_server.getValue());
    strcpy(settings.mqttPort,     custom_mqtt_port.getValue());
    strcpy(settings.mqttUser,     custom_mqtt_user.getValue());
    strcpy(settings.mqttPassword, custom_mqtt_password.getValue());

    EEPROM.begin(512);
    EEPROM.put(0, settings);
    EEPROM.end();
  }

  // configure MQTT
  mqttClient.setServer(settings.mqttServer, atoi(settings.mqttPort));

  // connect to the MQTT broker
  reconnect();

  ArduinoOTA.setHostname(MQTT_CLIENT_ID);
  ArduinoOTA.begin();

  ticker.detach();

  occupancyState = digitalRead(OCCUPANCY_SENSOR_PIN);
  digitalWrite(BUILTIN_LED, HIGH);

  publishOccupancyState();
}

void loop() {
  ArduinoOTA.handle();

  yield();

  // keep the MQTT client connected to the broker
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();

  yield();

  switch (cmd) {
    case CMD_NOT_DEFINED:
      // do nothing ...
      break;
    case CMD_OCCUPANCY_STATE_CHANGED:
      currentOccupancyState = digitalRead(OCCUPANCY_SENSOR_PIN);
      if (occupancyState != currentOccupancyState) {
        if (currentOccupancyState == HIGH) {
          DEBUG_PRINTLN(F("INFO: Occupancy opened"));
        } else {
          DEBUG_PRINTLN(F("INFO: Occupancy closed"));
        }
        occupancyState = currentOccupancyState;
      }
      publishOccupancyState();
      cmd = CMD_NOT_DEFINED;
      break;    
  }
}
