#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <esp_now.h>
#include <WiFi.h>
#include <BLEAdvertisedDevice.h>
#include "RollingAverage.h"

BLEScan* pBLEScan;
int scanTime = 1;

// MMU MAC Address
BLEAddress targetDeviceAddress("54:dc:e9:1d:34:ff");

// Gateway MAC Address
uint8_t broadcastAddress[] = {0xD0, 0xCF, 0x13, 0x19, 0x93, 0x38};
// uint8_t broadcastAddress[] = {0xD0, 0xCF, 0x13, 0x19, 0x87, 0xF8};


// Structure example to send data
// Must match the receiver structure
typedef struct struct_message {
  char a[32];
  float b;
} struct_message;

// Create a struct_message called myData
struct_message myData;
esp_now_peer_info_t peerInfo;

// callback when data is sent
void OnDataSent(const wifi_tx_info_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}


RollingAverage<int> rssiAvg(10);

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getAddress().equals(targetDeviceAddress)) {

      int deviceRSSI = advertisedDevice.getRSSI();

      rssiAvg.add_value(deviceRSSI);

      Serial.printf("Raw RSSI: %d | Rolling Average: %.2f\n",deviceRSSI, rssiAvg.get_average());
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Scanning...");

  // wifi Setup
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
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

  // Check if we have *any* data in the rolling average
  if (rssiAvg.get_count() > 0) {
    // Add data to ESP Now transfer
    strcpy(myData.a, targetDeviceAddress.toString().c_str());
    myData.b = rssiAvg.get_average();
    
    // Send over ESP-NOW
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
    
    if (result == ESP_OK) {
      Serial.println("Sent continuous RSSI average with success");
    }
    else {
      Serial.println("Error sending the data");
    }
  }
  else {
    // This will only print if no packets have *ever* been received
    Serial.println("No target device found yet...");
  }
}
