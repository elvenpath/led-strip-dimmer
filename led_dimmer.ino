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
  #define SECRET_CLIENT_ID  "some.mqtt.client.id"
  const char* secret_wifi_ssid     = "some.wifi.ssid";
  const char* secret_wifi_password = "some.wifi.password";
  const char* command_topic = "led/kitchen1/cmnd";
  const char* status_topic = "led/kitchen1/stat";
*/
#include <ESP8266WiFi.h>
#include "PubSubClient.h"
#include "secrets.h"

bool debug = true;
bool retain_mqtt_message = true;

#define MAX_BRIGHTNESS 1023 // max pwm value
#define MQTTSERVER SECRET_MQTTSERVER
#define MQTTPORT SECRET_MQTTPORT
#define MQTTUSER SECRET_MQTTUSER
#define MQTTPASSWORD SECRET_MQTTPASSWORD
#define CLIENT_ID "kitchen_led1_dimmer"

#define LED_PWM_PIN 16
#define PIR_PIN 5

const char* wifi_ssid = secret_wifi_ssid;
const char* wifi_password = secret_wifi_password;

const char* command_topic = "led/kitchen1/cmnd";
const char* status_topic = "led/kitchen1/stat";

char messageBuffer[100];
String messageString;
String ip = "";
int led_power = 0;
int last_led_power = MAX_BRIGHTNESS;

WiFiClient espClient;
PubSubClient mqttClient;

void setup() {
  pinMode(LED_PWM_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  analogWriteRange(MAX_BRIGHTNESS);

  Serial.begin(115200);

  delay(100);
  ShowDebug("Dimmer start");

  analogWrite(LED_PWM_PIN, 0);

  wifiConnect();

  mqttSetup();

  mqttConnect();
}

void loop() {
  if (!mqttClient.connected()) {          // check MQTT connection status and reconnect if connection lost
    ShowDebug("MQTT not Connected!");
  }

  long state = digitalRead(PIR_PIN);
  if (state == HIGH) {
    turnOn();
  }
  else {
    turnOff();
  }


  mqttClient.loop();
}

void wifiConnect()
{
  WiFi.disconnect();

  ShowDebug("Connecting to ");
  ShowDebug(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password);
  // Wait for connection
  for (int i = 0; i < 25; i++)
  {
    if (WiFi.status() != WL_CONNECTED) {
      delay (250);
      Serial.print(".");
      delay(250);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    ESP.restart();
  }

  ShowDebug("WiFi connected");
  ShowDebug("IP address: " + String(WiFi.localIP()));
  ShowDebug("Netmask: " + String(WiFi.subnetMask()));
  ShowDebug("Gateway: " + String(WiFi.gatewayIP()));

  //convert ip Array into String
  ip = String (WiFi.localIP()[0]) + "." + String (WiFi.localIP()[1]) + "." + String (WiFi.localIP()[2]) + "." + String (WiFi.localIP()[3]);
}

void mqttSetup() {
  mqttClient.setClient(espClient);
  mqttClient.setServer(MQTTSERVER, MQTTPORT);
  ShowDebug("MQTT client configured");
  mqttClient.setCallback(callback);
}

void mqttConnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    ShowDebug("Attempting MQTT connection...");

    if (mqttClient.connect(CLIENT_ID, MQTTUSER, MQTTPASSWORD)) {
      ShowDebug("MQTT connected");

      mqttClient.publish(status_topic, ip.c_str());
      mqttClient.publish(status_topic, "ESP8266 connected");

      mqttClient.subscribe(command_topic);
    } else {
      ShowDebug("failed, rc=" + String(mqttClient.state()));
      ShowDebug("trying again in 5 seconds");

      delay(5000);
    }
  }
}

void callback(char* topic, byte * payload, unsigned int length) {
  char msgBuffer[20];
  // I am only using one ascii character as command, so do not need to take an entire word as payload
  // However, if you want to send full word commands, uncomment the next line and use for string comparison
  payload[length] = '\0'; // terminate string with 0
  String strPayload = String((char*)payload);  // convert to string

  ShowDebug(strPayload);
  ShowDebug("Message arrived");
  ShowDebug(topic);
  ShowDebug(strPayload);

  if (strPayload[0] == 'OFF') {
    turnOff();
  }
  else if (strPayload[0] == 'ON') {
    turnOn();
  }
  else if ((strPayload.substring(0).toInt() == 0) and (strPayload != "0")) {
    mqttClient.publish(status_topic, "Unknown Command"); // unknown command
  }
  else {
    int brightness = strPayload.substring(0).toInt();
    changeBrightness(brightness);
  }
}

void turnOff() {
  ShowDebug("Setting LED to OFF");

  last_led_power = led_power;
  led_power = 0;
  analogWrite(LED_PWM_PIN, led_power);

  messageString = '{"Status": "OFF"}';
  messageString.toCharArray(messageBuffer, messageString.length() + 1);
  mqttClient.publish(status_topic, messageBuffer, retain_mqtt_message);
}

void turnOn() {
  ShowDebug("Setting LED to ON");
  led_power = last_led_power;

  messageString = '{"Status": "ON"}';
  messageString.toCharArray(messageBuffer, messageString.length() + 1);
  mqttClient.publish(status_topic, messageBuffer, retain_mqtt_message);

  changeBrightness(led_power);
}

void changeBrightness(int brightness)
{
  if (brightness > MAX_BRIGHTNESS) {
    brightness = MAX_BRIGHTNESS;
  }
  analogWrite(LED_PWM_PIN, brightness);

  messageString = '{"Brightness": ' + String(brightness) + '}';
  messageString.toCharArray(messageBuffer, messageString.length() + 1);
  mqttClient.publish(status_topic, messageBuffer,  retain_mqtt_message);
}

void ShowDebug(String string) {
  if (debug) {
    Serial.println(string);
  }
}
