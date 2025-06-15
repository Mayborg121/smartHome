#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <DHT.h>
#include <Adafruit_NeoPixel.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <Preferences.h>
#include <map>
#include <math.h>





// DHT Pins
#define DHT22_PIN       4
#define DHTTYPE DHT22
DHT dht(DHT22_PIN, DHTTYPE);

//Internal LED pin
#define LED_PIN         2

// Relay Pins
#define RELAY_1         16
#define RELAY_2         17
#define RELAY_3         18
#define RELAY_4         19


// PWM Pins 
#define FAN_PWM_PIN 12
#define PWM_CHANNEL 0
#define PWM_FREQ 50        // 50hz PWM frequency
#define PWM_RESOLUTION 8      // 8-bit resolution (0–255)


// Button Pins
#define BUTTON_1        21
#define BUTTON_2        22
#define BUTTON_3        23
#define BUTTON_4        25

// Debounce variables
volatile unsigned long lastDebounceTime1 = 0;
volatile unsigned long lastDebounceTime2 = 0;
volatile unsigned long lastDebounceTime3 = 0;
volatile unsigned long lastDebounceTime4 = 0;
const unsigned long debounceDelay = 200; // in milliseconds

//NeoPixel data pin
#define NEOPIXEL_PIN     5             
#define DEFAULT_LEDS     8   // Safe default if not known


//------------------- Client MGMT --------------------

AsyncWebSocketClient* activeClient = nullptr;

//------------------- Sending Messages ---------------


// Function to send a JSON message to a WebSocket client
void sendJsonResponse(AsyncWebSocketClient *client, const String &type, const String &key, const String &value) {
  StaticJsonDocument<256> response;
  response["Type"] = type;
  response["Key"] = key;
  response["Value"] = value;

  String out;
  serializeJson(response, out);
  client->text(out);
  Serial.printf("➡️ Replied: %s\n", out.c_str());
}


//------------------- DHT Setting --------------------
  static unsigned long lastRead = 0;
  static const unsigned long interval = 2000;
  static String temperatureStr = "0.00";
  static String humidityStr = "0.00";

    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    bool autoAmbience = false;

void readAndLogDHT(AsyncWebSocketClient *client) {
  unsigned long now = millis();
  temp = dht.readTemperature();
  hum = dht.readHumidity();
  if (now - lastRead >= interval) {
    lastRead = now;

    if (!isnan(temp) && !isnan(hum)) {
      temperatureStr = String(temp, 2); // 2 decimal places
      humidityStr = String(hum, 2);     // 2 decimal places
      //sendJsonResponse(client,"temp",temperatureStr,humidityStr);

      StaticJsonDocument<256> response;
      response["Type"] = "temp";
      response["Key"] = temperatureStr;
      response["Value"] = humidityStr;

      String out;
      serializeJson(response, out);
      client->text(out);
      Serial.printf("➡️ Replied: %s\n", out.c_str());

    } else {
      Serial.println("⚠️ Failed to read from DHT22.");
    }
  }
}

//------------------- Air Conditioning ---------------
// Function to set fan speed (0–100%)
void setFanSpeed(int percent) {
  percent = constrain(percent, 0, 100);

  if (percent > 0 && percent < 30) {
    // Kick start the motor
    ledcWrite(0, 255);
    delay(200);
  }

  int duty = map(percent, 0, 100, 0, 255);
  ledcWrite(0, duty);
}
bool autoFanSpeed = true;
unsigned long lastAutoSpeedUpdate = 0;
const unsigned long autoSpeedInterval = 1000;


// ------------------ Color Setting ------------------
Adafruit_NeoPixel strip(DEFAULT_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

bool breathingEnabled = false;
bool cyclesEnabled = false;
uint32_t currentColor = strip.Color(255, 0, 0);

//breathe effect
void updateBreathingEffect() {
  static unsigned long startTime = 0;
  if (startTime == 0) startTime = millis();

  const float minBrightness = 0.05;  // 5%
  const float maxBrightness = 1.0;   // 100%
  const float period = 5000.0;       // 5 seconds per full breath

  unsigned long now = millis();
  float elapsed = (now - startTime) % (unsigned long)period;
  float phase = (elapsed / period) * 2 * PI;

  float sineValue = (sin(phase) + 1.0) / 2.0;
  float brightnessFraction = minBrightness + sineValue * (maxBrightness - minBrightness);
  int brightness = int(brightnessFraction * 255);

  strip.setBrightness(brightness);

  // Always use the saved color, not getPixelColor()
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, currentColor);
  }
  strip.show();
}


