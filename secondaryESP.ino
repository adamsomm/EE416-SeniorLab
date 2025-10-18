#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <queue>

BLEScan* pBLEScan; 
int scanTime = 1;
BLEAddress targetDeviceAddress("54:dc:e9:1d:34:ff");

template <typename T>
class RollingAverage {
  private:
    std::queue<T> data_queue;
    double current_sum = 0.0;
    size_t max_size;

  public:
    explicit RollingAverage(size_t size) : max_size(size) {}

    void add_value(T value){
      data_queue.push(value);
      current_sum += value;

      if(data_queue.size() > max_size){
        current_sum -= data_queue.front();
        data_queue.pop();
      }
    }

    double get_average() const {
      if(data_queue.empty()){
        return 0.0;
      }
      return current_sum / data_queue.size();
    }
};

RollingAverage<int> rssiAvg(10);

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getAddress().equals(targetDeviceAddress)) {
      // Print ONLY the RSSI number and a newline
      int deviceRSSI = advertisedDevice.getRSSI();

      rssiAvg.add_value(deviceRSSI);

      Serial.print("Raw RSSI: ");
      Serial.print(deviceRSSI);
      Serial.print(" | Rolling Average: ");
      Serial.println(rssiAvg.get_average());  
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








