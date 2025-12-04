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
#include "Arduino.h"

// ----------------- CONFIG -----------------
#define MAX_DEVICES 5
#define ESPNOW_QUEUE_LENGTH 32
#define QUEUE_ITEM_SIZE sizeof(EsponQueueItem)
#define BLE_SCAN_SECONDS 1          // short scan
#define RSSI_ROLLING_SIZE 10
#define SEND_THROTTLE_MS 5000       // min time between POSTs for same tag
const char* ssid = "ClarksonGuest";
const char* password = "";
const char* serverAddress = "http://10.128.41.115:5000/receive_data";

// ----------------- TYPES -----------------
struct EsponQueueItem {
  char mac[18];        // null-terminated MAC str
  float anchorRssi;
};

// ESP-NOW incoming message format 
struct struct_device_data {
  char mac_addr[18];
  float average_rssi;
};

typedef struct struct_message {
  int device_count;
  struct_device_data devices[MAX_DEVICES];
} struct_message;

// ----------------- GLOBALS -----------------
BLEScan* pBLEScan;
int scanTime = BLE_SCAN_SECONDS;

BLEAddress targetDevices[] = {
    BLEAddress("54:dc:e9:1d:34:ff"),
    BLEAddress("8c:6f:b9:a7:de:4e"),
    BLEAddress("8c:6f:b9:a7:de:7f")
};
int numTargetDevices = sizeof(targetDevices) / sizeof(targetDevices[0]);

// rolling RSSI per device seen by *this* gateway (from BLE)
std::map<std::string, RollingAverage<int>> deviceRssiMap;

// map for last sent status and last time sent (debounce)
std::map<std::string, bool> lastSentStatus;
std::map<std::string, unsigned long> lastSentMillis;

ProximityCalculator proxy;

// FreeRTOS queue to pass minimal data from ISR -> main loop
QueueHandle_t espnowQueue = NULL;

