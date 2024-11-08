#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>

// Access Point credentials
bool isAPMode = false;
char ap_ssid[32];
String home_ssid;
String home_password;

// Create a WebServer object
WebServer server(80);

// Desired mDNS hostname
String mdns_name;

// LED setup for SK6812 RGBW
#define LED_PIN 7
#define NUM_LEDS 21
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRBW + NEO_KHZ800);

#define CLEAR_CREDENTIALS_BUTTON_PIN 0
unsigned long buttonPressStartTime = 0;
bool credentialsCleared = false;

unsigned long previousMillis = 0;
const long interval = 500;
bool ledState = false;

const char* deviceName = "LumiBrick";
const char* version = "1.0.0-alpha.11";
bool ledIsOn = false;
int ledColorR = 0, ledColorG = 0, ledColorB = 0;

// Function prototypes
void setupWiFi();
void startAPMode();
void startWebServer();
void saveCredentials();
void loadCredentials();
void handleRoot();
void handleConnect();
void handleLEDControl();
void handleGetDeviceInfo();
void blinkLED();
void blinkWiFiAttemptLED();
String createUniqueSSID();
void monitorCredentialsClearButton();
void handleWifiScan();
void handleUpdateFirmware();
void performOTAUpdate(String firmwareURL);

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.show();  // LED'leri başlat

  Serial.println("Device name: ");
  Serial.println(deviceName);
  Serial.println("Version: ");
  Serial.println(version);

  pinMode(CLEAR_CREDENTIALS_BUTTON_PIN, INPUT_PULLUP);
  EEPROM.begin(512);  // EEPROM başlatma

  // Wi-Fi modunu başlat
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();  // Var olan bağlantıları kes
  
  delay(100);  // Modülün başlatılması için küçük bir gecikme

  // Benzersiz SSID ve mDNS adı oluştur
  String ssid = createUniqueSSID();
  ssid.toCharArray(ap_ssid, ssid.length() + 1);
  mdns_name = "lumibrick-" + String(WiFi.macAddress()).substring(12);
  mdns_name.replace(":", "");

  // EEPROM'dan kaydedilmiş Wi-Fi bilgilerini yükle
  loadCredentials();

  if (home_ssid.length() > 0 && home_password.length() > 0) {
    // Eğer Wi-Fi bilgisi varsa Wi-Fi'ye bağlanmayı dene
    setupWiFi();
  } else {
    // Eğer Wi-Fi bilgisi yoksa AP modunu başlat
    startAPMode();
  }

  // Web sunucusunu başlat
  startWebServer();
}

String createUniqueSSID() {
  String mac = WiFi.macAddress();
  Serial.println("MAC Address in createUniqueSSID: " + mac);
  mac.replace(":", "");
  String uniqueSSID = "HomeVision_LumiBrick_" + mac.substring(8);
  return uniqueSSID;
}

void loop() {
  monitorCredentialsClearButton();
  
  // Handle client requests
  server.handleClient();

  // Blink LED in AP mode
  if (isAPMode) {
    blinkLED();
  } else {
    // Check Wi-Fi status and attempt to reconnect if disconnected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected, attempting to reconnect...");
    unsigned long reconnectAttemptTime = millis();

    // Try to reconnect for 30 seconds
    while (WiFi.status() != WL_CONNECTED && millis() - reconnectAttemptTime < 30000) {
      delay(500);
      Serial.print(".");
      blinkWiFiAttemptLED();  // Continue blue LED blinking while attempting reconnection
    }

    // Check if reconnection was successful
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconnected to Wi-Fi");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      // Restart mDNS responder after reconnection
      if (!MDNS.begin(mdns_name.c_str())) {
        Serial.println("Error setting up mDNS responder after reconnection!");
      } else {
        Serial.println("mDNS responder restarted successfully.");
      }

      // Turn on green LEDs to indicate reconnection success
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(0, 255, 0));  // Green
      }
      strip.show();
    } else {
      Serial.println("\nReconnection attempt failed, starting AP mode.");
      startAPMode();  // Re-enter AP mode if reconnection fails
    }
  }
  }
}

