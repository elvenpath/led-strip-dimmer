/*
  inspired from https://github.com/pmansvelder/arduino-sketches/blob/master/led_pwm_dimmer_esp8266/led_pwm_dimmer_esp8266.ino

  Used as a 'smart' LED PWM dimmer for 12V leds
  Interfaces: encoder with button function, connected to pins 0 and WiFi connection to MQTT
  Output 16 is the PWM signal for the MOSFET (30N06L)
  Output 5 is the PIR input which triggers the led tu turn on or off
  The dimmer uses MQTT to listen for commands:
   - command_topic for incoming commands (OFF, ON or brightness)
   - status_topic for state (ON, OFF, brightness) and startup state

  you should create a file called secrets.h containg your credentials:
  ----
  #define SECRET_MQTTSERVER "some.mqtt.host"
  #define SECRET_MQTTPORT 1883
  #define SECRET_MQTTUSER "some.mqtt.user"
  #define SECRET_MQTTPASSWORD "some.mqtt.password"
  #define SECRET_OTAPASSWORD "some.password" 
  const char* secret_wifi_ssid     = "some.wifi.ssid";
  const char* secret_wifi_password = "some.wifi.password";
*/
#include <ESP8266WiFi.h>
#include "PubSubClient.h"
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>

#include "secrets.h"

bool stateOn = false;

#define MAX_BRIGHTNESS 1023 // max pwm value
#define MQTTSERVER SECRET_MQTTSERVER
#define MQTTPORT SECRET_MQTTPORT
#define MQTTUSER SECRET_MQTTUSER
#define MQTTPASSWORD SECRET_MQTTPASSWORD
#define OTAPASSWORD SECRET_OTAPASSWORD
#define SENSORNAME "kitchen_led1"

int OTAport = 8266;

#define USE_PIR true

#define LED_PWM_PIN 4
#define PIR_PIN 16

const char* wifi_ssid = secret_wifi_ssid;
const char* wifi_password = secret_wifi_password;
const int BUFFER_SIZE = JSON_OBJECT_SIZE(10);

const char* command_topic = "led/kitchen1/cmnd";
const char* status_topic = "led/kitchen1/stat";
const char* on_cmd = "ON";
const char* off_cmd = "OFF";

int pirCalibrationTime = 3; //seconds

char messageBuffer[100];
String messageString;
int brightness = 0;
int last_brightness = 0;

WiFiClient espClient;
PubSubClient mqttClient;

void setup() {
  Serial.println("Dimmer starting");

  pinMode(LED_PWM_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT_PULLDOWN_16);

  analogWriteRange(MAX_BRIGHTNESS);
  analogWrite(LED_PWM_PIN, 0);

  Serial.begin(57600);

  wifiConnect();

  delay(1000);

  otaSetup();

  mqttSetup();

  delay(1000);

  mqttConnect();

  warmUpPir();

  Serial.println("Dimmer ready");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiReconnect();
  }

  // check MQTT connection status and reconnect if connection lost
  if (!mqttClient.connected()) {
    Serial.println("MQTT not Connected!");
    delay(1000);
    mqttConnect();
  }

  mqttClient.loop();

  ArduinoOTA.handle();

  handleMovement();
}