//colour cycle effect
void updateColorCycle() {
  static unsigned long lastUpdate = 0;
  static uint16_t hue = 0;
  const uint16_t hueMax = 65535;
  const uint16_t hueStep = 40;       // Faster step per update
  const unsigned long interval = 4;  // Faster updates (every 4 ms)

  unsigned long now = millis();
  if (now - lastUpdate >= interval) {
    lastUpdate = now;

    uint32_t color = strip.gamma32(strip.ColorHSV(hue));
    for (int i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, color);
    }
    strip.show();

    hue += hueStep;
    if (hue >= hueMax) hue = 0;
  }
}


bool isValidHex(const String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = tolower(s.charAt(i));
    if (!isHexadecimalDigit(c)) return false;
  }
  return true;
}

void setColorFromString(const String& rgbStr) {
  int r, g, b;
  if (sscanf(rgbStr.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
    for (int i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show(); // Applies both color and current brightness setting
    currentColor = strip.getPixelColor(0);
  } else {
    Serial.println("Invalid RGB string format");
  }
}

void setBrightnessFromString(const String& brightnessStr) {
  int percent = brightnessStr.toInt();
  percent = constrain(percent, 1, 100); // Clamp safely between 1–100

  int brightnessValue = map(percent, 1, 100, 2, 255); // Avoid 0 to prevent full black
  strip.setBrightness(brightnessValue);

  // Re-apply current pixel colors at new brightness
  for (int i = 0; i < strip.numPixels(); i++) {
    uint32_t color = strip.getPixelColor(i); // Get current color
    strip.setPixelColor(i, color);           // Re-set it (to re-apply brightness)
  }

  strip.show();
}

void setStripColor(String input) {
  input.trim();
  input.toLowerCase();
  if (input.startsWith("#")) input.remove(0, 1);

  // If 6-digit HEX
  if (input.length() == 6 && isValidHex(input)) {
    long hexColor = strtol(input.c_str(), NULL, 16);
    uint8_t r = (hexColor >> 16) & 0xFF;
    uint8_t g = (hexColor >> 8) & 0xFF;
    uint8_t b = hexColor & 0xFF;

    for (uint16_t i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
    return;
  }

  // Handle named modes
  if (input == "white") {
    breathingEnabled = false;
    cyclesEnabled = false;
    setColorFromString("255,255,255");
  } else if (input == "breathing") {
      breathingEnabled = true;
      cyclesEnabled = false;
  } else if (input == "cycle") {
      breathingEnabled = false;
      cyclesEnabled = true;
  } else if (input == "warm") {
    breathingEnabled = false;
    cyclesEnabled = false;
    setColorFromString("255, 170, 80");
  } else if (input == "cool") {
    breathingEnabled = false;
    cyclesEnabled = false;
    setColorFromString("180, 225, 255");
  } else {

    Serial.printf("Colour set from slider: '%s'\n", input.c_str());
  }
}

// ------------------ NeoPixel Setup ------------------

void setupNeoPixel() {
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  strip.setBrightness(255);
  setColorFromString("145,26,220");// Warm yellowish daylight color
}



//------------------------ Client MGMT --------------------------------

//Mapping per client challenges
std::map<uint32_t, String> clientChallenges;
void printClientChallenges() {
  Serial.println("---- Client Challenges ----");
  for (auto const& pair : clientChallenges) {
    Serial.print("Client ID: ");
    Serial.print(pair.first);          // The uint32_t client ID
    Serial.print(" -> Challenge: ");
    Serial.println(pair.second);       // The associated challenge (String)
  }
  Serial.println("---------------------------");
}


//------------------------ sha256 helper ------------------------------
String sha256(const String& input) {
  byte hash[32];
  mbedtls_sha256((const unsigned char*)input.c_str(), input.length(), hash, 0);

  char hex[65];
  for (int i = 0; i < 32; ++i) {
    sprintf(hex + i * 2, "%02x", hash[i]);
  }
  hex[64] = '\0';
  return String(hex);
}


//------------------------- Challenge Generation -----------------------
//genrates 12char challenge
String generateChallenge(uint8_t length = 12) {
  const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  const size_t charsetLength = strlen(charset);
  String result = "";
  for (uint8_t i = 0; i < length; i++) {
    uint8_t index = random(0, charsetLength);
    result += charset[index];
  }
  return result;
}


String challenge;
String Lpassword = "00000000";
//chalenge & hash stored 

//------------------------- UUID MGMT ----------------------------------
Preferences prefs;
const int MAX_USERS = 5;

void beginPrefs() {
  prefs.begin("uuids", false); // read/write
}

void endPrefs() {
  prefs.end();
}

String getUUIDOrder() {
  return prefs.getString("order", "");
}

void setUUIDOrder(const String& order) {
  prefs.putString("order", order);
}

void removeOldestUUID(String& order) {
  int commaIndex = order.indexOf(',');
  String oldestEntry;
  if (commaIndex == -1) {
    oldestEntry = order;
    order = "";
  } else {
    oldestEntry = order.substring(0, commaIndex);
    order = order.substring(commaIndex + 1);
  }

  // Extract uuid from "uuid:username"
  int colonIndex = oldestEntry.indexOf(':');
  if (colonIndex != -1) {
    String uuid = oldestEntry.substring(0, colonIndex);
    prefs.remove(uuid.c_str());
  }
}

void addUUIDUsername(const String& uuid, const String& username) {
  beginPrefs();
  String order = getUUIDOrder();

  // Remove old entry for same UUID if it exists
  int start = 0;
  int commaIndex;
  String updatedOrder = "";
  bool exists = false;

  while (start < order.length()) {
    commaIndex = order.indexOf(',', start);
    String pair = (commaIndex == -1) ? order.substring(start) : order.substring(start, commaIndex);
    start = (commaIndex == -1) ? order.length() : commaIndex + 1;

    int colonIndex = pair.indexOf(':');
    if (colonIndex != -1) {
      String existingUUID = pair.substring(0, colonIndex);
      if (existingUUID == uuid) {
        exists = true; // skip this one, we’ll replace it below
        continue;
      }
    }

    if (updatedOrder.length() > 0) updatedOrder += ",";
    updatedOrder += pair;
  }

  // Enforce MAX_USERS
  int count = 0;
  for (int i = 0; i < updatedOrder.length(); i++) {
    if (updatedOrder.charAt(i) == ',') count++;
  }
  count++; // count entries

  if (count >= MAX_USERS) {
    removeOldestUUID(updatedOrder);
  }

  // Append new UUID:username
  if (updatedOrder.length() > 0) updatedOrder += ",";
  updatedOrder += uuid + ":" + username;

  prefs.putString(uuid.c_str(), username);
  setUUIDOrder(updatedOrder);
  endPrefs();
}

String getUsernameByUUID(const String& uuid) {
  beginPrefs();
  String username = prefs.getString(uuid.c_str(), "");
  endPrefs();
  return username;
}

bool isUUIDKnown(const String& uuid) {
  return getUsernameByUUID(uuid) != "";
}

String generateUUID16() {
  const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  String uuid = "";
  for (int i = 0; i < 16; i++) {
    uint8_t index = random(0, sizeof(charset) - 1);
    uuid += charset[index];
  }
  return uuid;
}

void verifyUUID(AsyncWebSocketClient *client, String UUIDhash) {
  beginPrefs();
  String order = getUUIDOrder(); // UUID:username pairs
  int start = 0;
  int commaIndex;

  // Get the challenge specific to this client:
  String challenge = "";
  auto it = clientChallenges.find(client->id());
  if (it != clientChallenges.end()) {
    challenge = it->second;
  } else {
    Serial.println("❌ Challenge not found for client.");
    endPrefs();
    return;
  }
  while (start < order.length()) {
    commaIndex = order.indexOf(',', start);
    String pair = (commaIndex == -1) ? order.substring(start) : order.substring(start, commaIndex);
    start = (commaIndex == -1) ? order.length() : commaIndex + 1;

    int colonIndex = pair.indexOf(':');
    if (colonIndex == -1) continue; // skip malformed

    String uuid = pair.substring(0, colonIndex);
    String username = pair.substring(colonIndex + 1);

    String p = sha256(uuid + challenge);  // Use client-specific challenge here
    Serial.println("Comparing:");
    Serial.printf("  Hash of UUID + challenge: %s\n", p.c_str());
    Serial.printf("  Received UUIDhash: %s\n", UUIDhash.c_str());

    if (p.length() > 0 && UUIDhash.length() > 0 && strcmp(p.c_str(), UUIDhash.c_str()) == 0) {
      Serial.printf("✅ Match found! UUID: %s | Username: %s\n", uuid.c_str(), username.c_str());
      activeClient = client;
      sendJsonResponse(client, "Verified", username, "Verified Successfully");
      endPrefs();
      return;
    }
  }
  Serial.println("❌ No matching UUID found.");
  endPrefs();
}






//------------------ Hosting Webserver and Websockets -----------------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");


//------------------- User Authentication Process ---------------------
void onUserAuthenticated(AsyncWebSocketClient *client,const String& username) {

  String uuid = generateUUID16(); // 16 char UUID generator
  addUUIDUsername(uuid, username);
  activeClient = client;
  Serial.printf("Saved UUID %s for user %s\n", uuid.c_str(), username.c_str());
  
  StaticJsonDocument<256> response;
    response["Type"] = "Authenticated";
    response["Key"] = uuid;
    response["Value"] = "User" + username + "Autenticated Successfully !";

    sendJsonResponse(client,"Authenticated",uuid,username);
}


//--------------------- Message Handling ------------------------------
void handleWebSocketMessage(AsyncWebSocketClient *client, void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;

  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    // Safely convert byte array to String
    String msg = "";
    for (size_t i = 0; i < len; i++) {
      msg += (char)data[i];
    }

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, msg);

    if (err) {
      Serial.println("❌ JSON parse error.");
      return;
    }

    String type = doc["Type"] | "";
    String key = doc["Key"] | "";
    String value = doc["Value"] | "";

    if (type == "Auth") {
      String hash1 = value;
      String hash2 = sha256(Lpassword + challenge); // hash of (password + challenge)
      if(key == "UUID"){
        //make client = active user if UUID hash MAtches
        Serial.printf(value.c_str());
        verifyUUID(client,value);
      } else if (hash1 == hash2) {
        onUserAuthenticated(client, key);  // key = username
      } else {
        sendJsonResponse(client, "NotAuthenticated", "", "Incorrect Password");
        Serial.printf("Stored Hash: %s",hash2 );
      }
    } else if (type == "Control" && client==activeClient) {
      // control logic
      if (key == "T.V.") {
        if (value == "on") {
          digitalWrite(RELAY_3, HIGH);
        } else if (value == "off") {
          digitalWrite(RELAY_3, LOW);
        } else {
          digitalWrite(RELAY_3, !digitalRead(RELAY_3)); // Toggle
        }
      }
      else if (key == "Desktop") {
        if (value == "on") {
          digitalWrite(RELAY_4, HIGH);
        } else if (value == "off") {
          digitalWrite(RELAY_4, LOW);
        } else {
          digitalWrite(RELAY_4, !digitalRead(RELAY_4)); // Toggle
        }
      }
      else if (key == "Light-1") {
        if (value == "on") {
          digitalWrite(RELAY_1, HIGH);
          digitalWrite(LED_PIN, HIGH);
        } else if (value == "off") {
          digitalWrite(RELAY_1, LOW);
          digitalWrite(LED_PIN, LOW);
        } else {
          digitalWrite(RELAY_1, !digitalRead(RELAY_1)); // Toggle
          digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Toggle LED
        }
      }
      else if (key == "Light-2") {
        if (value == "on") {
          digitalWrite(RELAY_2, HIGH);
        } else if (value == "off") {
          digitalWrite(RELAY_2, LOW);
        } else {
          digitalWrite(RELAY_2, !digitalRead(RELAY_2)); // Toggle
        }
      } else if(key == "AirCondition"){
        //airCondition(value);
        autoFanSpeed = false;
        if (value == "hot") {
          setFanSpeed(20);
        } else if (value == "cool") {
          setFanSpeed(70);
        } else if (value == "cold") {
          setFanSpeed(100);
        } else if (value == "normal") {
          setFanSpeed(45);
        } else{
          int speed = value.toInt();
          setFanSpeed(speed);
        }

      } else if(key == "Ambience"){
        //ambienceSet(value);

        if(value == "on"){
          autoAmbience = true;
        }else{
          autoAmbience = false;
          setStripColor(value);
        }
      } else if(key == "colour"){
        //neoPixelSetColour(value);
        autoAmbience = false;
        breathingEnabled = false;
        cyclesEnabled = false;
        setColorFromString(value);
      } else if(key == "brightness"){
        //neoPixelSetbrightness(value);
        setBrightnessFromString(value);
      } else if(key == "Air Condition"){
        if(value == "on"){
          autoFanSpeed = true;
        } else{
          autoFanSpeed = false;
        }
      } else{
        Serial.println("Wrong Control Instruction");
      }
    } else if (type == "UpdateWifi" && client==activeClient){
      prefs.begin("wifiCreds", false);
      prefs.putString("ssid", key);
      prefs.putString("password", value);
      prefs.end();
      delay(1000);
      ESP.restart();// restarting esp AFTER changing wifi credentials

    } else if (type == "UpdatePassword" && client==activeClient){
      prefs.begin("LoginCreds", false);
      prefs.putString("Loginpassword", key);
      prefs.end();

      Serial.println("Storing SSID: " + key);
      Serial.println("Storing Password: " + value);
    } else {
      // handle other types or print No data 
      Serial.printf("UnAuthorized Instruction");
    }
  }
}


void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:{
      Serial.println("✅ WebSocket client connected");
      String Newchallenge = generateChallenge();
      challenge = Newchallenge;              // Generate a new one
      clientChallenges[client->id()] = challenge;          // Store per client
      Serial.printf("Challenge for client %u: %s\n", client->id(), challenge.c_str());
      sendJsonResponse(client, "Challenge", challenge.c_str(), "Challenged");
      printClientChallenges(); // prints the previously connected clients.
      break;
    }
    case WS_EVT_DISCONNECT:
      Serial.println("❎ WebSocket client disconnected");
      clientChallenges.erase(client->id());
      break;
    case WS_EVT_DATA: {
      AwsFrameInfo* info = (AwsFrameInfo*)arg;

      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        // Create a proper string from raw bytes
      String msg = "";
      for (size_t i = 0; i < len; i++) {
      msg += (char)data[i];
      }

      Serial.printf("➡️ Recieved: %s\n", msg.c_str());

      handleWebSocketMessage(client, arg, data, len);  // or pass `msg`
      }
      break;
      }
    default:
      break;
  }
}




