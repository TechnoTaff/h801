//
// Alternative firmware for H801 5 channel LED dimmer
// based on https://github.com/mertenats/open-home-automation/blob/master/ha_mqtt_rgb_light/ha_mqtt_rgb_light.ino
//
// Tunable WW light mod by Steve Randall 2017

/*
 * EXAMPLE CONFIG FOR HOME ASSISTANT   
   - platform: mqtt
    name: "LoungeBay-RGB"
    state_topic: "001AB148/rgb/light/status"
    command_topic: "001AB148/rgb/light/switch"
    brightness_state_topic: "001AB148/rgb/brightness/status"
    brightness_command_topic: "001AB148/rgb/brightness/set"
    rgb_state_topic: "001AB148/rgb/rgb/status"
    rgb_command_topic: "001AB148/rgb/rgb/set"
    rgb_value_template: "{{ value_json.rgb | join(',') }}"
    qos: 0
    payload_on: "ON"
    payload_off: "OFF"
    optimistic: false
    effect_list: ["none","colorloop", "random"]
    effect_command_topic: "001AB148/rgb/effect/set"

  - platform: mqtt
    name: "LoungeBay-WW"
    state_topic: "001AB148/ww/light/status"
    command_topic: "001AB148/ww/light/switch"
    brightness_state_topic: "001AB148/ww/brightness/status"
    brightness_command_topic: "001AB148/ww/brightness/set"
    color_temp_state_topic : "001AB148/ww/colortemp/status"
    color_temp_command_topic : "001AB148/ww/colortemp/set"
    qos: 0
    payload_on: "ON"
    payload_off: "OFF"
    optimistic: false
 */

#include <string>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>   // Local WebServer used to serve the configuration portal
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>        // WiFi Configuration Magic
#include <PubSubClient.h>       // MQTT client
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#define DEVELOPMENT

WiFiManager wifiManager;
WiFiClient wifiClient;
PubSubClient client(wifiClient);
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

const char* mqtt_server = "192.168.178.29";
const char* mqtt_user = "homeassistant";
const char* mqtt_pass = "homeassistant";
// Password for update server
const char* username = "admin";
const char* password = "homeassistant";

// MQTT topics
// state, brightness, rgb
const char* MQTT_UP = "active";
char* MQTT_LIGHT_RGB_STATE_TOPIC = "XXXXXXXX/rgb/light/status";
char* MQTT_LIGHT_RGB_COMMAND_TOPIC = "XXXXXXXX/rgb/light/switch";
char* MQTT_LIGHT_RGB_BRIGHTNESS_STATE_TOPIC = "XXXXXXXX/rgb/brightness/status";
char* MQTT_LIGHT_RGB_BRIGHTNESS_COMMAND_TOPIC = "XXXXXXXX/rgb/brightness/set";
char* MQTT_LIGHT_RGB_RGB_STATE_TOPIC = "XXXXXXXX/rgb/rgb/status";
char* MQTT_LIGHT_RGB_RGB_COMMAND_TOPIC = "XXXXXXXX/rgb/rgb/set";

char* MQTT_LIGHT_RGB_EFFECT_COMMAND_TOPIC = "XXXXXXXX/rgb/effect/set";
char* MQTT_LIGHT_RGB_STATE_COMMAND_TOPIC = "XXXXXXXX/rgb/effect/status";

char* MQTT_LIGHT_WW_STATE_TOPIC = "XXXXXXXX/ww/light/status";
char* MQTT_LIGHT_WW_COMMAND_TOPIC = "XXXXXXXX/ww/light/switch";
char* MQTT_LIGHT_WW_BRIGHTNESS_COMMAND_TOPIC = "XXXXXXXX/ww/brightness/set";
char* MQTT_LIGHT_WW_BRIGHTNESS_STATE_TOPIC = "XXXXXXXX/ww/brightness/status";
char* MQTT_LIGHT_WW_COLOR_TEMP_STATE_TOPIC =  "XXXXXXXX/ww/colortemp/status";
char* MQTT_LIGHT_WW_COLOR_TEMP_COMMAND_TOPIC =  "XXXXXXXX/ww/colortemp/set";


char* chip_id = "00000000";
char* myhostname = "esp00000000";

// buffer used to send/receive data with MQTT
const uint8_t MSG_BUFFER_SIZE = 20;
char m_msg_buffer[MSG_BUFFER_SIZE];