void wifiConnect() {
  WiFi.disconnect();

  Serial.println("Connecting to " + String(wifi_ssid));

  WiFi.begin(wifi_ssid, wifi_password);

  for (int i = 0; i < 50; i++)
  {
    if (WiFi.status() != WL_CONNECTED) {
      delay (200);
      Serial.print(".");
      delay(200);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Restarting...");

    ESP.restart();
  }
}

void wifiReconnect() {
  Serial.println("WiFi disconnected");
  delay(1000);
  Serial.println("Connecting to " + String(wifi_ssid));

  WiFi.begin(wifi_ssid, wifi_password);

  for (int i = 0; i < 25; i++)
  {
    if (WiFi.status() != WL_CONNECTED) {
      delay (200);
      Serial.print(".");
      delay(200);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Restarting...");

    ESP.restart();
  }
}

void otaSetup() {
  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname(SENSORNAME);
  ArduinoOTA.setPassword((const char *)OTAPASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("Starting OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
}

void mqttSetup() {
  mqttClient.setClient(espClient);
  mqttClient.setServer(MQTTSERVER, MQTTPORT);
  Serial.println("MQTT client configured");
  mqttClient.setCallback(callback);
}

void mqttConnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.println("Attempting MQTT connection...");

    if (mqttClient.connect(SENSORNAME, MQTTUSER, MQTTPASSWORD)) {
      Serial.println("connected");

      mqttClient.subscribe(command_topic);
    } else {
      Serial.println("Connection failed, rc=" + String(mqttClient.state()));
      Serial.println("Trying again in 5 seconds");
      delay(5000);
    }
  }
}

void callback(char* topic, byte * payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }

  // Conver the incoming byte array to a string
  message[length] = '\0'; // Null terminator used to terminate the char array
  Serial.println(message);

  if (!processJson(message)) {
    return;
  }

  sendState();
}

bool processJson(char* message) {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial.println("parseObject() failed");

    return false;
  }

  if (root.containsKey("state")) {
    Serial.println(stateOn);
    Serial.println(last_brightness);
    if (strcmp(root["state"], on_cmd) == 0 && stateOn == false) { // leds are off, turn them on
      stateOn = true;
      if (root.containsKey("brightness")) {
        brightness = root["brightness"];
        if (last_brightness > MAX_BRIGHTNESS) {
          last_brightness = MAX_BRIGHTNESS;
        }
        last_brightness = brightness;
      } else {
        brightness = last_brightness;
      }

      Serial.println("Setting LED to ON, brightness " + brightness);
      analogWrite(LED_PWM_PIN, brightness);
    } else if (strcmp(root["state"], off_cmd) == 0 && stateOn == true) { // leds are on, turn them off
      stateOn = false;
      if (root.containsKey("brightness")) {
        last_brightness = root["brightness"];
      } else {
        last_brightness = brightness;
      }

      if (last_brightness > MAX_BRIGHTNESS) {
        last_brightness = MAX_BRIGHTNESS;
      }

      brightness = 0;
      Serial.println("Setting LED to OFF");

      analogWrite(LED_PWM_PIN, brightness);
    } else if (root.containsKey("brightness") && stateOn == true) { // leds are on, increase the brightness
      brightness = root["brightness"];

      if (brightness > MAX_BRIGHTNESS) {
        brightness = MAX_BRIGHTNESS;
      }

      last_brightness = brightness;
      analogWrite(LED_PWM_PIN, brightness);
    }

    // no state sent, check if on and update brightness
  } else if (root.containsKey("brightness") && stateOn == true) {
    brightness = root["brightness"];

    if (brightness > MAX_BRIGHTNESS) {
      brightness = MAX_BRIGHTNESS;
    }

    last_brightness = brightness;
    analogWrite(LED_PWM_PIN, brightness);
  }

  return true;
}

void sendState() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (stateOn) ? on_cmd : off_cmd;
  root["brightness"] = brightness;

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  mqttClient.publish(status_topic, buffer, true);
}

void warmUpPir() {
  Serial.print("Calibrating PIR");
  for (int i = 0; i < pirCalibrationTime; i++) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println(".");
}

void handleMovement() {
  if (USE_PIR) {
    long pirState = digitalRead(PIR_PIN);

    if (pirState == HIGH && stateOn == false) {
      Serial.println("Movement detected");
      stateOn = true;
      if (last_brightness > 0) {
        brightness = last_brightness;
      } else {
        brightness = MAX_BRIGHTNESS;
        last_brightness = brightness;
      }
      Serial.println("Setting LED to ON with brightness at " + brightness);

      analogWrite(LED_PWM_PIN, brightness);

      sendState();

      delay(2000);
    }
    else if (pirState == LOW && stateOn == true) {
      Serial.println("Movement stopped");
      stateOn = false;
      if (brightness > 0) {
        last_brightness = brightness;
      } else {
        last_brightness = MAX_BRIGHTNESS;
      }
      Serial.println("Setting LED to OFF");

      analogWrite(LED_PWM_PIN, 0);

      sendState();

      delay(2000);
    }
  }
}