const char* ap_ssid  = "SmartHome-ESP32";
const char* ap_pass  = "00000000";

bool connectedToWiFi = false;

void setupAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("[AP] AP IP address: ");
  Serial.println(IP);
}

void connectToWiFi() {

  prefs.begin("wifiCreds", true);
  String ssid = prefs.getString("ssid", "Mayur'sDevice");
  String password = prefs.getString("password", "00000001");
  prefs.end();
  Serial.println("SSID: " + ssid);
  Serial.println("Password: " + password);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("Smart-Home");
  WiFi.disconnect(true);  // Clear old WiFi credentials
  delay(100);
  WiFi.begin(ssid, password);

  Serial.print("[WIFI] Connecting");
  for (int i = 0; i < 4; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      connectedToWiFi = true;
      break;
    }
    delay(1000);
    Serial.print(".");
  }

  if (connectedToWiFi) {
    Serial.println("\n[WIFI] Connected to Wi-Fi! :"+ssid);
    Serial.print("[WIFI] IP Address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_PIN, HIGH);
    setColorFromString("144,238,144");
  } else {
    Serial.println("\n[WIFI] Failed to connect. Switching to AP mode...");
    setupAccessPoint();
    digitalWrite(LED_PIN, LOW);
  }
}

void setupMDNS() {
  if (MDNS.begin("home")) {
    Serial.println("[mDNS] home.local is ready");
  } else {
    Serial.println("[mDNS] Error starting mDNS");
  }
}