// Light
// the payload that represents enabled/disabled state, by default
const char* LIGHT_ON = "ON";
const char* LIGHT_OFF = "OFF";


#define RGB_LIGHT_RED_PIN    15
#define RGB_LIGHT_GREEN_PIN  13
#define RGB_LIGHT_BLUE_PIN   12
#define W1_PIN               14
#define W2_PIN               4

#define GREEN_PIN    1
#define RED_PIN      5

// store the state of the rgb light (colors, brightness, ...)
boolean m_rgb_state = false;
uint8_t m_rgb_brightness = 255;
uint8_t m_rgb_red = 255;
uint8_t m_rgb_green = 255;
uint8_t m_rgb_blue = 255;

boolean m_rgb_cycling = false;
uint16_t currentColor = 0;
boolean rgb_cycling_forward = true;

boolean m_ww_state = false;
uint8_t m_ww_brightness = 255;
uint16_t m_ww_colortemp = 327;

unsigned long previousMillis = 0;
unsigned long currentMillis = 0;

void setup()
{
  pinMode(RGB_LIGHT_RED_PIN, OUTPUT);
  pinMode(RGB_LIGHT_GREEN_PIN, OUTPUT);
  pinMode(RGB_LIGHT_BLUE_PIN, OUTPUT);
  setColor(0, 0, 0);
  pinMode(W1_PIN, OUTPUT);
  pinMode(W2_PIN, OUTPUT);
  setWW(0,m_ww_colortemp);

  pinMode(GREEN_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  digitalWrite(RED_PIN, 0);
  digitalWrite(GREEN_PIN, 1);

  sprintf(chip_id, "%08X", ESP.getChipId());
  sprintf(myhostname, "esp%08X", ESP.getChipId());

  // Setup console
  Serial1.begin(115200);
  delay(10);
  Serial1.println();
  Serial1.println("Booting...");
  Serial1.println();

  // reset if necessary
  // wifiManager.resetSettings();

  wifiManager.setTimeout(3600);
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  wifiManager.addParameter(&custom_mqtt_server);
  WiFiManagerParameter custom_password("password", "password for updates", password, 40);
  wifiManager.addParameter(&custom_password);
  WiFiManagerParameter custom_mqtt_user("mqttuser", "mqtt user", mqtt_user, 40);
  wifiManager.addParameter(&custom_mqtt_user);
  WiFiManagerParameter custom_mqtt_pass("mqttpass", "mqtt pass", mqtt_pass, 40);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.setCustomHeadElement(chip_id);
  wifiManager.autoConnect();

  mqtt_server = custom_mqtt_server.getValue();
  mqtt_user = custom_mqtt_user.getValue();
  mqtt_pass = custom_mqtt_pass.getValue();
  password = custom_password.getValue();

  Serial1.println("WiFi connected");
  Serial1.println("IP address: ");
  Serial1.println(WiFi.localIP());

  // init the MQTT connection
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // replace chip ID in channel names
  memcpy(MQTT_LIGHT_RGB_STATE_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_RGB_COMMAND_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_RGB_BRIGHTNESS_STATE_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_RGB_BRIGHTNESS_COMMAND_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_RGB_RGB_STATE_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_RGB_RGB_COMMAND_TOPIC, chip_id, 8);

  memcpy(MQTT_LIGHT_RGB_EFFECT_COMMAND_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_RGB_STATE_COMMAND_TOPIC, chip_id, 8);

  memcpy(MQTT_LIGHT_WW_STATE_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_WW_COMMAND_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_WW_BRIGHTNESS_STATE_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_WW_BRIGHTNESS_COMMAND_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_WW_COLOR_TEMP_STATE_TOPIC, chip_id, 8);
  memcpy(MQTT_LIGHT_WW_COLOR_TEMP_COMMAND_TOPIC, chip_id, 8);

  digitalWrite(RED_PIN, 1);

  // OTA
  // do not start OTA server if no password has been set
  if (password != "") {
    MDNS.begin(myhostname);
    httpUpdater.setup(&httpServer,username,password);
    httpServer.begin();
    MDNS.addService("http", "tcp", 80);
  }
}

// function called to adapt the brightness and the colors of the led
void setColor(uint8_t p_red, uint8_t p_green, uint8_t p_blue) {
  analogWrite(RGB_LIGHT_RED_PIN, map(p_red, 0, 255, 0, m_rgb_brightness));
  analogWrite(RGB_LIGHT_GREEN_PIN, map(p_green, 0, 255, 0, m_rgb_brightness));
  analogWrite(RGB_LIGHT_BLUE_PIN, map(p_blue, 0, 255, 0, m_rgb_brightness));
}


//void setWW(uint8_t brightness) {
//  // convert the brightness in % (0-100%) into a digital value (0-255)
//  analogWrite(W1_PIN, brightness);
//  analogWrite(W2_PIN, brightness);
//}


void setWW(uint8_t brightness, uint16_t mired){
  uint8_t w1val;
  uint8_t w2val;

   // quick and dirty mireds to 0-255 W1/W2 values (1.474 is rough multiplier)
   // MIRED << 154 ----------------------- 327 ----------------------- 500 >>
   // W1    << 255 ----------------------- 255 ----------------------- 0   >>
   // W2    <<   0 ----------------------- 255 ----------------------- 255 >>
   
  if (mired >= 327  && mired <=500 ) {
     // warmer
     w2val = 255;
     w1val = int(255 - float( float( mired - 327 ) * 1.474)) ;
  } else if (mired < 327 && mired >=0) {
    // cooler
    w1val = 255;
    w2val = int(255 - float( float( 327 - mired ) * 1.474)) ;
  }
  // brightness divider
  if (brightness > 0) { 
    w1val = (w1val / float(255/brightness));
    w2val = (w2val / float(255/brightness));
  } else {
    w1val = 0;
    w2val = 0;
  }
  
  analogWrite(W1_PIN, w1val);
  analogWrite(W2_PIN, w2val);

}

// function called to publish the state of the led (on/off)
void publishRGBState() {
  if (m_rgb_state) {
    client.publish(MQTT_LIGHT_RGB_STATE_TOPIC, LIGHT_ON, true);
  } else {
    client.publish(MQTT_LIGHT_RGB_STATE_TOPIC, LIGHT_OFF, true);
  }
}

void publishWWState() {
  if (m_ww_state) {
    client.publish(MQTT_LIGHT_WW_STATE_TOPIC, LIGHT_ON, true);
  } else {
    client.publish(MQTT_LIGHT_WW_STATE_TOPIC, LIGHT_OFF, true);
  }
}

// function called to publish the colors of the led (xx(x),xx(x),xx(x))
void publishRGBColor() {
  snprintf(m_msg_buffer, MSG_BUFFER_SIZE, "%d,%d,%d", m_rgb_red, m_rgb_green, m_rgb_blue);
  client.publish(MQTT_LIGHT_RGB_RGB_STATE_TOPIC, m_msg_buffer, true);
}

// function called to publish the brightness of the led (0-100)
void publishRGBBrightness() {
  snprintf(m_msg_buffer, MSG_BUFFER_SIZE, "%d", m_rgb_brightness);
  client.publish(MQTT_LIGHT_RGB_BRIGHTNESS_STATE_TOPIC, m_msg_buffer, true);
}

void publishWWBrightness() {
  snprintf(m_msg_buffer, MSG_BUFFER_SIZE, "%d", m_ww_brightness);
  client.publish(MQTT_LIGHT_WW_BRIGHTNESS_STATE_TOPIC, m_msg_buffer, true);
}

void publishWWColortemp(){
  snprintf(m_msg_buffer, MSG_BUFFER_SIZE, "%d", m_ww_colortemp);
  client.publish(MQTT_LIGHT_WW_COLOR_TEMP_STATE_TOPIC, m_msg_buffer, true);  
}

void publishEffectState(){
  if (m_rgb_cycling) {
    client.publish(MQTT_LIGHT_RGB_STATE_COMMAND_TOPIC, "colorloop", true);
  } else {
    client.publish(MQTT_LIGHT_RGB_STATE_COMMAND_TOPIC, "colorloop", false);
    client.publish(MQTT_LIGHT_RGB_STATE_COMMAND_TOPIC, "none", true);
  }
  client.publish(MQTT_LIGHT_RGB_STATE_COMMAND_TOPIC, "random", false);
}


// function called when a MQTT message arrived
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
  // concat the payload into a string
  String payload;
  for (uint8_t i = 0; i < p_length; i++) {
    payload.concat((char)p_payload[i]);
  }
  //Serial1.println(String(p_topic));
  //Serial1.println(String(payload));
  
  // handle message topic
  if (String(MQTT_LIGHT_RGB_COMMAND_TOPIC).equals(p_topic)) {
    // test if the payload is equal to "ON" or "OFF"
    if (payload.equals(String(LIGHT_ON))) {
      m_rgb_state = true;
      setColor(m_rgb_red, m_rgb_green, m_rgb_blue);
      publishRGBState();
    } else if (payload.equals(String(LIGHT_OFF))) {
      m_rgb_state = false;
      setColor(0, 0, 0);
      publishRGBState();
    }
  }  else if (String(MQTT_LIGHT_WW_COMMAND_TOPIC).equals(p_topic)) {
    // test if the payload is equal to "ON" or "OFF"
    if (payload.equals(String(LIGHT_ON))) {
      m_ww_state = true;
      setWW(m_ww_brightness,m_ww_colortemp);
      publishWWState();
    } else if (payload.equals(String(LIGHT_OFF))) {
      m_ww_state = false;
      setWW(0,m_ww_colortemp);
      publishWWState();
    }
  }  else if (String(MQTT_LIGHT_RGB_BRIGHTNESS_COMMAND_TOPIC).equals(p_topic)) {
    uint8_t brightness = payload.toInt();
    if (brightness < 0 || brightness > 255) {
      // do nothing...
      return;
    } else {
      m_rgb_brightness = brightness;
      setColor(m_rgb_red, m_rgb_green, m_rgb_blue);
      publishRGBBrightness();
    }
  } else if (String(MQTT_LIGHT_WW_BRIGHTNESS_COMMAND_TOPIC).equals(p_topic)) {
    uint8_t brightness = payload.toInt();
    if (brightness < 0 || brightness > 255) {
      // do nothing...
      return;
    } else {
      m_ww_brightness = brightness;
      setWW(m_ww_brightness, m_ww_colortemp);
      publishWWBrightness();
    }
  } else if (String(MQTT_LIGHT_WW_COLOR_TEMP_COMMAND_TOPIC).equals(p_topic)) {
      uint16_t colortemp = payload.toInt();
      if (colortemp < 154 || colortemp > 500) {
        // do nothing...
        return;
      } else {
        m_ww_colortemp = colortemp;
        setWW(m_ww_brightness, m_ww_colortemp);
        publishWWColortemp();
      }
  } 
  else if (String(MQTT_LIGHT_RGB_EFFECT_COMMAND_TOPIC).equals(p_topic)) {
       if (payload.equals(String("colorloop"))){
          m_rgb_cycling = true;
          publishEffectState();
       } else if (payload.equals(String("random"))) {
          setRandomRGBcolor();
       } else if (payload.equals(String("none"))) {
          m_rgb_cycling = false;
          publishEffectState();
          publishRGBColor();
       }
  }
  else if (String(MQTT_LIGHT_RGB_RGB_COMMAND_TOPIC).equals(p_topic)) {
    // get the position of the first and second commas
    uint8_t firstIndex = payload.indexOf(',');
    uint8_t lastIndex = payload.lastIndexOf(',');
    
    if (m_rgb_cycling) {
      m_rgb_cycling = false;
      publishEffectState();
      previousMillis = millis();
    }
    
    uint8_t rgb_red = payload.substring(0, firstIndex).toInt();
    if (rgb_red < 0 || rgb_red > 255) {
      return;
    } else {
      m_rgb_red = rgb_red;
    }

    uint8_t rgb_green = payload.substring(firstIndex + 1, lastIndex).toInt();
    if (rgb_green < 0 || rgb_green > 255) {
      return;
    } else {
      m_rgb_green = rgb_green;
    }

    uint8_t rgb_blue = payload.substring(lastIndex + 1).toInt();
    if (rgb_blue < 0 || rgb_blue > 255) {
      return;
    } else {
      m_rgb_blue = rgb_blue;
    }

    setColor(m_rgb_red, m_rgb_green, m_rgb_blue);
    publishRGBColor();
  }

  digitalWrite(GREEN_PIN, 0);
  delay(1);
  digitalWrite(GREEN_PIN, 1);
}