// Function to setup Wi-Fi
// Function to setup Wi-Fi
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(home_ssid.c_str(), home_password.c_str());
  Serial.println("Attempting to connect to Wi-Fi...");

  unsigned long startAttemptTime = millis();

  // Try to connect for 30 seconds, then timeout
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
    delay(500);
    Serial.print(".");
    blinkWiFiAttemptLED();  // Mavi yanıp sönme işlemi devam edecek
  }

  // Check if connection was successful
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to Wi-Fi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Start mDNS responder
    if (!MDNS.begin(mdns_name.c_str())) {
      Serial.println("Error setting up mDNS responder on home network!");
    } else {
      Serial.println("mDNS responder started successfully. You can access your device via: " + mdns_name + ".local");
    }

    // Turn on green LEDs to indicate success
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(0, 255, 0));  // Green
    }
    strip.show();

    isAPMode = false;  // Ensure AP mode is disabled
  } else {
    Serial.println("\nFailed to connect to Wi-Fi, switching to AP mode.");
    startAPMode();  // Start Access Point mode if Wi-Fi connection fails
  }
}

// Function to start Access Point mode
void startAPMode() {
  // Start the Access Point
  isAPMode = true;
  WiFi.softAP(ap_ssid);
  Serial.println("Access Point started");

  // Print the IP address of the ESP32
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Start the mDNS responder for the Access Point
  if (!MDNS.begin(mdns_name.c_str())) {
    Serial.println("Error setting up mDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started (AP mode)");
}

// Function to start web server
void startWebServer() {
  // Serve the HTML form to capture Wi-Fi credentials
  server.on("/", HTTP_GET, handleRoot);

  // Handle form submission and connect to the Wi-Fi network
  server.on("/connect", HTTP_POST, handleConnect);

  // Handle LED control via mDNS
  server.on("/led", HTTP_GET, handleLEDStatus);
  server.on("/led", HTTP_POST, handleLEDControl);

  // Handle request to get device info
  server.on("/device_info", HTTP_GET, handleGetDeviceInfo);

  // Define a new endpoint to scan Wi-Fi networks
  server.on("/scan", HTTP_GET, handleWifiScan);

  server.on("/update_firmware", HTTP_POST, handleUpdateFirmware);

  // Start the server
  server.begin();
  Serial.println("HTTP server started");
}

// Function to save Wi-Fi credentials to EEPROM
void saveCredentials() {
  int ssidLength = home_ssid.length();
  int passwordLength = home_password.length();

  EEPROM.write(0, ssidLength);
  for (int i = 0; i < ssidLength; ++i) {
    EEPROM.write(1 + i, home_ssid[i]);
  }

  EEPROM.write(1 + ssidLength, passwordLength);
  for (int i = 0; i < passwordLength; ++i) {
    EEPROM.write(2 + ssidLength + i, home_password[i]);
  }

  EEPROM.commit();
}

// Function to clear Wi-Fi credentials from EEPROM
void clearCredentials() {
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("Wi-Fi credentials cleared from EEPROM.");
  esp_restart();
}

// Button control function to clear Wi-Fi credentials from EEPROM
void monitorCredentialsClearButton() {
  int buttonState = digitalRead(CLEAR_CREDENTIALS_BUTTON_PIN);

  // Detect button press
  if (buttonState == LOW) {
    if (buttonPressStartTime == 0) {
      buttonPressStartTime = millis(); // Record the time the button was pressed
    }

    // If button is held down for more than 3 seconds, clear credentials
    if (!credentialsCleared && (millis() - buttonPressStartTime > 3000)) {
      credentialsCleared = true;
      clearCredentials(); // Clear Wi-Fi credentials
    }
  } else {
    // Reset variables when button is released
    buttonPressStartTime = 0;
    credentialsCleared = false;
  }
}

// Function to load Wi-Fi credentials from EEPROM
void loadCredentials() {
  int ssidLength = EEPROM.read(0);
  char ssidBuffer[ssidLength + 1];
  for (int i = 0; i < ssidLength; ++i) {
    ssidBuffer[i] = EEPROM.read(1 + i);
  }
  ssidBuffer[ssidLength] = '\0';
  home_ssid = String(ssidBuffer);

  int passwordLength = EEPROM.read(1 + ssidLength);
  char passwordBuffer[passwordLength + 1];
  for (int i = 0; i < passwordLength; ++i) {
    passwordBuffer[i] = EEPROM.read(2 + ssidLength + i);
  }
  passwordBuffer[passwordLength] = '\0';
  home_password = String(passwordBuffer);
}

// Handle root URL requests ("/")
void handleRoot() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(200, "text/html",
      "<form action='/connect' method='POST'>"
      "SSID: <input type='text' name='ssid'><br>"
      "Password: <input type='text' name='password'><br>"
      "<input type='submit' value='Connect'>"
      "</form>");
  } else {
    String message = "<html><body><h1>Connected to Wi-Fi!</h1>";
    message += "<p>SSID: " + WiFi.SSID() + "</p>";
    message += "<p>IP Address: " + WiFi.localIP().toString() + "</p>";
    message += "</body></html>";
    server.send(200, "text/html", message);
  }
}

