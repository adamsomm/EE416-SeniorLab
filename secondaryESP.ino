#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "RollingAverage.h" // <-- Include your new local file

BLEScan* pBLEScan; 
int scanTime = 1;
BLEAddress targetDeviceAddress("54:dc:e9:1d:34:ff");

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

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();  // create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);  // active scan -> uses more power but faster results
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal to setInterval value
  
}



void loop() {
  pBLEScan->start(scanTime, false);
}








