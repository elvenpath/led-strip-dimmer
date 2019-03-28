# ESP8266 LED strip dimmer
A ESP8266 dimmer using a PIR, a 30N06L MOSFET and MQTT


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
  #define SECRET_MQTTSERVER "some.mqtt.host"<br>
  #define SECRET_MQTTPORT 1883<br>
  #define SECRET_MQTTUSER "some.mqtt.user"<br>
  #define SECRET_MQTTPASSWORD "some.mqtt.password"<br>
  #define SECRET_CLIENT_ID  "some.mqtt.client.id"<br>
  const char* secret_wifi_ssid     = "some.wifi.ssid";<br>
  const char* secret_wifi_password = "some.wifi.password";<br>
  const char* command_topic = "led/kitchen1/cmnd";<br>
  const char* status_topic = "led/kitchen1/stat";