// Handle "/connect" URL requests
void handleConnect() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      Serial.println("Failed to parse JSON");
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
      return;
    }
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"WiFi information received\"}");
    home_ssid = doc["ssid"].as<String>();
    home_password = doc["password"].as<String>();

    saveCredentials();

    WiFi.softAPdisconnect();
    isAPMode = false;

    WiFi.begin(home_ssid.c_str(), home_password.c_str());
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
      delay(500);
      Serial.print(".");
      retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to Wi-Fi");

      if (!MDNS.begin(mdns_name.c_str())) {
        Serial.println("Error setting up mDNS responder on home network!");
      } else {
        Serial.println("mDNS responder started on home network (" + mdns_name + ".local)");
      }

    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(0, 255, 0));
    }
    strip.show();

    } else {
      isAPMode = true;
    }
  } else if (server.hasArg("ssid") && server.hasArg("password")) {
    home_ssid = server.arg("ssid");
    home_password = server.arg("password");

    saveCredentials();

    WiFi.softAPdisconnect();
    isAPMode = false;

    WiFi.begin(home_ssid.c_str(), home_password.c_str());
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
      delay(500);
      Serial.print(".");
      retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to Wi-Fi");

      if (!MDNS.begin(mdns_name.c_str())) {
        Serial.println("Error setting up mDNS responder on home network!");
      } else {
        Serial.println("mDNS responder started on home network (" + mdns_name + ".local)");
      }

    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(0, 255, 0));
    }
    strip.show();

      String message = "<html><body><h1>Connected to Wi-Fi!</h1>";
      message += "<p>SSID: " + WiFi.SSID() + "</p>";
      message += "<p>IP Address: " + WiFi.localIP().toString() + "</p>";
      message += "</body></html>";
    } else {
      isAPMode = true;
    }
  } else {
    server.send(400, "text/plain", "Missing SSID or Password");
  }
}

// Handle "/led" URL requests for LED control
void handleLEDStatus() {
    StaticJsonDocument<200> jsonDoc;

    // Make sure the colors are being correctly reflected
    jsonDoc["isOn"] = ledIsOn;
    jsonDoc["color"]["r"] = ledColorR;
    jsonDoc["color"]["g"] = ledColorG;
    jsonDoc["color"]["b"] = ledColorB;

    Serial.printf("Setting LED Color to R: %d, G: %d, B: %d\n", ledColorR, ledColorG, ledColorB);

    String response;
    serializeJson(jsonDoc, response);

    server.send(200, "application/json", response);
}

void handleLEDControl() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, body);

        if (error) {
            Serial.println("JSON deserialization error");
            server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            return;
        }

        if (doc.containsKey("state")) {
            String state = doc["state"].as<String>();
            if (state == "on") {
                ledIsOn = true;

                for (int i = 0; i < NUM_LEDS; i++) {
                    strip.setPixelColor(i, strip.Color(ledColorR, ledColorG, ledColorB));
                }
                strip.show();
                server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"LEDs turned on\"}");
            } else if (state == "off") {
                ledIsOn = false;

                for (int i = 0; i < NUM_LEDS; i++) {
                    strip.setPixelColor(i, strip.Color(0, 0, 0));
                }
                strip.show();
                server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"LEDs turned off\"}");
            } else {
                server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid state value\"}");
            }
        } else if (doc.containsKey("color")) {
            // Set color
            JsonObject color = doc["color"];
            if (color.containsKey("r") && color.containsKey("g") && color.containsKey("b")) {
                ledColorR = color["r"].as<int>();
                ledColorG = color["g"].as<int>();
                ledColorB = color["b"].as<int>();
                
                // Make sure to update the LED only if it is supposed to be on
                if (ledIsOn) {
                    for (int i = 0; i < NUM_LEDS; i++) {
                        strip.setPixelColor(i, strip.Color(ledColorR, ledColorG, ledColorB));
                    }
                    strip.show();
                }
                server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"LED color changed\"}");
            } else {
                server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid color values\"}");
            }
        } else {
            server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No valid parameter found\"}");
        }
    } else {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No JSON body found\"}");
    }
}