void IRAM_ATTR button1_ISR() {
  unsigned long now = millis();
  if (now - lastDebounceTime1 > debounceDelay) {
    lastDebounceTime1 = now;
    digitalWrite(RELAY_1, !digitalRead(RELAY_1));
  }
}

void IRAM_ATTR button2_ISR() {
  unsigned long now = millis();
  if (now - lastDebounceTime2 > debounceDelay) {
    lastDebounceTime2 = now;
    digitalWrite(RELAY_2, !digitalRead(RELAY_2));
  }
}

void IRAM_ATTR button3_ISR() {
  unsigned long now = millis();
  if (now - lastDebounceTime3 > debounceDelay) {
    lastDebounceTime3 = now;
    digitalWrite(RELAY_3, !digitalRead(RELAY_3));
  }
}

void IRAM_ATTR button4_ISR() {
  unsigned long now = millis();
  if (now - lastDebounceTime4 > debounceDelay) {
    lastDebounceTime4 = now;
    digitalWrite(RELAY_4, !digitalRead(RELAY_4));
  }
}



void sendDeviceInfo(AsyncWebSocketClient* client) {

  size_t freeHeap = ESP.getFreeHeap();
  size_t totalHeap = ESP.getHeapSize();

  String json = "{";
  json += "\"Type\":\"info\",";
  json += "\"device\":\"ESP32\",";
  json += "\"ram\":\"" + String(ESP.getFreeHeap() / 1024) + "KB / " + String(ESP.getHeapSize() / 1024) + "KB (free/total)\",";
  json += "\"cpu\":" + String(getCpuFrequencyMhz()) + ",";
  json += "\"fs_total\":" + String(LittleFS.totalBytes() / 1024) + ",";
  json += "\"fs_used\":" + String(LittleFS.usedBytes() / 1024) + ",";
  json += "\"esp_ip\":\"" + WiFi.localIP().toString() + "\"" +",";

  unsigned long seconds = millis() / 1000;
  unsigned int hours = seconds / 3600;
  unsigned int minutes = (seconds % 3600) / 60;
  unsigned int secs = seconds % 60;

  char uptimeStr[32];
  snprintf(uptimeStr, sizeof(uptimeStr), "%02uh %02um %02us", hours, minutes, secs);

  json += "\"uptime\":\"" + String(uptimeStr) + "\",";

  json += "\"temp\":" + String(temperatureRead(), 1) + ",";
  json += "\"client\":" + String(client->id());
  json += "}";

  client->text(json);

  Serial.println(json);
}