// ----------------- NETWORK SAFE HTTP SENDER -----------------
void sendAttendanceUpdate(const std::string &tag_id, bool is_present) {
  // This runs in task/main context (NOT in ISR)
  if (WiFi.status() != WL_CONNECTED) {
    // Try to reconnect quickly (non-blocking attempt)
    WiFi.reconnect();
    unsigned long start = millis();
    while (millis() - start < 1000 && WiFi.status() != WL_CONNECTED) {
      delay(10); // short wait; still in main context
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi still disconnected. Skipping HTTP POST.");
      return;
    }
  }

  JsonDocument doc;
  doc.clear();
  doc["tag_id"] = tag_id;
  doc["is_present"] = is_present;
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  Serial.print("HTTP payload -> ");
  Serial.println(jsonPayload);

  HTTPClient http;
  http.begin(serverAddress);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST((uint8_t*)jsonPayload.c_str(), jsonPayload.length());

  if (httpResponseCode > 0) {
    Serial.printf("[HTTP] POST code: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.print("Server Response: ");
    Serial.println(response);
  } else {
    Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

// ----------------- BLE Callback (only updates RollingAverage) -----------------
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    // Only update RSSI if the device is one of the tracked target devices
    std::string macStr = advertisedDevice.getAddress().toString().c_str();
    int rssi = advertisedDevice.getRSSI();

    auto it = deviceRssiMap.find(macStr);
    if (it != deviceRssiMap.end()) {
      it->second.add_value(rssi);
      // small optional debug:
      // Serial.printf("BLE updated %s -> %d (avg %d)\n", macStr.c_str(), rssi, it->second.get_average());
    }
  }
};

// ----------------- ESP-NOW RX: very small and safe; queue minimal struct -----------------
void OnDataRecv(const esp_now_recv_info* info, const uint8_t* incomingData, int len) {
  // Called in ISR context: must be fast and safe.
  struct_message myData;
  if (len != sizeof(myData)) {
    // if sizes mismatch we still try to memcpy but warn (do NOT do allocations)
    memcpy(&myData, incomingData, min(len, (int)sizeof(myData)));
  } else {
    memcpy(&myData, incomingData, sizeof(myData));
  }

  for (int i = 0; i < myData.device_count; i++) {
    EsponQueueItem qItem;
    strncpy(qItem.mac, myData.devices[i].mac_addr, sizeof(qItem.mac));
    qItem.mac[sizeof(qItem.mac)-1] = '\0';
    qItem.anchorRssi = myData.devices[i].average_rssi;

    // Send to FreeRTOS queue from ISR
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(espnowQueue, &qItem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
}

// ----------------- Setup -----------------
void setup() {
  Serial.begin(115200);
  delay(100);

  // Wi-Fi initial mode (STA) - do not block on connect here
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println(WiFi.channel());


  Serial.print("WiFi connecting...");
  unsigned long start = millis();
  while (millis() - start < 2000 && WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi not yet connected (will try later).");
  }

  // Initialize BLE and scanning (passive scan reduces radio contention)
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(false); // passive = less radio contention. set true if you need active scan.
  pBLEScan->setInterval(200);     // ms
  pBLEScan->setWindow(30);        // ms (smaller window reduces BLE duty)

  Serial.println("Tracking devices:");
  for (int i = 0; i < numTargetDevices; i++) {
    std::string macStr = targetDevices[i].toString().c_str();
    deviceRssiMap.emplace(macStr, RollingAverage<int>(RSSI_ROLLING_SIZE));
    lastSentStatus[macStr] = false;        // default absent
    lastSentMillis[macStr] = 0;
    Serial.println(macStr.c_str());
  }

  // Create the FreeRTOS queue
  espnowQueue = xQueueCreate(ESPNOW_QUEUE_LENGTH, sizeof(EsponQueueItem));
  if (espnowQueue == NULL) {
    Serial.println("Failed to create espnowQueue");
  }

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    // do not return; we might still want to attempt to recover later
  } else {
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("ESP-NOW initialized and recv callback registered.");
  }

  // small delay
  delay(100);
}

// ----------------- MAIN LOOP: BLE scan + process queue + send HTTP safely -----------------
void loop() {
  // 1) Start a short BLE scan (blocking but short)
  pBLEScan->start(scanTime, false);
  pBLEScan->clearResults(); // free memory

  // 2) Process any pending ESP-NOW items (do not block long here)
  EsponQueueItem item;
  // Dequeue up to some number per loop iteration to avoid hogging CPU:
  int processed = 0;
  const int maxProcessPerLoop = 8;
  while (xQueueReceive(espnowQueue, &item, 0) == pdTRUE && processed < maxProcessPerLoop) {
    processed++;

    std::string macStr(item.mac);

    // Look up rolling RSSI for this device from BLE
    auto it = deviceRssiMap.find(macStr);
    if (it == deviceRssiMap.end()) {
      Serial.printf("Received ESP-NOW for unknown device %s\n", item.mac);
      continue;
    }

    int gatewayAvgRssi = it->second.get_average();
    float anchorRssi = item.anchorRssi;

    bool isHere = proxy.isInRoom(anchorRssi, gatewayAvgRssi);

    Serial.println("--------------------");
    Serial.print("Device: "); Serial.println(macStr.c_str());
    Serial.print("Anchor RSSI: "); Serial.print(anchorRssi);
    Serial.print(" | Gateway RSSI: "); Serial.println(gatewayAvgRssi);
    Serial.print("Presence: "); Serial.println(isHere ? "PRESENT" : "ABSENT");

    // Debounce and rate-limit logic
    bool needSend = false;
    unsigned long now = millis();
    if (lastSentStatus.find(macStr) == lastSentStatus.end()) {
      // first time seeing it from this gateway
      needSend = true;
    } else {
      bool prev = lastSentStatus[macStr];
      unsigned long prevMillis = lastSentMillis[macStr];
      if (prev != isHere) {
        // state changed -> send immediately
        needSend = true;
      } else if (now - prevMillis > SEND_THROTTLE_MS) {
        // no change but it's been long enough, optionally refresh
        needSend = false; // keep false by default to avoid spamming; set true if you want periodic refresh
      }
    }

    if (needSend) {
      lastSentStatus[macStr] = isHere;
      lastSentMillis[macStr] = now;
      Serial.println(">>> Sending update to server (from loop)");
      sendAttendanceUpdate(macStr, isHere);
    } else {
      Serial.println("Status unchanged or throttled; not sending.");
    }
    Serial.println("--------------------");
  }

  // small delay to yield CPU and let other RTOS things run
  delay(200);
}