// rough function to turn 0-1530 value to colour on the spectrum
void linearToRBGColor(uint16_t linearColor, uint8_t brightness = 255){
  //break down the full range into 6 logical blocks (https://goo.gl/HknkN8)
  uint8_t redBit;
  uint8_t greenBit;
  uint8_t blueBit;
  
  if (linearColor >=0 and  linearColor<=255){
    // slot 1
    blueBit = 0;
    greenBit = (linearColor % 256);
    redBit = 255 - (linearColor % 256);
  } else if (linearColor > 255 and linearColor <= 512){
    // slot 2
    redBit = 0;
    blueBit = (linearColor % 256);
    greenBit = 255 - (linearColor % 256);
  } else if (linearColor > 512 and linearColor <= 765){
    // slot 3
    greenBit = 0;
    redBit = (linearColor % 256);
    blueBit = 255 - (linearColor % 256);
  } else if (linearColor > 765 and linearColor <= 1020){
    // slot 4
    blueBit = 0;
    greenBit = (linearColor % 256);
    redBit = 255 - (linearColor % 256);
  } else if (linearColor > 1020 and linearColor <= 1275){
    // slot 5
    redBit = 0;
    blueBit = (linearColor % 256);
    greenBit = 255 - (linearColor % 256);
  } else if (linearColor > 1275 and linearColor <= 1530){
    // slot 6
    greenBit = 0;
    redBit = (linearColor % 256);
    blueBit = 255 - (linearColor % 256);
  }
  setColor(redBit, greenBit, blueBit);
  m_rgb_red = redBit;
  m_rgb_green = greenBit;
  m_rgb_blue = blueBit;
}