void autoSpeed() {
  int fanSpeed = 40; // Default fallback speed

  // If sensor data is missing (NaN), stick to fallback and exit
  if (isnan(temp) || isnan(hum)) {
    setFanSpeed(fanSpeed);
    return;
  }

  // Base fan speed logic (based on temperature)
  if (temp < 22) {
    fanSpeed = 0;  // Too cold, turn off
  } else if (temp < 24) {
    fanSpeed = 30;
  } else if (temp < 26) {
    fanSpeed = 45;
  } else if (temp < 28) {
    fanSpeed = 60;
  } else if (temp < 30) {
    fanSpeed = 75;
  } else if (temp < 32) {
    fanSpeed = 90;
  } else {
    fanSpeed = 100; // Very hot
  }

  // Apply humidity adjustment
  if (hum >= 70 && temp >= 26) {
    fanSpeed += 5;
  }
  if (hum >= 80 && temp >= 28) {
    fanSpeed += 5; // Additional boost
  }

  // Final safety clamp (robustness)
  fanSpeed = constrain(fanSpeed, 0, 100);

  // Optional debug
  Serial.printf("[AutoSpeed] Temp: %.1f°C, Hum: %.1f%% -> Fan: %d%%\n", temp, hum, fanSpeed);

  setFanSpeed(fanSpeed);
}

