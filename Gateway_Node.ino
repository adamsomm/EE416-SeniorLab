#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_now.h>
#include <WiFi.h>
#include <map>
#include <string>
#include <ProximityCalculator.h> 
#include <Geometry.h>
#include "RollingAverage.h"      

#define MAX_DEVICES 5

// ---------- BLE scanning setup ----------
BLEScan* pBLEScan;
int scanTime = 2;  // seconds

BLEAddress targetDevices[] = {
    BLEAddress("54:dc:e9:1d:34:ff"),
    BLEAddress("8C:6F:B9:A7:DE:4E")
};
int numTargetDevices = sizeof(targetDevices) / sizeof(targetDevices[0]);

// Map to store rolling averages of RSSI per MMU
std::map<std::string, RollingAverage<int>> deviceRssiMap;

// ---------- BLE callback ----------
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        std::string macStr = advertisedDevice.getAddress().toString().c_str();
        int rssi = advertisedDevice.getRSSI();

        auto it = deviceRssiMap.find(macStr);
        if (it != deviceRssiMap.end()) {
            it->second.add_value(rssi);
            Serial.print("BLE RSSI updated for ");
            Serial.print(macStr.c_str());
            Serial.print(": ");
            Serial.println(it->second.get_average());
        } else {
            // Serial.print("Unknown device ignored: ");
            // Serial.println(macStr.c_str());
        }
    }
};

// ---------- ESP-NOW reception ----------
struct struct_device_data {
    char mac_addr[18];
    float average_rssi;
};

typedef struct struct_message {
    int device_count;
    struct_device_data devices[MAX_DEVICES];
} struct_message;

std::map<std::string, float> mmuDataMap;  // store anchor data
ProximityCalculator proxy;

void OnDataRecv(const esp_now_recv_info* info, const uint8_t* incomingData, int len) {
    struct_message myData;
    memcpy(&myData, incomingData, sizeof(myData));

    for (int i = 0; i < myData.device_count; i++) {
        std::string macStr(myData.devices[i].mac_addr);
        float rssi = myData.devices[i].average_rssi;
        mmuDataMap[macStr] = rssi;

        // Serial.print("Anchor data received: ");
        // Serial.print(macStr.c_str());
        // Serial.print(" | RSSI: ");
        // Serial.println(rssi);

        auto it = deviceRssiMap.find(macStr);
        if (it != deviceRssiMap.end()) {
            bool isHere = proxy.isInRoom(mmuDataMap[macStr], it->second.get_average());
            // Serial.print("Anchr Node RSSI Value: ");
            // Serial.print(it->second.get_average());
            Serial.print("Anchor RSSI Distance: ");
            Serial.println(proxy.rssiToDistance(mmuDataMap[macStr]));
            // Serial.println("Gateway Node RSSI Value: ");
            // Serial.print(mmuDataMap[macStr]);
            Serial.print("Gateway Node RSSI Distance: ");
            Serial.println(proxy.rssiToDistance(it->second.get_average()));
            Serial.print("Attendance Status: ");

            Serial.println(isHere);
        } else {
            Serial.print("Warning: Received ESP-NOW data for unknown BLE device ");
            Serial.println(macStr.c_str());
        }
    }
}

// ---------- Setup ----------
void setup() {
    Serial.begin(115200);

    // BLE setup
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    Serial.println("Tracking devices:");
    for (int i = 0; i < numTargetDevices; i++) {
        std::string macStr = targetDevices[i].toString().c_str();
        deviceRssiMap.emplace(macStr, RollingAverage<int>(10));  // rolling buffer size 10
        Serial.println(macStr.c_str());
    }

    // ESP-NOW setup
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);
}

// ---------- Loop ----------
void loop() {
    pBLEScan->start(scanTime, false);
    pBLEScan->clearResults();  // free memory

    for (auto const& pair : deviceRssiMap) {
        Serial.print("MAC: "); Serial.print(pair.first.c_str());
        Serial.print(" | Gateway RSSI: "); Serial.println(pair.second.get_average());
    }

    for (auto const& pair : mmuDataMap) {
        Serial.print("MAC: "); Serial.print(pair.first.c_str());
        Serial.print(" | Anchor RSSI: "); Serial.println(pair.second);
    }

    delay(2000);
}
