#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <map>
#include <string>
#include <ProximityCalculator.h>
#include <Geometry.h>
#include "RollingAverage.h"

#define MAX_DEVICES 5

// ---------- Wi-Fi & Server Configuration ----------
const char* ssid = "ClarksonGuest";
const char* password = "";
const char* serverAddress = "http://10.131.206.180:5000/receive_data";

// ---------- BLE Scanning Setup ----------
BLEScan* pBLEScan;
int scanTime = 2; // seconds

BLEAddress targetDevices[] = {
    BLEAddress("54:dc:e9:1d:34:ff"),
    BLEAddress("8C:6F:B9:A7:DE:4E")
};
int numTargetDevices = sizeof(targetDevices) / sizeof(targetDevices[0]);

// Map to store rolling averages of RSSI per MMU (Gateway's perspective)
std::map<std::string, RollingAverage<int>> deviceRssiMap;

// ---------- ESP-NOW Reception ----------
struct struct_device_data {
    char mac_addr[18];
    float average_rssi;
};

typedef struct struct_message {
    int device_count;
    struct_device_data devices[MAX_DEVICES];
} struct_message;

// Map to store data from Anchor nodes
std::map<std::string, float> mmuDataMap;
// Map to store the last sent status to the server (prevents spam)
std::map<std::string, bool> lastSentStatus;

ProximityCalculator proxy;

// ---------- NEW: HTTP Sending Function ----------
/**
 * Sends the attendance status update to the Flask server.
 * This is called ONLY when a student's status changes.
 */
void sendAttendanceUpdate(std::string tag_id, bool is_present) {
    if (WiFi.status() == WL_CONNECTED) {
        
        // 1. Create JSON Payload
        StaticJsonDocument<100> doc;
        // Your Flask server expects "tag_id" and "is_present"
        doc["tag_id"] = tag_id;
        doc["is_present"] = is_present;

        String jsonPayload;
        serializeJson(doc, jsonPayload);
        
        Serial.print("Sending payload: ");
        Serial.println(jsonPayload);

        // 2. Send HTTP POST Request
        HTTPClient http;
        http.begin(serverAddress);
        http.addHeader("Content-Type", "application/json");
        
        int httpResponseCode = http.POST(jsonPayload);

        // 3. Handle Response
        if (httpResponseCode > 0) {
            Serial.printf("[HTTP] POST... code: %d\n", httpResponseCode);
            String response = http.getString();
            Serial.print("Server Response: ");
            Serial.println(response);
        } else {
            Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
        }

        // 4. Close Connection
        http.end();
    } else {
        Serial.println("WiFi Disconnected. Cannot send update.");
        // Optional: you could try WiFi.reconnect() here
    }
}

// ---------- BLE Callback ----------
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        std::string macStr = advertisedDevice.getAddress().toString().c_str();
        int rssi = advertisedDevice.getRSSI();

        auto it = deviceRssiMap.find(macStr);
        if (it != deviceRssiMap.end()) {
            it->second.add_value(rssi);
            // Serial.print("BLE RSSI updated for ");
            // Serial.print(macStr.c_str());
            // Serial.print(": ");
            // Serial.println(it->second.get_average());
        }
    }
};

// ---------- ESP-NOW Callback ----------
void OnDataRecv(const esp_now_recv_info* info, const uint8_t* incomingData, int len) {
    struct_message myData;
    memcpy(&myData, incomingData, sizeof(myData));

    for (int i = 0; i < myData.device_count; i++) {
        std::string macStr(myData.devices[i].mac_addr);
        float anchorRssi = myData.devices[i].average_rssi;
        mmuDataMap[macStr] = anchorRssi; // Store anchor data

        // Find the matching data from this Gateway's BLE scan
        auto it = deviceRssiMap.find(macStr);
        if (it != deviceRssiMap.end()) {
            
            float gatewayRssi = it->second.get_average();
            
            // Determine presence
            bool isHere = proxy.isInRoom(anchorRssi, gatewayRssi);

            Serial.println("--------------------");
            Serial.print("Device: "); Serial.println(macStr.c_str());
            Serial.print("Anchor RSSI: "); Serial.print(anchorRssi);
            Serial.print(" | Gateway RSSI: "); Serial.println(gatewayRssi);
            Serial.print("Attendance Status: "); Serial.println(isHere ? "PRESENT" : "ABSENT");

            // *** NEW: Check if status has changed before sending ***
            bool currentStatus = isHere;
            
            // Check if this is a new status (or first time seeing device)
            if (lastSentStatus.find(macStr) == lastSentStatus.end() || lastSentStatus[macStr] != currentStatus) {
                Serial.println(">>> New attendance status! Sending to server...");
                sendAttendanceUpdate(macStr, currentStatus);
                lastSentStatus[macStr] = currentStatus; // Update the last sent status
            } else {
                Serial.println("Status unchanged. Not sending.");
            }
            Serial.println("--------------------");

        } else {
            Serial.print("Warning: Received ESP-NOW data for unknown BLE device ");
            Serial.println(macStr.c_str());
        }
    }
}

// ---------- Setup ----------
void setup() {
    Serial.begin(115200);
    delay(100);

    // 1. Initialize Wi-Fi
    Serial.print("Connecting to Wi-Fi: ");
    Serial.println(ssid);
    WiFi.mode(WIFI_STA); // Set STA mode for both Wi-Fi and ESP-NOW
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


    // 2. Initialize BLE
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    Serial.println("Tracking devices:");
    for (int i = 0; i < numTargetDevices; i++) {
        std::string macStr = targetDevices[i].toString().c_str();
        deviceRssiMap.emplace(macStr, RollingAverage<int>(10)); // rolling buffer size 10
        lastSentStatus[macStr] = false; // Default all to "Absent"
        Serial.println(macStr.c_str());
    }

    // 3. Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);
}

// ---------- Loop ----------
void loop() {
    // Start BLE scan
    pBLEScan->start(scanTime, false);
    pBLEScan->clearResults(); // free memory

    // Optional: Print current smoothed RSSI values every loop
    // for (auto const& pair : deviceRssiMap) {
    //     Serial.print("MAC: "); Serial.print(pair.first.c_str());
    //     Serial.print(" | Gateway RSSI (Avg): "); Serial.println(pair.second.get_average());
    // }

    delay(2000); // Wait 2 seconds before next BLE scan
}