// auto Ambience



float calculateFeelsLike(float tempC, float humidity) {
  float feelsLike = tempC;

  if (tempC >= 26.0) {
    // Rothfusz regression (approximation for heat index in °C)
    float t = tempC;
    float h = humidity;

    feelsLike = -8.7847 
                + (1.6114 * t) 
                + (2.3385 * h) 
                - (0.1461 * t * h)
                - (0.0123 * t * t)
                - (0.0164 * h * h)
                + (0.0022 * t * t * h)
                + (0.0007 * t * h * h)
                - (0.00000358 * t * t * h * h);
  } else {
    // Chill effect below 26°C
    feelsLike = tempC - ((humidity - 50.0) / 10.0);  // reduces or increases depending on humidity
  }

  return feelsLike;
}

float feelsLike = calculateFeelsLike(temp, hum);

const char* moodColors[25] = {
  "208,240,255", // 22°C - Icy Blue
  "198,235,255",
  "188,230,255",
  "178,225,255",
  "168,220,255",

  "210,245,255",
  "230,250,255",
  "240,253,255",
  "250,255,255",
  "255,255,255", // Arctic White

  "255,252,230",
  "255,248,210",
  "255,244,190",
  "255,240,170",
  "255,232,150",

  "255,224,130",
  "255,216,110",
  "255,208,90",
  "255,196,70",
  "255,180,50",

  "255,164,35",
  "255,148,20",
  "255,132,10",
  "255,116,5",
  "255,100,0"   // 40°C - Warm Orange
};