// Handle "/get_mdns" URL requests to return mDNS name
void handleGetDeviceInfo() {
  // Create a document to return JSON
  StaticJsonDocument<200> jsonDoc;

  // Add device info to the JSON document
  jsonDoc["macAddress"] = WiFi.macAddress();
  jsonDoc["mdnsName"] = mdns_name + ".local";
  jsonDoc["deviceName"] = deviceName;
  jsonDoc["version"] = version;

  // Convert JSON document to a string
  String jsonResponse;
  serializeJson(jsonDoc, jsonResponse);

  // Send the JSON response
  server.send(200, "application/json", jsonResponse);
}

// Function to scan Wi-Fi and return results in JSON format
void handleWifiScan() {
  Serial.println("Starting Wi-Fi scan...");
  int n = WiFi.scanNetworks();  // Scan for nearby Wi-Fi networks

  if (n == 0) {
    Serial.println("No networks found.");
    server.send(200, "application/json", "{\"networks\": []}");
  } else {
    // Create a JSON document
    DynamicJsonDocument doc(1024);  // Adjust memory size for JSON document
    JsonArray networks = doc.createNestedArray("networks");

    for (int i = 0; i < n; ++i) {
      JsonObject network = networks.createNestedObject();
      network["ssid"] = WiFi.SSID(i);       // SSID
      network["rssi"] = WiFi.RSSI(i);       // Signal strength
      network["encryption"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Secured";  // Encryption type
    }

    String response;
    serializeJson(doc, response);  // Convert JSON format to string
    server.send(200, "application/json", response);  // Respond with JSON

    Serial.println("Scan complete, SSID list sent.");
  }
}

// Blink LED in AP mode
void blinkLED() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    ledState = !ledState;

    for (int i = 0; i < NUM_LEDS; i++) {
      if (ledState) {
        strip.setPixelColor(i, strip.Color(255, 255, 0)); // Yellow
      } else {
        strip.setPixelColor(i, strip.Color(0, 255, 0)); // Green
      }
    }
    strip.show();
  }
}

void blinkWiFiAttemptLED() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    ledState = !ledState;

    for (int i = 0; i < NUM_LEDS; i++) {
      if (ledState) {
        strip.setPixelColor(i, strip.Color(0, 0, 255));  // Blue
      } else {
        strip.setPixelColor(i, strip.Color(0, 0, 0));  // Turn off
      }
    }
    strip.show();
  }
}

void handleUpdateFirmware() {
  if (server.hasArg("firmwareURL")) {
    String firmwareURL = server.arg("firmwareURL");
    server.send(200, "text/plain", "Starting OTA update...");
    performOTAUpdate(firmwareURL);
  } else {
    server.send(400, "text/plain", "Firmware URL missing!");
  }
}

void performOTAUpdate(String firmwareURL) {
  // Check if internet connection is available
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No Wi-Fi connection. OTA update cannot proceed.");
    return;
  }

  HTTPClient http;
  http.begin(firmwareURL);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    
    if (contentLength > 0) {
      bool canBegin = Update.begin(contentLength);

      if (canBegin) {
        WiFiClient *stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);

        if (written == contentLength) {
          Serial.println("Firmware successfully downloaded and written.");
        } else {
          Serial.printf("Firmware download failed: %d/%d bytes downloaded.\n", written, contentLength);
        }

        if (Update.end()) {
          Serial.println("OTA update completed successfully.");
          if (Update.isFinished()) {
            Serial.println("Restarting...");
            ESP.restart();
          } else {
            Serial.println("OTA update not finished.");
          }
        } else {
          Serial.printf("OTA error: %s\n", Update.errorString());
        }
      } else {
        Serial.println("OTA process cannot start. Not enough space.");
      }
    } else {
      Serial.println("Invalid content length.");
    }
  } else {
    Serial.printf("HTTP request failed, error code: %d\n", httpCode);
  }

  http.end();  // Close HTTP connection
}
