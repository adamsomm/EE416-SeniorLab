#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <esp_now.h>
#include <WiFi.h>
#include <map>
#include <string>
#include <BLEAdvertisedDevice.h>
#include <esp_wifi.h>
#include "RollingAverage.h"

#define MAX_DEVICES 5
#define WIFI_CHANNEL 1
#define RSSI_ROLLING_SIZE 10

BLEScan* pBLEScan;
int scanTime = 1;

// MMU MAC Addresses 
BLEAddress targetDevices[] = {
  BLEAddress("54:dc:e9:1d:34:ff"),
  BLEAddress("8c:6f:b9:a7:dE:4e"),
  BLEAddress("8c:6f:b9:a7:de:7f")
 
};

int numTargetDevices = sizeof(targetDevices) / sizeof(targetDevices[0]);

std::map<std::string, RollingAverage<int>> deviceRssiMap;

// Gateway MAC Address
uint8_t broadcastAddress[] = {0xD0, 0xCF, 0x13, 0x19, 0x93, 0x38};
// uint8_t broadcastAddress[] = {0xCC, 0xDB, 0xA7, 0x97, 0xB8, 0x20};

// Structure to hold data for a single device.
typedef struct struct_device_data {
  char mac_addr[18];
  float average_rssi;
} struct_device_data;

// Structure to send data
// Must match the receiver structure
typedef struct struct_message {
  int device_count;
  struct_device_data devices[MAX_DEVICES];
} struct_message;

// Create a struct_message called myData
struct_message myData;
esp_now_peer_info_t peerInfo;

// callback when data is sent
void OnDataSent(const wifi_tx_info_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    std::string mac_str = advertisedDevice.getAddress().toString().c_str();

    // Check if the found device is one we are tracking 
    // .find() returns .end() if it does not find it 
    auto it = deviceRssiMap.find(mac_str);
  
    if(it != deviceRssiMap.end()) { 
      int deviceRSSI = advertisedDevice.getRSSI();
      it->second.add_value(deviceRSSI);
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Scanning...");

  // wifi Setup
  WiFi.mode(WIFI_STA);
  if (esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.println("Error setting WiFi channel");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  // Populate map with target devices.
  Serial.println("Tracking devices: ");
  for (int i = 0; i < numTargetDevices; i++) {
    std::string mac_str = targetDevices[i].toString().c_str();
    deviceRssiMap.emplace(mac_str, RSSI_ROLLING_SIZE);
    Serial.printf(" - %s\n", mac_str.c_str());
  }

  // BLE Setup
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();  // create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);  // active scan -> uses more power but faster results
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal to setInterval value
}


void loop() {
  pBLEScan->start(scanTime, false);

  if (deviceRssiMap.empty()) {
    Serial.println("No devices are being tracked.");
    return;
  }
  
  // Building the packet.
  myData.device_count = 0;
  const int min_readings_to_send = 5;

  for(const auto& pair : deviceRssiMap) {
    if (pair.second.get_count() >= min_readings_to_send) {
      if (myData.device_count >= MAX_DEVICES) {
        Serial.println("Packet full, sending partial list.");
        break;
      }
      // Copy MAC Address
      strncpy(myData.devices[myData.device_count].mac_addr,
              pair.first.c_str(),
              sizeof(myData.devices[myData.device_count].mac_addr));
      
      myData.devices[myData.device_count].average_rssi = pair.second.get_average();
      myData.device_count++;
    }
  }
    
    // Send packet over ESP-NOW.
    if (myData.device_count > 0) {
      esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
      
      if (result == ESP_OK) {
        Serial.println("Sent continuous RSSI average with success");
      } else {
        Serial.println("Error sending the data");
      }
    }
    else {
      // This will only print if no packets have *ever* been received
      Serial.println("No target device found yet...");
    }  
}