void autoLights() {
  if (isnan(temp) || isnan(hum)) {
    setColorFromString("255,255,255"); // fallback on sensor failure
    return;
  }

  feelsLike = calculateFeelsLike(temp, hum);  // Focused range

  // Map 22.0–40.0°C to 0–24 index
  int index = map(feelsLike * 100, 2200, 4000, 0, 24);
  index = constrain(index, 0, 24);

  const char* rgbColor = moodColors[index];

  Serial.printf("[AutoLights] Temp: %.1f°C, Hum: %.1f%% → FeelsLike: %.1f°C → RGB: %s\n",
                temp, hum, feelsLike, rgbColor);

  setColorFromString(String(rgbColor));
}






void setup() {
  Serial.begin(115200);


  // Initialize GPIOs
  pinMode(DHT22_PIN, INPUT);
  pinMode(NEOPIXEL_PIN, OUTPUT);

  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  pinMode(RELAY_3, OUTPUT);
  pinMode(RELAY_4, OUTPUT);

  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);
  pinMode(BUTTON_4, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON_1), button1_ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_2), button2_ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_3), button3_ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_4), button4_ISR, FALLING);

  pinMode(LED_PIN, OUTPUT);

  // Configure LEDC (PWM) channel
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(FAN_PWM_PIN, PWM_CHANNEL);
  setFanSpeed(50);


  // Debug info
  Serial.println("GPIOs mapped and initialized.");


  connectToWiFi();

  if (connectedToWiFi) {
    setupMDNS();
  }

  if (!LittleFS.begin(true)) {
    Serial.println("[LittleFS] Mount failed!");
    return;
  }
  Serial.println("[LittleFS] Filesystem mounted.");

  
  dht.begin();
  Serial.println("[DHT] Sensor initialized.");

  strip.begin();
  setColorFromString("255, 170, 80");
  setBrightnessFromString("50");
  strip.show();
  Serial.println("[NEOPIXEL] Initialized.");

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.serveStatic("/data", LittleFS, "/data");
  server.serveStatic("/favicon.ico", LittleFS, "/data/img/favicon.ico");
  server.serveStatic("/", LittleFS, "/").setDefaultFile("login.html");
  server.begin();
  Serial.println("[HTTP] Server started on port 80");

}

void loop() {
  //Neopixel breathe effect
  if (breathingEnabled) {
    updateBreathingEffect();
  }
  //Neopixel cycle effect
  if (cyclesEnabled) {
    updateColorCycle();
  }

  static unsigned long lastRun = 0;
  unsigned long now = millis();

  if (now - lastRun >= 1000) {  // Run every 1000 ms (1 second)
    lastRun = now;

    if (activeClient) {
      readAndLogDHT(activeClient);
      sendDeviceInfo(activeClient);
    }
  }


  if (now - lastAutoSpeedUpdate >= autoSpeedInterval)  {
    unsigned long now = millis();
    if (autoFanSpeed) {
      lastAutoSpeedUpdate = now;
      autoSpeed();  // Run once every second
    }
    if (autoAmbience) {
      autoLights();  // Run once every second
    }
    
  }

  
}
