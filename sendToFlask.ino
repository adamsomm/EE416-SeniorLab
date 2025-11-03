#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---------------------- Configuration ----------------------
// Wi-Fi Credentials
const char* ssid = "ClarksonGuest";
const char* password = "";

// Flask Server Details
// IMPORTANT: Use the actual IP address of the computer running the Flask server,
// and the port it's running on (default is 5000). Do NOT use localhost or 127.0.0.1.
const char* serverAddress = "http://10.131.206.180:5000/receive_data"; 

// Interval for sending data (in milliseconds)
const long postingInterval = 5000; 
unsigned long lastConnectionTime = 0;

// ---------------------- WiFi Connection Setup ----------------------

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected.");
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Flask Server address: ");
  Serial.println(serverAddress);
}

// ---------------------- Data Sending Function ----------------------

void sendDataToServer() {
  if (WiFi.status() == WL_CONNECTED) {
    
    // 1. --- Simulate Sensor Readings ---
    float temp = random(200, 300) / 10.0; // Random temperature between 20.0 and 30.0
    float hum = random(400, 700) / 10.0;  // Random humidity between 40.0 and 70.0

    // 2. --- Create JSON Payload ---
    StaticJsonDocument<200> doc;
    doc["device_id"] = "ESP32_Sensor_01";
    doc["temperature"] = temp;
    doc["humidity"] = hum;

    String jsonPayload;
    serializeJson(doc, jsonPayload);
    
    Serial.print("Sending payload: ");
    Serial.println(jsonPayload);

    // 3. --- Send HTTP POST Request ---
    HTTPClient http;
    http.begin(serverAddress);
    // Specify content-type to let the server know we're sending JSON
    http.addHeader("Content-Type", "application/json"); 
    
    int httpResponseCode = http.POST(jsonPayload); 

    // 4. --- Handle Response ---
    if (httpResponseCode > 0) {
      Serial.printf("[HTTP] POST... code: %d\n", httpResponseCode);
      String response = http.getString();
      Serial.print("Server Response: ");
      Serial.println(response);
    } else {
      Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    // 5. --- Close Connection ---
    http.end();
  } else {
    Serial.println("WiFi Disconnected. Reconnecting...");
    // Optionally add code here to re-attempt connection
  }
}

// ---------------------- Main Loop ----------------------

void loop() {
  // Check if it's time to send data
  if (millis() - lastConnectionTime > postingInterval) {
    sendDataToServer();
    lastConnectionTime = millis();
  }
}