void setRandomRGBcolor(){
  m_rgb_cycling = false;
  publishEffectState();
  linearToRBGColor(int(random(1530)));
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial1.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(chip_id, mqtt_user, mqtt_pass)) {
      Serial1.println("connected");
      // blink 10 times green LED for success connected
      for (int x=0; x < 10; x++){
        delay(100);
        digitalWrite(GREEN_PIN, 0);
        delay(100);
        digitalWrite(GREEN_PIN, 1);
      }
      
      client.publish(MQTT_UP, chip_id);
      // Once connected, publish an announcement...
      // publish the initial values
      publishRGBState();
      publishRGBBrightness();
      publishRGBColor();
      publishWWState();
      publishWWBrightness();
      publishWWColortemp();
      publishEffectState();
      // ... and resubscribe
      client.subscribe(MQTT_LIGHT_RGB_COMMAND_TOPIC);
      client.subscribe(MQTT_LIGHT_RGB_BRIGHTNESS_COMMAND_TOPIC);
      client.subscribe(MQTT_LIGHT_RGB_RGB_COMMAND_TOPIC);
      client.subscribe(MQTT_LIGHT_RGB_EFFECT_COMMAND_TOPIC);
      client.subscribe(MQTT_LIGHT_WW_COMMAND_TOPIC);
      client.subscribe(MQTT_LIGHT_WW_BRIGHTNESS_COMMAND_TOPIC);
      client.subscribe(MQTT_LIGHT_WW_COLOR_TEMP_COMMAND_TOPIC);
    } else {
      Serial1.print("failed, rc=");
      Serial1.print(client.state());
      Serial1.print(mqtt_server);
      Serial1.println(" try again in 5 seconds");
      // Wait about 5 seconds (10 x 500ms) before retrying
      for (int x=0; x < 10; x++){
        delay(400);
        digitalWrite(RED_PIN, 0);
        delay(100);
        digitalWrite(RED_PIN, 1);
      }
    }
  }
}

uint16_t i = 0;

void loop()
{
  // process OTA updates
  httpServer.handleClient();
  
  i++;
  if (!client.connected()) {
    reconnect();
  }
  client.loop();


  // handling cycling effect:

  if (m_rgb_cycling) {
    currentMillis = millis();
    if((unsigned long)(currentMillis - previousMillis) >= 200) {    
      previousMillis = millis();  
      // do colorloop effect here
      if (rgb_cycling_forward) {
        currentColor++;
      } else{
        currentColor--;
      }
      if (currentColor > 1530) {
        rgb_cycling_forward = false;
        currentColor = 1530;
      } else if (currentColor <= 0) {
        rgb_cycling_forward = true;
        currentColor = 1530;
      }
      linearToRBGColor(currentColor);
    }
    
  }

  // Post the full status to MQTT every 65535 cycles. This is roughly once a minute
  // this isn't exact, but it doesn't have to be. Usually, clients will store the value
  // internally. This is only used if a client starts up again and did not receive
  // previous messages
  delay(1);
  if (i == 0) {
    publishRGBState();
    publishRGBBrightness();
    publishRGBColor();
    publishWWState();
    publishWWBrightness();
    publishWWColortemp();
  }
}
